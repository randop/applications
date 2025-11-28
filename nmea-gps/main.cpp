#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string.h>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <TinyGPSPlus.h>
#include <nmeaparse/nmea.h>

#include "nmea/message/gga.hpp"
#include "nmea/message/gll.hpp"
#include "nmea/message/gsa.hpp"
#include "nmea/message/gsv.hpp"
#include "nmea/sentence.hpp"

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>

using namespace boost::asio;
using namespace std::chrono_literals;

class SerialReader {
public:
  SerialReader(io_context &ioc, const std::string &dev,
               unsigned int baud = 115200)
      : port_(ioc), buffer_(8192), running_(true) {
    port_.open(dev);
    port_.set_option(serial_port::baud_rate(baud));
    port_.set_option(serial_port::character_size(8));
    port_.set_option(
        serial_port::flow_control(serial_port::flow_control::none));
    port_.set_option(serial_port::parity(serial_port::parity::none));
    port_.set_option(serial_port::stop_bits(serial_port::stop_bits::one));

    std::cout << "Opened " << dev << " @ " << baud << " baud\n";
    start_read();
  }

  ~SerialReader() { stop(); }

  void stop() {
    if (!running_)
      return;
    running_ = false;

    // Cancel all async operations on the port
    port_.cancel();
    port_.close(); // safe even if already closed
  }

  const boost::circular_buffer<uint8_t> &buffer() const { return buffer_; }
  void clear() { buffer_.clear(); }

private:
  void start_read() {
    if (!running_)
      return;

    port_.async_read_some(
        boost::asio::buffer(tmp_, sizeof(tmp_)),
        [this](boost::system::error_code ec, std::size_t n) {
          if (!ec) {
            for (std::size_t i = 0; i < n; ++i)
              buffer_.push_back(tmp_[i]);
            start_read(); // continue
          } else if (ec != boost::asio::error::operation_aborted) {
            if (running_)
              std::cerr << "Read error: " << ec.message() << '\n';
          }
        });
  }

  serial_port port_;
  uint8_t tmp_[256]{}; // larger = fewer syscalls
  boost::circular_buffer<uint8_t> buffer_;
  bool running_;
};

// Global atomic flag (volatile not needed with std::atomic)
std::atomic<bool> running{true};

// Signal handler
extern "C" void signal_handler(int sig) {
  running.store(false, std::memory_order_release);
  // Optional: print which signal we received
  const char *signame = (sig == SIGINT)    ? "SIGINT"
                        : (sig == SIGTERM) ? "SIGTERM"
                                           : "UNKNOWN";
  std::cerr << "\nReceived " << signame << " - shutting down gracefully...\n";
}

// Helper function — split by \r, \n, or \r\n
std::vector<std::string> split_lines(const std::string &text) {
  std::vector<std::string> lines;
  std::string line;
  line.reserve(80); // typical line length

  for (char c : text) {
    if (c == '\r' || c == '\n') {
      if (!line.empty()) {
        lines.push_back(std::move(line));
        line.clear();
      }
      // Skip the second char if \r\n
      if (c == '\r') {
        // peek ahead safely
        auto next = std::find_if(text.begin() + (&c - &text[0] + 1), text.end(),
                                 [](char ch) { return ch == '\n'; });
        if (next != text.end() && *next == '\n') {
          // skip the \n — we'll process it in next iteration anyway
        }
      }
    } else {
      line += c;
    }
  }

  if (!line.empty()) {
    lines.push_back(std::move(line));
  }

  return lines;
}

// Helper: trim whitespace from both ends
inline std::string trim(const std::string &str) {
  if (str.empty())
    return {};

  auto first = std::find_if(str.begin(), str.end(),
                            [](unsigned char c) { return !std::isspace(c); });

  if (first == str.end())
    return {}; // all whitespace

  auto last = std::find_if(str.rbegin(), str.rend(),
                           [](unsigned char c) { return !std::isspace(c); })
                  .base() -
              1;

  return std::string(first, last + 1);
}

// Updated: splits + trims + skips empty lines
std::vector<std::string> split_serial_lines(const std::string &input) {
  std::vector<std::string> lines;
  std::string current;

  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];

    if (c == '\r' || c == '\n') {
      // Handle \r\n pair
      if (c == '\r' && i + 1 < input.size() && input[i + 1] == '\n') {
        ++i; // skip the \n
      }

      std::string trimmed = trim(current);
      if (!trimmed.empty()) {
        lines.push_back(std::move(trimmed));
      }
      current.clear();
    } else {
      current += c;
    }
  }

  // Don't forget the last line if no terminator at end
  std::string trimmed = trim(current);
  if (!trimmed.empty()) {
    lines.push_back(std::move(trimmed));
  }

  return lines;
}

