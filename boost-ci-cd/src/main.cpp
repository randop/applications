#include <iostream>
#include <boost/lexical_cast.hpp>

int main() {
    try {
        std::string str = "42";
        int num = boost::lexical_cast<int>(str);
        std::cout << "The number is: " << num << std::endl;
        
        std::string str2 = "hello";
        double dbl = boost::lexical_cast<double>(str2);
    } catch (const boost::bad_lexical_cast& e) {
        std::cout << "Bad cast: " << e.what() << std::endl;
    }
    
    return 0;
}
