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
        "$GPGGA,092725.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"  // Valid: XOR=71='G'
        "$GPRMC,092725.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n" // Valid: XOR=106='j'
        "$GPGSA,A,3,04,05,,09,12,,,24,25,,30,,3.6,2.1,2.0*3C\r\n"                   // Valid: XOR=60='<'
        "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,40*75\r\n"   // Valid: XOR=117='u'
        "$GPGSV,2,2,08,16,19,256,25,22,11,301,19,29,08,080,00,30,05,180,00*71\r\n"   // Valid: XOR=113='q'
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"      // Valid: XOR=71='G'
        "$GPRMC,123519,A,4807.038,N,01131.000,E,041.0,090.0,251194,003.1,W*6B\r\n"    // Valid: XOR=107='k'
        "\r\n";  // Trailing empty line (parser ignores)

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
  // Send in a log file or a byte stream
  try {
    parser.readBuffer((uint8_t *)bytestream, sizeof(bytestream));
  } catch (NMEAParseError &err) {
    std::cerr << "Error: " << err.message << std::endl;
  }

  string line;
	ifstream file("nmea_log.txt");
	while (getline(file, line)){
		try {
			parser.readLine(line);
		}
		catch (NMEAParseError& e){
			cout << e.message << endl << endl;
			// You can keep feeding data to the gps service...
			// The previous data is ignored and the parser is reset.
		}
	}

  return EXIT_SUCCESS;
}
