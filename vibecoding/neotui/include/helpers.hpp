#pragma once

#include <vector>

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    size_t i = 0;

    while (i < text.size()) {
        char c = text[i];

        if (c == '\r' || c == '\n') {
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
            }

            // Skip the full \r\n sequence
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            ++i;
            continue;
        }

        current += c;
        ++i;
    }

    if (!current.empty()) {
        lines.push_back(current);
    }

    return lines;
}

