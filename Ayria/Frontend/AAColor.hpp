/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-26
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>

// There's no universally preferred format for colors.
// Web-devs and OpenGL seem to prefer RGBA while DirectX prefer ARGB.
struct ARGB_t
{
    uint8_t A, R, G, B;
    ARGB_t() = default;

    // Colors are usually convertible to uint32_t..
    template <typename T> explicit ARGB_t(uint32_t Packed) noexcept requires (std::is_same_v<std::decay_t<T>, uint32_t>)
    {
        *this = std::bit_cast<ARGB_t>(Packed);
    }
    explicit ARGB_t(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) : A(a), R(r), G(g), B(b){}
    explicit operator uint32_t() const noexcept
    {
        return std::bit_cast<uint32_t>(*this);
    }

    // Prevent implicit conversion.
    #if defined (_WIN32)
    constexpr explicit ARGB_t(COLORREF ABGR) noexcept : ARGB_t(ABGR & 0xFF, (ABGR >> 8) & 0xFF, (ABGR >> 16) & 0xFF, (ABGR >> 24) & 0xFF) { if (!A) A = 0xFF; }
    constexpr operator COLORREF() const noexcept { return R | (G << 8U) | (B << 16U); }
    constexpr operator PALETTEENTRY() const noexcept { return { R, G, B, 0 }; }
    constexpr operator RGBQUAD() const noexcept { return { B, G, R, A }; }
    constexpr operator RGBTRIPLE() const noexcept { return { B, G, R }; }
    #endif
};

// Someone may want sRGB for some reason.
struct sARGB_t
{
    uint8_t A, R, G, B;
    sARGB_t() = default;

    // Acceptable error.
    constexpr auto inv255 = static_cast<float>(1.0 / 255.0);

    explicit sARGB_t(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) noexcept : A(a), R(r), G(g), B(b){}
    explicit sARGB_t(const ARGB_t &Linear) noexcept
    {
        // Normalize.
        const float Normal[3] = {Linear.R * inv255, Linear.G * inv255, Linear.B * inv255};

        A = Linear.A;

        R = (Normal[0] <= 0.0031308f) ? uint8_t(Normal[0] / 12.92f * 255.0f) : uint8_t(cmp::pow((Normal[0] + 0.055f) / 1.055f, 2.4f) * 255.0f);
        G = (Normal[1] <= 0.0031308f) ? uint8_t(Normal[1] / 12.92f * 255.0f) : uint8_t(cmp::pow((Normal[1] + 0.055f) / 1.055f, 2.4f) * 255.0f);
        B = (Normal[2] <= 0.0031308f) ? uint8_t(Normal[2] / 12.92f * 255.0f) : uint8_t(cmp::pow((Normal[2] + 0.055f) / 1.055f, 2.4f) * 255.0f);
    }
    explicit operator ARGB_t() const noexcept
    {
        // Normalize.
        const float Normal[3] = {R * inv255, G * inv255, B * inv255};

        return ARGB_t {
            A,
            (Normal[0] <= 0.04045f) ? uint8_t(Normal[0] / 12.92f * 255.0f) : uint8_t(cmp::pow((Normal[0] + 0.055f) / 1.055f, 2.4f) * 255.0f),
            (Normal[1] <= 0.04045f) ? uint8_t(Normal[1] / 12.92f * 255.0f) : uint8_t(cmp::pow((Normal[1] + 0.055f) / 1.055f, 2.4f) * 255.0f),
            (Normal[2] <= 0.04045f) ? uint8_t(Normal[2] / 12.92f * 255.0f) : uint8_t(cmp::pow((Normal[2] + 0.055f) / 1.055f, 2.4f) * 255.0f)
        };
    }
};

// Need some kind of indentifier..
enum class Colorformat_t : uint8_t
{
    INVALID,

    // 32 BPP
    B8G8R8A8,   // WIN32 RGBQUAD
    R8G8B8A8,   // WIN32 PALETTEENTRY
    A8R8G8B8,
    A8B8G8R8,   // WIN32 COLORREF

    // 24 BPP
    B8G8R8,     // WIN32 RGBTRIPLE
    R8G8B8,

    // 16 BPP
    B5G6R5,
    B5G5R5,
    R5G6B5,
    R5G5B5,

    // 1 BPP has many names..
    MASK, MONOCHROME, BINARY
};

