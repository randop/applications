// JsonParser.hpp - JSON parsing utilities
#pragma once

#include <string>

namespace uvsvc {

class JsonParser {
public:
    static void parseHttpBinJson(const std::string& json_str);
    static void parseHttpBinIp(const std::string& json_str);
};

} // namespace uvsvc
