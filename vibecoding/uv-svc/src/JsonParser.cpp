// JsonParser.cpp - JSON parsing implementation
#include "uvsvc/JsonParser.hpp"
#include "uvsvc/SafeOutput.hpp"
#include "vendor/ArduinoJson.hpp"

namespace uvsvc {

void JsonParser::parseHttpBinJson(const std::string& json_str) {
    LOG_INFO("[JSON] === Parsing /json response ===");

    ArduinoJson::JsonDocument doc;
    ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, json_str);

    if (error) {
        LOG_ERROR(std::string("[JSON] Parse error: ") + error.c_str());
        return;
    }

    auto slideshow = doc["slideshow"];
    if (!slideshow.isNull()) {
        LOG_INFO("[JSON] Slideshow found:");
        LOG_INFO(std::string("  Author: ") + (slideshow["author"] | "N/A"));
        LOG_INFO(std::string("  Date: ") + (slideshow["date"] | "N/A"));
        LOG_INFO(std::string("  Title: ") + (slideshow["title"] | "N/A"));

        auto slides = slideshow["slides"];
        if (slides.is<ArduinoJson::JsonArray>()) {
            LOG_INFO("  Slides:");
            ArduinoJson::JsonArray slidesArr = slides;
            for (auto slide : slidesArr) {
                LOG_INFO(std::string("    - ") + (slide["title"] | "Untitled"));
                LOG_INFO(std::string("      Type: ") + (slide["type"] | "unknown"));
            }
        }
    }

    LOG_INFO("[JSON] =================================");
}

void JsonParser::parseHttpBinIp(const std::string& json_str) {
    LOG_INFO("[JSON] === Parsing /ip response ===");

    ArduinoJson::JsonDocument doc;
    ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, json_str);

    if (error) {
        LOG_ERROR(std::string("[JSON] Parse error: ") + error.c_str());
        return;
    }

    const char* origin = doc["origin"];
    if (origin) {
        LOG_INFO(std::string("[JSON] Your IP: ") + origin);
    }

    LOG_INFO("[JSON] =================================");
}

} // namespace uvsvc
