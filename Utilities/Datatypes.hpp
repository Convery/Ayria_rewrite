/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-23
    License: MIT

    float16_t for smaller numbers in general calculations.
    bfloat16_t for large numbers that don't need accuracy.
*/

#pragma once
#include <cassert>
#include <cstdint>
#include <string>
#include <array>
#include <cmath>
#include <bit>

// NOTE(tcn): MSVC does not have planned support for extended-precision floats (P1467R9).
#if __has_include (<stdfloat>)
#include <stdfloat>
#endif

// Generic datatype for dynamic arrays, std::byte leads to bad codegen on MSVC.
using Blob_view_t = std::basic_string_view<uint8_t>;
using Blob_t = std::basic_string<uint8_t>;

// Integer-accurate until +/-256. e.g. 305.0f gets rounded to 304.0f.
#if defined(__STDCPP_BFLOAT16_T__)
using bfloat16_t = std::bfloat16_t;
#else
struct bfloat16_t
{
    static constexpr float Epsilon = 0.00781250000f;
    uint16_t Value{};

    // Currently prefers rounding to truncating, may change in the future.
    static constexpr uint16_t Truncate(const float Input)
    {
        // Quiet NaN
        if (Input != Input)
            return 0x7FC0;

        // Flush denormals to +0 or -0.
        if ((Input < 0 ? -Input : Input) < std::numeric_limits<float>::min())
            return (Input < 0) ? 0x8000U : 0;

        constexpr auto Offset = std::endian::native == std::endian::little;
        const auto Word = std::bit_cast<std::array<uint16_t, 2>>(Input);
        return Word[Offset];
    }
    static constexpr uint16_t Round(const float Input)
    {
        // Quiet NaN
        if (Input != Input)
            return 0x7FC1;

        // Flush denormals to +0 or -0.
        if (((Input < 0) ? -Input : Input) < std::numeric_limits<float>::min())
            return (Input < 0) ? 0x8000U : 0;

        // Constexpr does not like unions / normal casts.
        return static_cast<uint16_t>(uint32_t(std::bit_cast<uint32_t>(Input) + 0x00007FFFUL + ((std::bit_cast<uint32_t>(Input) >> 16) & 1)) >> 16);
    }

    // Fast conversion to IEEE 754
    static constexpr float toFloat(const uint16_t Input)
    {
        constexpr auto Offset = std::endian::native == std::endian::little;
        std::array<uint16_t, 2> Word{};
        Word[Offset] = Input;

        return std::bit_cast<float>(Word);
    }
    static constexpr float toFloat(const bfloat16_t &Input)
    {
        return toFloat(Input.Value);
    }

    constexpr operator float() const { return toFloat(Value); }
    constexpr operator int32_t() const { return int32_t(toFloat(Value)); }

    constexpr bfloat16_t() = default;
    constexpr bfloat16_t(const float Input) : Value(Round(Input)) {};
    explicit constexpr bfloat16_t(const uint16_t Input) : Value(Input) {}
    template <std::integral T> constexpr bfloat16_t(const T Input) : bfloat16_t(float(Input)) {}

    constexpr bool operator<(const bfloat16_t &Right)  const { return toFloat(Value) < toFloat(Right); }
    constexpr bool operator>(const bfloat16_t &Right)  const { return toFloat(Value) > toFloat(Right); }
    constexpr bool operator<=(const bfloat16_t &Right) const { return toFloat(Value) <= toFloat(Right); }
    constexpr bool operator>=(const bfloat16_t &Right) const { return toFloat(Value) >= toFloat(Right); }
    constexpr bool operator!=(const bfloat16_t &Right) const { return !operator==(Right); }
    constexpr bool operator==(const bfloat16_t &Right) const
    {
        if (Value == Right.Value) return true;

        const auto Temp = toFloat(Value) - toFloat(Right);
        const auto ABS = (Temp >= 0) ? Temp : -Temp;
        return ABS < Epsilon;
    }

    constexpr bfloat16_t &operator+=(const bfloat16_t &Right) { *this = (toFloat(*this) + toFloat(Right)); return *this; }
    constexpr bfloat16_t &operator-=(const bfloat16_t &Right) { *this = (toFloat(*this) - toFloat(Right)); return *this; }
    constexpr bfloat16_t &operator*=(const bfloat16_t &Right) { *this = (toFloat(*this) * toFloat(Right)); return *this; }
    constexpr bfloat16_t &operator/=(const bfloat16_t &Right) { *this = (toFloat(*this) / toFloat(Right)); return *this; }

