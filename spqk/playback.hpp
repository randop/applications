#pragma once

#include <string>
#include <functional>

int process_speech(const std::string& payload,
                   std::function<int(const std::string&)> speak);
