# Half-Precision Arithmetic?

Half-precision arithmetic refers to computations using **half-precision floating-point numbers**, a compact binary floating-point format defined in the IEEE 754 standard as **binary16** (or FP16). It uses only 16 bits to represent real numbers, making it much smaller than single-precision (FP32, 32 bits) or double-precision (FP64, 64 bits). This format is particularly useful in scenarios where memory and bandwidth are limited, such as graphics processing, machine learning inference, and embedded systems, but it trades off some accuracy and range for these efficiencies.

Half-precision was introduced to balance performance and precision in hardware like GPUs (e.g., NVIDIA's Tensor Cores support it natively). Arithmetic operations (addition, subtraction, multiplication, division) follow the same principles as in higher-precision formats but are performed with reduced dynamic range and mantissa bits, which can lead to rounding errors or overflows more readily.

### Bit Layout

A half-precision number is structured as follows:

| Bit Position | 15 (MSB) | 14–10     | 9–0       |
|--------------|----------|-----------|-----------|
| **Field**    | Sign     | Exponent  | Mantissa  |
| **Bits**     | 1        | 5         | 10        |
| **Purpose**  | 0 for positive, 1 for negative | Biased exponent (bias = 15) | Fractional part of the significand (implicit leading 1 for normalized numbers) |

- **Sign bit (1 bit)**: Indicates the sign of the number (0 = positive, 1 = negative).
- **Exponent (5 bits)**: Represents the exponent in a biased form. The bias is 15, so the true exponent is the stored value minus 15. This allows exponents from -14 to +15 for normalized numbers.
- **Mantissa (10 bits)**: The fractional part of the significand. For normalized numbers, there's an implicit leading 1, making the effective precision 11 bits (1 + 10).

Special values include:
- **Zero**: All bits 0 (positive) or sign bit 1 with rest 0 (negative zero).
- **Infinity**: Exponent all 1s (31), mantissa all 0s; sign determines ±∞.
- **NaN (Not a Number)**: Exponent all 1s, mantissa non-zero; used for invalid operations like 0/0.

### Range and Precision

- **Approximate range**: ±6.55 × 10⁴ (from about 6.1 × 10⁻⁵ to 65,504).
- **Precision**: About 3–4 decimal digits (due to 10 explicit mantissa bits + implicit 1).
- **Smallest positive normalized number**: 2⁻¹⁴ ≈ 6.1035 × 10⁻⁵.
- **Largest finite number**: (2 - 2⁻¹⁰) × 2¹⁵ ≈ 65,504.

Compared to other formats:

| Format       | Bits | Exponent Bits | Mantissa Bits | Approx. Decimal Digits | Max Value (Positive) |
|--------------|------|---------------|---------------|------------------------|----------------------|
| **Half (FP16)** | 16   | 5             | 10            | 3–4                    | 65,504              |
| **Single (FP32)** | 32 | 8             | 23            | 7–8                    | 3.4 × 10³⁸          |
| **Double (FP64)** | 64 | 11            | 52            | 15–16                  | 1.8 × 10³⁰⁸        |

Half-precision has a much smaller range and lower precision, so values outside its range may overflow to infinity, and small differences may be lost due to rounding.

### How Arithmetic Works

Arithmetic in half-precision mimics IEEE 754 rules for higher precisions but is executed at 16-bit resolution:
1. **Operands alignment**: Numbers are aligned by exponent (shift the mantissa of the smaller one).
2. **Addition/Subtraction**: Add/subtract mantissas; normalize the result (shift to avoid overflow/underflow).
3. **Multiplication**: Multiply mantissas and add exponents (adjusted for bias); normalize.
4. **Division**: Divide mantissas and subtract exponents.
5. **Rounding**: Results are rounded to nearest (ties to even) to fit back into 10 mantissa bits. This can introduce errors, especially in accumulations.

Underflow can produce denormalized numbers (exponent 0, no implicit 1), which have even lower precision. Overflow results in infinity, and invalid ops yield NaN.

In practice, hardware or software (like the `half.hpp` library in C++) emulates these operations. For example, converting a single-precision float to half may lose detail if the value requires more than 3–4 decimal digits.

### Advantages and Limitations

**Advantages**:
- **Efficiency**: Half the memory of FP32; faster computations in parallel workloads (e.g., neural networks).
- **Bandwidth savings**: Ideal for data transfer in AI models.

**Limitations**:
- **Precision loss**: Accumulating errors in loops (e.g., summing many small numbers) can be inaccurate.
- **Range issues**: Large or very small numbers overflow/underflow easily.
- **Not always drop-in replaceable**: Software often converts to/from higher precision for critical computations (mixed-precision training).

For deeper dives, refer to the IEEE 754-2008 standard or libraries like OpenEXR's `half` for implementation details. If you're working with code (e.g., the previous C++ example), test edge cases like `half::half(1e6f)` to see overflow in action!
