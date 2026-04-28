#include <iostream>
#include <optional>
#include <system_error>
#include <string>

using namespace std;

std::pair<std::optional<int>, std::errc> parse_to_int(const std::string& input)
{
    if (input.empty()) {
        return {std::nullopt, std::errc::invalid_argument};
    }

    try {
        int value = std::stoi(input);
        return {value, std::errc{}};
    }
    catch (const std::invalid_argument&) {
        return {std::nullopt, std::errc::invalid_argument};
    }
    catch (const std::out_of_range&) {
        return {std::nullopt, std::errc::result_out_of_range};
    }
}

int main()
{
    cout << "demo" << endl;

    auto test = [](const std::string& s) {
        std::cout << "Input: \"" << s << "\"\n";

        auto [optional, errc] = parse_to_int(s);

        if (errc != std::errc{}) {
            std::error_code ec = std::make_error_code(errc);
            std::cout << "   → ERROR: " << ec.message() << " (code " << ec.value() << ")\n\n";
        }
        else if (optional.has_value()) {
            std::cout << "   → SUCCESS: parsed value = " << *optional << "\n\n";
        }
        else {
            std::cout << "   → UNEXPECTED: both error and optional are empty!\n\n";
        }
    };

    test("42");
    test("-123");
    test("abc");
    test("");
    test("999999999999999999999999");
    test("  100  ");

    cout << "done" << endl;
    return 0;
}