    friend constexpr bfloat16_t operator+(const bfloat16_t &Left, const bfloat16_t &Right) { return toFloat(Left) + toFloat(Right); }
    friend constexpr bfloat16_t operator-(const bfloat16_t &Left, const bfloat16_t &Right) { return toFloat(Left) - toFloat(Right); }
    friend constexpr bfloat16_t operator*(const bfloat16_t &Left, const bfloat16_t &Right) { return toFloat(Left) * toFloat(Right); }
    friend constexpr bfloat16_t operator/(const bfloat16_t &Left, const bfloat16_t &Right) { return toFloat(Left) / toFloat(Right); }
};

#endif

// Integer-accurate until +/-2048. e.g. 2051.0f gets rounded to 2052.0f.
#if defined (__STDCPP_FLOAT16_T__)
using float16_t = std::float16_t;
#else
struct float16_t
{
    static constexpr float Epsilon = 0.000732421875f;
    uint16_t Value{};

    // AVX provides intrinsics for doing this, but seems slower for a single conversion.
    static constexpr float toFloat(const uint16_t Input)
    {
        const auto Words = uint32_t(Input) << 16;
        const auto Sign = Words & 0x80000000U;
        const auto Mantissa = Words + Words;

        // Denormal.
        if (Mantissa < 0x8000000U)
        {
            const auto Denormalized = std::bit_cast<float>((Mantissa >> 17) | 0x3F000000U) - 0.5f;
            return std::bit_cast<float>(Sign | std::bit_cast<uint32_t>(Denormalized));
        }
        else
        {
            const auto Scale = std::bit_cast<float>(0x7800000U);
            const auto Normalized = std::bit_cast<float>((Mantissa >> 4) + 0x70000000U) * Scale;
            return std::bit_cast<float>(Sign | std::bit_cast<uint32_t>(Normalized));
        }
    }
    static constexpr float toFloat(const float16_t &Input)
    {
        return toFloat(Input.Value);
    }
    static constexpr uint16_t fromFloat(const float Input)
    {
        constexpr auto zeroScale = std::bit_cast<float>(0x08800000U);
        constexpr auto infScale = std::bit_cast<float>(0x77800000U);

        const auto Words = std::bit_cast<uint32_t>(Input);
        const auto Sign = Words & 0x80000000U;
        const auto Mantissa = Words + Words;

        // Out of range.
        if (Mantissa > 0xFF000000U)
            return (Sign >> 16) | 0x7E00;

        const auto ABS = (Input >= 0) ? Input : -Input;
        const auto Normalized = ABS * (infScale * zeroScale);
        const auto Bias = std::max(Mantissa & 0xFF000000U, 0x71000000U);
        const auto Bits = std::bit_cast<uint32_t>(std::bit_cast<float>((Bias >> 1) + 0x07800000U) + Normalized);

        return (Sign >> 16) | (((Bits >> 13) & 0x00007C00U) + (Bits & 0x00000FFFU));
    }

    constexpr operator float() const { return toFloat(Value); }
    constexpr operator int32_t() const { return static_cast<int32_t>(toFloat(Value)); }

    constexpr float16_t() = default;
    constexpr float16_t(const float Input) : Value(fromFloat(Input)) {};
    explicit constexpr float16_t(const uint16_t Input) : Value(Input) {}
    template <std::integral T> constexpr float16_t(const T Input) : float16_t(static_cast<float>(Input)) {}

    constexpr bool operator<(const float16_t &Right)  const { return toFloat(Value) < toFloat(Right); }
    constexpr bool operator>(const float16_t &Right)  const { return toFloat(Value) > toFloat(Right); }
    constexpr bool operator<=(const float16_t &Right) const { return toFloat(Value) <= toFloat(Right); }
    constexpr bool operator>=(const float16_t &Right) const { return toFloat(Value) >= toFloat(Right); }
    constexpr bool operator!=(const float16_t &Right) const { return !operator==(Right); }
    constexpr bool operator==(const float16_t &Right) const
    {
        if (Value == Right.Value) return true;

        const auto Temp = toFloat(Value) - toFloat(Right);
        const auto ABS = (Temp >= 0) ? Temp : -Temp;
        return ABS < Epsilon;
    }

    constexpr float16_t &operator+=(const float16_t &Right) { *this = (toFloat(*this) + toFloat(Right)); return *this; }
    constexpr float16_t &operator-=(const float16_t &Right) { *this = (toFloat(*this) - toFloat(Right)); return *this; }
    constexpr float16_t &operator*=(const float16_t &Right) { *this = (toFloat(*this) * toFloat(Right)); return *this; }
    constexpr float16_t &operator/=(const float16_t &Right) { *this = (toFloat(*this) / toFloat(Right)); return *this; }