// For getting the channels, e.g. uint8_t Green = (getColormasks()[1] >> getColorshifts()[1]) & 0xFF;
constexpr std::array<uint32_t, 4> getColormasks(Colorformat_t Format)
{
    std::array<uint32_t, 4> RGBAMasks{};
    switch (Format)
    {
        using enum Colorformat_t;

        case B8G8R8A8:
            RGBAMasks[0] = 0x0000FF00;
            RGBAMasks[1] = 0x00FF0000;
            RGBAMasks[2] = 0xFF000000;
            RGBAMasks[3] = 0x000000FF;
            break;
        case R8G8B8A8:
            RGBAMasks[0] = 0xFF000000;
            RGBAMasks[1] = 0x00FF0000;
            RGBAMasks[2] = 0x0000FF00;
            RGBAMasks[3] = 0x000000FF;
            break;
        case A8R8G8B8:
            RGBAMasks[0] = 0x00FF0000;
            RGBAMasks[1] = 0x0000FF00;
            RGBAMasks[2] = 0x000000FF;
            RGBAMasks[3] = 0xFF000000;
            break;
        case A8B8G8R8:
            RGBAMasks[0] = 0x000000FF;
            RGBAMasks[1] = 0x0000FF00;
            RGBAMasks[2] = 0x00FF0000;
            RGBAMasks[3] = 0xFF000000;
            break;
        case B8G8R8:
            RGBAMasks[0] = 0x0000FF;
            RGBAMasks[1] = 0x00FF00;
            RGBAMasks[2] = 0xFF0000;
            break;
        case R8G8B8:
            RGBAMasks[0] = 0xFF0000;
            RGBAMasks[1] = 0x00FF00;
            RGBAMasks[2] = 0x0000FF;
            break;
        case B5G6R5:
            RGBAMasks[0] = 0x001F;
            RGBAMasks[1] = 0x07E0;
            RGBAMasks[2] = 0xF800;
            break;
        case B5G5R5:
            RGBAMasks[0] = 0x001F;
            RGBAMasks[1] = 0x03E0;
            RGBAMasks[2] = 0x7C00;
            break;
        case R5G6B5:
            RGBAMasks[0] = 0xF800;
            RGBAMasks[1] = 0x07E0;
            RGBAMasks[2] = 0x001F;
            break;
        case R5G5B5:
            RGBAMasks[0] = 0x7C00;
            RGBAMasks[1] = 0x03E0;
            RGBAMasks[2] = 0x001F;
            break;
    }
    return RGBAMasks;
};
constexpr std::array<uint32_t, 4> getColorshifts(Colorformat_t Format)
{
    const auto Masks = getColormasks(Format);
    return { uint32_t(std::bit_width(Masks[0]) - 8), uint32_t(std::bit_width(Masks[1]) - 8),
             uint32_t(std::bit_width(Masks[2]) - 8), uint32_t(std::bit_width(Masks[3]) - 8) };
};

// Blending in linear colorspace.
namespace Blend
{
    // Simple smoothstep..
    template <size_t N> float Smoothstep(float Value)
    {
        if constexpr (N == 0) // f(x) = x
            return Value;

        if constexpr (N == 1) // f(x) = 3x^2 - 2x^3
            return Value * Value * (3.0f - 2.0f * Value);

        if constexpr (N == 2) // f(x) = 6x^5 - 15x^4 + 10x^3
            return Value * Value * Value * (3.0f * Value * (2.0f * Value - 5.0f) + 10.0f);

        if constexpr (N == 3) // f(x) = -20x^7 + 70x^6 - 84x^5 + 35x^4
            return Value * Value * Value * Value * (35.0f - 2 * Value * (5.0f * Value * (2.0f * Value - 7.0f) + 42.0f));

        if constexpr (N == 4) // f(x) = 70x^9 - 315x^8 + 540x^7 - 420x^6 + 126x^5
            return Value * Value * Value * Value * Value * (5.0f * Value * (Value *(7.0f * Value * (2.0f * Value - 9.0f) + 108.0f) - 84.0f) + 126.0f);

        if constexpr (N == 5) // f(x) = -252x^11 + 1386x^10 - 3080x^9 + 3465x^8 - 1980x^7 + 462x^6
            return Value * Value * Value * Value * Value * Value * (Value * (-7.0f * Value * (2.0f * Value * (9.0f * Value * (2.0f * Value - 11.0f) + 220.0f) - 495.0f) - 1980.0f) + 462.0f);

        if constexpr (N == 6) // f(x) = 924x^13 - 6006x^12 + 16380x^11 - 24024x^10 + 20020x^9 - 9009x^8 + 1716x^7
            return Value * Value * Value * Value * Value * Value * Value * (7.0f * Value * (2.0f * Value * (3.0f * Value * (Value * (11.0f * Value * (2.0f * Value - 13.0f) + 390.0f) - 572.0f) + 1430.0f) - 1287.0f) + 1716.0f);

        // A bit too smooth..
        std::unreachable();
    }

