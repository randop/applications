#include <boost/json/src.hpp>
#include <iostream>
#include <string>

using namespace boost::json;

int main(int argc, char **argv) {
  // Testing some type confusion issues
  std::string experiment =
      "{\"description\": \"Float (exp)\", \"test\": 1E400}";
  value test = parse(experiment);
  std::string real = serialize(test);
  std::cout << "[!] EXPECTED: " << experiment << std::endl;
  std::cout << "[-] Result: " << real << std::endl;

  /***
   * program output:

  [!] EXPECTED: {"description": "Float (exp)", "test": 1E400}
  [-] Result: {"description":"Float (exp)","test":1e99999}

  ***/
  return 0;
}