int main() {
  using namespace std;
  using namespace nmea;

  std::cout << "NMEA-GPS" << std::endl;
  std::string serialPortFile = "/dev/ttyUSB0";

  bool printChars = false;
  bool debugGpsLines = false;

  // Install signal handlers for SIGINT (Ctrl+C) and SIGTERM
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, nullptr) == -1 ||
      sigaction(SIGTERM, &sa, nullptr) == -1) {
    std::cerr << "Failed to install signal handlers: " << strerror(errno)
              << std::endl;
    return EXIT_FAILURE;
  }

  TinyGPSPlus tinygps;

  const char bytestream[] =
      "$GPGSV,5,3,17,8,38,15,,9,23,235,32,659,23,235,23,16,20,81,*41\n";

  NMEAParser parser;
  GPSService gps(parser);
  // (optional) Called when a sentence is valid syntax
  parser.onSentence += [](const NMEASentence &nmea) {
    cout << "Received $" << nmea.name << endl;
    cout << "Received " << (nmea.checksumOK() ? "good" : "bad")
         << " GPS Data: " << nmea.name << endl;
  };
  // (optional) Called when data is read/changed
  gps.onUpdate += [&gps]() {
    // There are *tons* of GPSFix properties
    if (gps.fix.locked()) {
      cout << " # Position: " << gps.fix.latitude << ", " << gps.fix.longitude
           << endl;
    } else {
      cout << "\tQuality: " << gps.fix.quality
           << "\tPosition: " << gps.fix.latitude << "'N, " << gps.fix.longitude
           << "'E" << endl
           << endl;
      cout << "GPS: " << gps.fix.toString() << endl;
      cout << " # Searching..." << endl;
    }
  };

  try {
    parser.readBuffer((uint8_t *)bytestream, sizeof(bytestream));
  } catch (NMEAParseError &err) {
    std::cerr << "Error: " << err.message << std::endl;
  }

  string line;
  ifstream file("nmea_log.txt");
  while (getline(file, line)) {
    try {
      cout << "File, line: " << line << endl;
      parser.readLine(line);
    } catch (NMEAParseError &e) {
      cout << e.message << endl << endl;
    }
  }

  try {
    io_context ioc;
    SerialReader reader(ioc, serialPortFile, 115200);

    std::thread t([&ioc] { ioc.run(); });

    // Main thread just waits for shutdown
    while (running.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!reader.buffer().empty()) {
        auto data = reader.buffer();
        // reader.clear();

        if (printChars) {
          std::cout << "Got " << data.size() << " bytes: ";
          for (uint8_t b : data) {
            if (std::isprint(static_cast<unsigned char>(b))) {
              std::cout << static_cast<char>(b);
            } else {
              std::cout << ' '; // non-printable characters
            }
          }

          std::cout << '\n';
        }

        if (data.size() > 0) {
          std::string text;
          text.reserve(data.size());
          for (uint8_t b : data) {
            text += static_cast<char>(b);
          }

          auto lines = split_serial_lines(text);

          for (const auto &line : lines) {
            if (!line.empty()) {

              if (debugGpsLines) {
                std::cout << line << endl;
              }

              if (line.size() >= 10 && line.rfind("$G", 0) == 0) {
                try {
                  nmea::sentence nmea_sentence(line);

                  if (debugGpsLines) {
                    cout << "GPS type: " << nmea_sentence.type() << endl;
                  }

                  if (nmea_sentence.type() == "GGA") {

                    std::string sentence = line;
                    if (sentence.find('\r') == std::string::npos) {
                      sentence += "\r\n"; // add proper CRLF if missing
                    } else if (sentence.back() != '\n') {
                      sentence += '\n';
                    }

                    // Feed entire string at once via c_str()
                    for (const char *p = sentence.c_str(); *p; ++p) {
                      tinygps.encode(*p);
                    }

                    if (tinygps.location.isUpdated()) {
                      printf("https://www.google.com/maps?q=%f,%f\n",
                             tinygps.location.lat(), tinygps.location.lng());
                    }

                    cout << "=================================================="
                         << endl;

                    nmea::gga gga(nmea_sentence);

                    // Print UTC time of day (seconds since 12:00AM UTC).
                    std::cout << "UTC: " << std::fixed << std::setprecision(2)
                              << gga.utc.get() << std::endl;

                    // Check if latitude is provided in the message.
                    if (gga.latitude.exists()) {
                      // Print latitude.
                      std::cout << "Latitude: " << std::fixed
                                << std::setprecision(6) << gga.latitude.get()
                                << std::endl;
                    }

                    // Check if longitude is provided in the message.
                    if (gga.longitude.exists()) {
                      // Print longitude.
                      std::cout << "Longitude: " << std::fixed
                                << std::setprecision(6) << gga.longitude.get()
                                << std::endl;
                    }

                    cout << "=================================================="
                         << endl
                         << endl;
                  }

                } catch (const std::exception &e) {
                  std::cerr << "NMEA serial read Error: " << e.what() << endl;
                }
              }
            }
          }
        }
      }
    }

    ioc.stop();

    if (t.joinable()) {
      t.join();
    }

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