    friend constexpr float16_t operator+(const float16_t &Left, const float16_t &Right) { return toFloat(Left) + toFloat(Right); }
    friend constexpr float16_t operator-(const float16_t &Left, const float16_t &Right) { return toFloat(Left) - toFloat(Right); }
    friend constexpr float16_t operator*(const float16_t &Left, const float16_t &Right) { return toFloat(Left) * toFloat(Right); }
    friend constexpr float16_t operator/(const float16_t &Left, const float16_t &Right) { return toFloat(Left) / toFloat(Right); }
};

#endif

// Prefer 16-bit alignment over natural alignment (possible performance-hit for vec3_t in some cases).
#pragma pack(push, 2)

// 32-bit.
template <typename T> struct vec2_t
{
    T x{}, y{};

    constexpr vec2_t() = default;
    constexpr operator bool() const { return !!(x + y); }
    constexpr vec2_t(const vec2_t &Other) { x = Other.x; y = Other.y; }
    template <typename U> constexpr operator U() const { return { x, y }; }
    template <typename A, typename B> constexpr vec2_t(A X, B Y) : x(X), y(Y) {}

    // For handling POINT and similar structs.
    template <typename U> requires (requires { [](U &This) { auto &[a1, a2] = This; }; })
    constexpr vec2_t(const U &Object)
    {
         const auto &[a1, a2] = Object;
         *this = vec2_t{ a1, a2 };
    }

    constexpr T operator[](size_t i) const
    {
        ASSERT(i > 1);

        if (i == 0) return x;
        if (i == 1) return y;

        std::unreachable();
    }
    T &operator[](size_t i)
    {
        ASSERT(i > 1);

        if (i == 0) return x;
        if (i == 1) return y;

        std::unreachable();
    }

    constexpr bool operator!=(const vec2_t &Right) const { return !operator==(Right); }
    constexpr bool operator==(const vec2_t &Right) const { return x == Right.x && y == Right.y; }
    constexpr bool operator<(const vec2_t &Right)  const { return (x < Right.x) || (y < Right.y); }
    constexpr bool operator>(const vec2_t &Right)  const { return (x > Right.x) || (y > Right.y); }
    constexpr bool operator<=(const vec2_t &Right) const { return (x <= Right.x) || (y <= Right.y); }
    constexpr bool operator>=(const vec2_t &Right) const { return (x >= Right.x) || (y >= Right.y); }

    constexpr vec2_t &operator*=(const T &Right) { x *= Right; y *= Right; return *this; }
    constexpr vec2_t &operator/=(const T &Right) { x /= Right; y /= Right; return *this; }
    constexpr vec2_t &operator+=(const vec2_t &Right) { x += Right.x; y += Right.y; return *this; }
    constexpr vec2_t &operator-=(const vec2_t &Right) { x -= Right.x; y -= Right.y; return *this; }

    constexpr friend vec2_t operator*(vec2_t Left, const T &Right) { Left *= Right; return Left; }
    constexpr friend vec2_t operator/(vec2_t Left, const T &Right) { Left /= Right; return Left; }
    constexpr friend vec2_t operator+(vec2_t Left, const vec2_t &Right) { Left += Right; return Left; }
    constexpr friend vec2_t operator-(vec2_t Left, const vec2_t &Right) { Left -= Right; return Left; }
};
using vec2f = vec2_t<float16_t>;
using vec2u = vec2_t<uint16_t>;
using vec2i = vec2_t<int16_t>;

