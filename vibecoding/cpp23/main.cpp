// main.cpp - Barebones C++23 application using std::format
// Compile with: g++ -std=c++23 -O2 main.cpp -o main.bin
// Or: clang++ -std=c++23 -O2 main.cpp -o main.bin

#include <format>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

int main()
{
    std::cout << std::format("Hello from C++{}!\n", 23);

    // Basic formatting
    std::string name = "Alice";
    int age = 30;
    double height = 1.75;

    std::cout << std::format("Name: {}, Age: {}, Height: {:.2f}m\n", 
                             name, age, height);

    // Width, alignment and padding
    std::cout << "\n=== Formatted Table ===\n";
    std::cout << std::format("{:>10} | {:>8} | {:>10}\n", "Product", "Qty", "Price");
    std::cout << std::format("{:-^30}\n", "");

    std::cout << std::format("{:>10} | {:>8} | ${:>9.2f}\n", "Widget", 42, 19.99);
    std::cout << std::format("{:>10} | {:>8} | ${:>9.2f}\n", "Gadget", 137, 5.50);

    // Using format specifiers
    std::cout << "\n=== Advanced Formatting ===\n";
    
    std::cout << std::format("Hex: {:x}, Upper hex: {:X}, Binary: {:b}\n", 255, 255, 42);
    std::cout << std::format("Scientific: {:e}, Fixed: {:.4f}\n", 12345.6789, 12345.6789);
    std::cout << std::format("With thousands separator: {:L}\n", 1'234'567);

    // Formatting chrono
    auto now = std::chrono::system_clock::now();
    std::cout << std::format("\nCurrent time: {:%Y-%m-%d %H:%M:%S}\n", now);

    // Formatting ranges (C++23)
    std::vector<int> numbers = {10, 20, 30, 40, 50};
    std::cout << std::format("\nVector: {}\n", numbers);

    // Runtime format string (the line that was failing)
    std::string fmt = "User {{name: {}, score: {}}}";
    
    // FIX: make_format_args requires non-const lvalue references.
    // Literals like 1337 are rvalues → we create named lvalues.
    std::string runtime_name = "Bob";
    int runtime_score = 1337;
    std::cout << std::vformat(fmt, std::make_format_args(runtime_name, runtime_score)) << "\n";

    std::cout << "\nC++23 std::format demo completed!\n";
    return 0;
}
