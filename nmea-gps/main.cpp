#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <nmeaparse/nmea.h>

int main() {
  using namespace std;
  using namespace nmea;

  std::cout << "NMEA-GPS" << std::endl;

  const char bytestream[] =
    "$GPGGA,143528.00,1439.42000,N,12101.62000,E,1,09,0.92,48.2,M,48.2,M,,*69\r\n"
    "$GPRMC,143528.00,A,1439.42000,N,12101.62000,E,0.12,87.34,251125,,,A*62\r\n"
    "$GPGSA,A,3,01,03,05,09,12,17,19,23,28,,,,3.2,1.8,2.6*36\r\n"
    "$GPGSV,3,1,12,01,45,120,38,03,35,280,34,05,60,060,42,09,25,310,36*72\r\n"
    "$GPGSV,3,2,12,12,55,180,40,17,30,050,37,19,70,240,44,23,15,090,32*75\r\n"
    "$GPGSV,3,3,12,28,40,320,39,31,20,160,33,32,10,200,30,33,05,340,28*71\r\n"
    "\r\n";

  NMEAParser parser;
  GPSService gps(parser);
  // (optional) Called when a sentence is valid syntax
  parser.onSentence += [](const NMEASentence &nmea) {
    cout << "Received $" << nmea.name << endl;
  };
  // (optional) Called when data is read/changed
  gps.onUpdate += [&gps]() {
    // There are *tons* of GPSFix properties
    if (gps.fix.locked()) {
      cout << " # Position: " << gps.fix.latitude << ", " << gps.fix.longitude
           << endl;
    } else {
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
      parser.readLine(line);
    } catch (NMEAParseError &e) {
      cout << e.message << endl << endl;
    }
  }

  return EXIT_SUCCESS;
}
