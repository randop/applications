#include <iostream>
#include <limits>
#include "half.hpp"

// Function to clamp a float to half-precision range
half::half clampToHalf(float value) {
    const float max_half = std::numeric_limits<half::half>::max();  // Approx. 65504.0f
    const float min_half = -max_half;
    
    if (value > max_half) {
        return half::half(max_half);
    } else if (value < min_half) {
        return half::half(min_half);
    } else {
        return half::half(value);
    }
}

int main() {
    // Basic example from before
    half::half h = 3.14159f;
    std::cout << "Half-precision value: " << static_cast<float>(h) << std::endl;

    // Demonstrate basic arithmetic
    half::half h2 = h * half::half(2.0f);
    std::cout << "Twice the value: " << static_cast<float>(h2) << std::endl;
    
    // Demonstrate overflow: Value exceeds half's max (~65504)
    half::half overflow_h = half::half(1e5f);  // This will become +inf
    std::cout << "Overflow attempt (1e5): " << static_cast<float>(overflow_h) << std::endl;
    std::cout << "Is infinity? " << half::isinf(overflow_h) << std::endl;
    std::cout << "Is finite? " << half::isfinite(overflow_h) << std::endl;
    
    // Negative overflow
    half::half neg_overflow_h = half::half(-1e5f);  // This will become -inf
    std::cout << "Negative overflow (-1e5): " << static_cast<float>(neg_overflow_h) << std::endl;
    std::cout << "Is negative infinity? " << (half::isinf(neg_overflow_h) && static_cast<float>(neg_overflow_h) < 0) << std::endl;
    
    // Fitting/clamping a large value to half range
    float large_val = 70000.0f;
    half::half clamped = clampToHalf(large_val);
    std::cout << "Clamped large value (" << large_val << "): " << static_cast<float>(clamped) << std::endl;
    std::cout << "Is finite after clamp? " << half::isfinite(clamped) << std::endl;
    
    // Arithmetic overflow example: Multiply to overflow
    half::half big_h = half::half(10000.0f);
    half::half result = big_h * half::half(10.0f);  // 100000 -> inf
    std::cout << "Arithmetic overflow (10000 * 10): " << static_cast<float>(result) << std::endl;
    std::cout << "Is infinity? " << half::isinf(result) << std::endl;
    
    return 0;
}