    // Not exactly blending, but common enough to optimize.
    constexpr ARGB_t Lerp(const ARGB_t &sA, const ARGB_t &sB)
    {
        // Explicit casts.
        const auto A = uint32_t(sA);
        const auto B = uint32_t(sB);

        const uint32_t Alpha = (B & 0xFF000000) >> 24;
        const uint32_t Complement = 0xFF - Alpha;

        const uint32_t RB = ((Complement * (A & 0x00FF00FF)) + (Alpha * (B & 0x00FF00FF))) >> 8;
        const uint32_t AG = (Complement * ((A & 0xFF00FF00) >> 8)) + (Alpha * (0x01000000 | ((B & 0x0000FF00) >> 8)));

        return std::bit_cast<ARGB_t>((RB & 0x00FF00FF) | (AG & 0xFF00FF00));
    }

    // Extracted the core of blending.
    namespace Internal
    {
        constexpr ARGB_t doBlend(const ARGB_t &A, const ARGB_t &B, auto &&Modifier)
        {
            using fARGB_t = struct { float A, R, G, B; };

            const fARGB_t NA = { A.A * inv255, A.R * inv255, A.G * inv255, A.B * inv255 };
            const fARGB_t NB = { B.A * inv255, B.R * inv255, B.G * inv255, B.B * inv255 };
            const auto F = NA.A, G = NB.A;

            const auto Alpha = F + G * (1.0f - G);
            if (Alpha <= 0.0f) return {};

            const auto X = (F * (1.0f - G));
            const auto Z = (1.0f - F) * G;
            const auto Y = F * G;

            return {
                uint8_t(255.0f * Alpha),
                uint8_t(255.0f * ((X * NA.R + Y * Modifier(NB.R, NA.R) + Z * NB.R) / Alpha)),
                uint8_t(255.0f * ((X * NA.G + Y * Modifier(NB.G, NA.G) + Z * NB.G) / Alpha)),
                uint8_t(255.0f * ((X * NA.B + Y * Modifier(NB.B, NA.B) + Z * NB.B) / Alpha))
            };
        }
    }

    // Some misc operations, feel free to add more as needed.
    constexpr ARGB_t Normal(const ARGB_t &A, const ARGB_t &B)
    {
        return Internal::doBlend(A, B, [](float A, float B) -> float
        {
            return std::clamp(B, 0.0f, 1.0f);
        });
    }
    constexpr ARGB_t Multiply(const ARGB_t &A, const ARGB_t &B)
    {
        return Internal::doBlend(A, B, [](float A, float B) -> float
        {
            return std::clamp(A * B, 0.0f, 1.0f);
        });
    }
    constexpr ARGB_t Darken(const ARGB_t &A, const ARGB_t &B)
    {
        return Internal::doBlend(A, B, [](float A, float B) -> float
        {
            return std::clamp(std::min(A, B), 0.0f, 1.0f);
        });
    }
    constexpr ARGB_t Lighten(const ARGB_t &A, const ARGB_t &B)
    {
        return Internal::doBlend(A, B, [](float A, float B) -> float
        {
            return std::clamp(std::max(A, B), 0.0f, 1.0f);
        });
    }
    constexpr ARGB_t Screen(const ARGB_t &A, const ARGB_t &B)
    {
        return Internal::doBlend(A, B, [](float A, float B) -> float
        {
            return std::clamp(A + B - A * B, 0.0f, 1.0f);
        });
    }
    constexpr ARGB_t Overlay(const ARGB_t &A, const ARGB_t &B)
    {
        return Internal::doBlend(A, B, [](float A, float B) -> float
        {
            if (A <= 0.5f) return std::clamp(2 * A * B, 0.0f, 1.0f);
            return std::clamp((2 * A - 1.0f) + B - (2 * A - 1.0f) * B, 0.0f, 1.0f);
        });
    }
}