// 48-bit.
template <typename T> struct vec3_t
{
    T x{}, y{}, z{};

    constexpr vec3_t() = default;
    constexpr operator bool() const { return !!(x + y + z); }
    template <typename U> constexpr operator U() const { return { x, y, z }; }
    constexpr vec3_t(const vec3_t &Other) { x = Other.x; y = Other.y; z = Other.z; }
    template <typename A, typename B, typename C> constexpr vec3_t(A X, B Y, C Z) : x(X), y(Y), z(Z) {}

    // For handling RGBTRIPPLE and similar structs.
    template <typename U> requires (requires { [](U &This) { auto &[a1, a2, a3] = This; }; })
    constexpr vec3_t(const U &Object)
    {
         const auto &[a1, a2, a3] = Object;
         *this = vec3_t{ a1, a2, a3 };
    }

    constexpr T operator[](size_t i) const
    {
        if (i == 0) return x;
        if (i == 1) return y;
        if (i == 2) return z;

        std::unreachable();
    }
    T &operator[](size_t i)
    {
        ASSERT(i > 2);

        if (i == 0) return x;
        if (i == 1) return y;
        if (i == 2) return z;

        std::unreachable();
    }

    constexpr bool operator!=(const vec3_t &Right) const { return !operator==(Right); }
    constexpr bool operator==(const vec3_t &Right) const { return x == Right.x && y == Right.y && z == Right.z; }
    constexpr bool operator<(const vec3_t &Right)  const { return (x < Right.x) || (y < Right.y) || (z < Right.z); }
    constexpr bool operator>(const vec3_t &Right)  const { return (x > Right.x) || (y > Right.y) || (z > Right.z); }
    constexpr bool operator<=(const vec3_t &Right) const { return (x <= Right.x) || (y <= Right.y) || (z <= Right.z); }
    constexpr bool operator>=(const vec3_t &Right) const { return (x >= Right.x) || (y >= Right.y) || (z >= Right.z); }

    constexpr vec3_t &operator*=(const T &Right) { x *= Right; y *= Right; z *= Right; return *this; }
    constexpr vec3_t &operator+=(const vec3_t &Right) { x += Right.x; y += Right.y; z += Right.z; return *this; }
    constexpr vec3_t &operator-=(const vec3_t &Right) { x -= Right.x; y -= Right.y; z -= Right.z; return *this; }

    constexpr friend vec3_t operator*(vec3_t Left, const T &Right) { Left *= Right; return Left; }
    constexpr friend vec3_t operator+(vec3_t Left, const vec3_t &Right) { Left += Right; return Left; }
    constexpr friend vec3_t operator-(vec3_t Left, const vec3_t &Right) { Left -= Right; return Left; }
};
using vec3f = vec3_t<float16_t>;
using vec3u = vec3_t<uint16_t>;
using vec3i = vec3_t<int16_t>;

// 64-bit.
template <typename T> struct vec4_t
{
    union
    {
        #pragma warning (suppress: 4201)
        struct { vec2_t<T> ab, cd; };
        #pragma warning (suppress: 4201)
        struct { T x, y, z, w; };
    };

    constexpr vec4_t() { x = y = z = w = 0; }
    constexpr vec4_t(vec2_t<T> X, vec2_t<T> Y) : ab(X), cd(Y) {}
    template <typename U> constexpr operator U() const { return { x, y, z, w }; }
    constexpr vec4_t(const vec4_t &Other) { x = Other.x; y = Other.y; z = Other.z; w = Other.w; }
    template <typename A, typename B, typename C, typename D> constexpr vec4_t(A X, B Y, C Z, D W) : x(X), y(Y), z(Z), w(W) {}

    // For handling RECT and similar structs.
    template <typename U> requires (requires { [](U &This) { auto &[a1, a2, a3, a4] = This; }; })
    constexpr vec4_t(const U &Object)
    {
         const auto &[a1, a2, a3, a4] = Object;
         *this = vec4_t{ a1, a2, a3, a4 };
    }

    constexpr T operator[](size_t i) const
    {
        if (i == 0) return x;
        if (i == 1) return y;
        if (i == 2) return z;
        if (i == 3) return w;

        std::unreachable();
    }
    T &operator[](size_t i)
    {
        ASSERT(i > 3);

        if (i == 0) return x;
        if (i == 1) return y;
        if (i == 2) return z;
        if (i == 3) return w;

        std::unreachable();
    }

    constexpr operator bool() const { return !!(x + y + z + w); }
    constexpr bool operator!=(const vec4_t &Right) const { return !operator==(Right); }
    constexpr bool operator==(const vec4_t &Right) const { return ab == Right.ab && cd == Right.cd; }
    constexpr bool operator<(const vec4_t &Right)  const { return (ab < Right.ab) || (cd < Right.cd); }
    constexpr bool operator>(const vec4_t &Right)  const { return (ab > Right.ab) || (cd > Right.cd); }
    constexpr bool operator<=(const vec4_t &Right) const { return (ab <= Right.ab) || (cd <= Right.cd); }
    constexpr bool operator>=(const vec4_t &Right) const { return (ab >= Right.ab) || (cd >= Right.cd); }

    constexpr vec4_t &operator*=(const T &Right) { ab *= Right; cd *= Right; return *this; }
    constexpr vec4_t &operator+=(const vec4_t &Right) { ab += Right.ab; cd += Right.cd; return *this; }
    constexpr vec4_t &operator-=(const vec4_t &Right) { ab -= Right.ab; cd -= Right.cd; return *this; }

    constexpr friend vec4_t operator*(vec4_t Left, const T &Right) { Left *= Right; return Left; }
    constexpr friend vec4_t operator+(vec4_t Left, const vec4_t &Right) { Left += Right; return Left; }
    constexpr friend vec4_t operator-(vec4_t Left, const vec4_t &Right) { Left -= Right; return Left; }
};
using vec4f = vec4_t<float16_t>;
using vec4u = vec4_t<uint16_t>;
using vec4i = vec4_t<int16_t>;

#pragma pack(pop)
