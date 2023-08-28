/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-28
    License: MIT

    Windows bitmaps are based on OS/2, so 24-bit BPP requires BGR and draws bottom-up.
*/

#include <Utilities/Utilities.hpp>
#include "../Rendering.hpp"

// If there's no other renderer defined, e.g. pure software.
#if defined (_WIN32) && !defined (HAS_RENDERER)
#define HAS_RENDERER

namespace Rendering
{
    // Windows SDK doesn't export this anymore..
    #if !defined (DIB_PAL_INDICES)
    constexpr auto DIB_PAL_INDICES = 2;
    #endif

    // For portability, we store the image as a DIB.
    struct GDIBitmap_t : Realizedbitmap_t
    {
        HPALETTE Palette{};
        HBITMAP DIB{};

        // NOTE(tcn): QOI supports sRGB so we may want to check the channel and convert as needed.
        GDIBitmap_t(const QOIBitmap_t *Bitmap) : Realizedbitmap_t(Bitmap)
        {
            // NOTE(tcn): If the API changes to pass around a context rather than DIB, optimize this.
            const auto Devicecontext = CreateCompatibleDC(nullptr);

            // Bitmapinfo sometimes needs to have bitfields (masks for where to find R, G, B).
            const auto BMP = (BITMAPINFO *)alloca(sizeof(BITMAPINFOHEADER) + 3 * sizeof(RGBQUAD));
            const auto Masks = getColormasks(Colorformat);
            const auto Pixels = Bitmap->getPixels();
            const auto BPP = Bitmap->getBPP();
            void *Buffer{};

            // Bitmasks are ignored for 24-BPP.
            BMP->bmiHeader = BITMAPINFOHEADER{ sizeof(BITMAPINFOHEADER), Width, -(Height), 1, BPP, DWORD(BI_BITFIELDS * (BPP > 8 && BPP != 24)) };
            BMP->bmiColors[0] = std::bit_cast<RGBQUAD>(Masks[0]); BMP->bmiColors[1] = std::bit_cast<RGBQUAD>(Masks[1]); BMP->bmiColors[2] = std::bit_cast<RGBQUAD>(Masks[2]);

            // If <= 8BPP the 'pixels' are indicies into the palette, else it's RGB (masked).
            DIB = CreateDIBSection(Devicecontext, BMP, DIB_PAL_INDICES * (BPP <= 8), &Buffer, NULL, NULL);

            // 24-BPP does not support colormasks, so we'd need to convert to B8G8R8.
            if (BPP == 24 && Colorformat != Colorformat_t::B8G8R8)
            {
                for (uint32_t i = 0; i < (Width * Height); ++i)
                {
                    auto Temp = ((const RGBTRIPLE *)Pixels)[i];
                    std::swap(Temp.rgbtRed, Temp.rgbtBlue);
                    ((RGBTRIPLE *)Buffer)[i] = Temp;
                }
            }
            else
            {
                std::memcpy(Buffer, Pixels, (Width * Height * BPP) / 8);
            }

            DeleteDC(Devicecontext);
        }
        GDIBitmap_t(const Palettebitmap_t *Bitmap) : Realizedbitmap_t(Bitmap)
        {
            // NOTE(tcn): If the API changes to pass around a context rather than DIB, optimize this.
            const auto Devicecontext = CreateCompatibleDC(nullptr);

            // Assume that the user prefers this function over QOI for good reason.
            if (Palettecount)
            {
                const auto LOGPalette = (LOGPALETTE *)alloca(sizeof(PALETTEENTRY) * (Palettecount + 1));
                LOGPalette->palNumEntries = Palettecount;
                LOGPalette->palVersion = 0x300;

                // Windows wants the palette to be RGB(+ Flags)..
                if (Colorformat != Colorformat_t::R8G8B8A8)
                {
                    const auto Masks = getColormasks(Colorformat);
                    const auto Shifts = getColorshifts(Colorformat);

                    // Only interested in RGB, isAnimated doubles as PC_RESERVED.
                    for (size_t i = 0; i < Palettecount; ++i)
                    {
                        const auto Pixel = Bitmap->getPalette()[i];
                        LOGPalette->palPalEntry[i] = { BYTE(Pixel & Masks[0] >> Shifts[0]),
                                                       BYTE(Pixel & Masks[1] >> Shifts[1]),
                                                       BYTE(Pixel & Masks[2] >> Shifts[2]),
                                                       BYTE(isAnimated) };
                    }
                }
                else
                {
                    for (size_t i = 0; i < Palettecount; ++i)
                    {
                        const auto Pixel = (Bitmap->getPalette()[i] & ~0xFF) | uint8_t(isAnimated);
                        LOGPalette->palPalEntry[i] = std::bit_cast<PALETTEENTRY>(Pixel);
                    }
                }

                Palette = CreatePalette(LOGPalette);
            }

            // Bitmapinfo sometimes needs to have bitfields (masks for where to find R, G, B).
            const auto BMP = (BITMAPINFO *)alloca(sizeof(BITMAPINFOHEADER) + 3 * sizeof(RGBQUAD));
            const auto Masks = getColormasks(Colorformat);
            const auto Pixels = Bitmap->getPixels();
            const auto BPP = Bitmap->getBPP();
            void *Buffer{};

            // Bitmasks are ignored for 24-BPP.
            BMP->bmiHeader = BITMAPINFOHEADER{ sizeof(BITMAPINFOHEADER), Width, -(Height), 1, BPP, DWORD(BI_BITFIELDS * (BPP > 8 && BPP != 24)) };
            BMP->bmiColors[0] = std::bit_cast<RGBQUAD>(Masks[0]); BMP->bmiColors[1] = std::bit_cast<RGBQUAD>(Masks[1]); BMP->bmiColors[2] = std::bit_cast<RGBQUAD>(Masks[2]);

            // If <= 8BPP the 'pixels' are indicies into the palette, else it's RGB (masked).
            DIB = CreateDIBSection(Devicecontext, BMP, DIB_PAL_INDICES * (BPP <= 8), &Buffer, NULL, NULL);

            // 24-BPP does not support colormasks, so we'd need to convert to B8G8R8.
            if (BPP == 24 && Colorformat != Colorformat_t::B8G8R8)
            {
                for (uint32_t i = 0; i < (Width * Height); ++i)
                {
                    auto Temp = ((const RGBTRIPLE *)Pixels)[i];
                    std::swap(Temp.rgbtRed, Temp.rgbtBlue);
                    ((RGBTRIPLE *)Buffer)[i] = Temp;
                }
            }
            else
            {
                std::memcpy(Buffer, Pixels, (Width * Height * BPP) / 8);
            }

            DeleteDC(Devicecontext);
        }
        GDIBitmap_t(std::string_view Filepath) : Realizedbitmap_t(Bitmap) {}

        virtual void Animatepalette(int8_t Offset)
        {
            assert(isAnimated);

            // Maximum of 256 * 4 bytes.
            const auto Count = GetPaletteEntries(Palette, 0, 0, nullptr);
            const auto Entries = (PALETTEENTRY *)alloca(sizeof(PALETTEENTRY) * Count);
            GetPaletteEntries(Palette, 0, Count, Entries);

            // Shift the palette and update.
            std::rotate(Entries, Entries + Offset, Entries + Count);
            AnimatePalette(Palette, 0, Count, Entries);
        }
    };

    // Wrappers.
    std::unique_ptr<Realizedbitmap_t> Realize(const Palettebitmap_t *Bitmap)
    {
        return std::make_unique<GDIBitmap_t>(Bitmap);
    }
    std::unique_ptr<Realizedbitmap_t> Realize(const QOIBitmap_t *Bitmap)
    {
        return std::make_unique<GDIBitmap_t>(Bitmap);
    }
    std::unique_ptr<Realizedbitmap_t> Realize(std::string_view Filepath)
    {
        return std::make_unique<GDIBitmap_t>(Filepath);
    }

    // For when we don't want to embed anything, result resolution being Steps * 1.
    std::unique_ptr<Realizedbitmap_t> Creategradient(ARGB_t First, ARGB_t Last, size_t Steps, bool isAnimated, uint8_t Smoothfactor)
    {
        assert(Steps && !(isAnimated && Steps > 256));

        // NOTE(tcn): If the API changes to pass around a context rather than DIB, optimize this.
        const auto Devicecontext = CreateCompatibleDC(nullptr);

        const ARGB_t Delta{ 0xFF, uint8_t((Last.R - First.R) / (Steps - 1)), uint8_t((Last.G - First.G) / (Steps - 1)), uint8_t((Last.B - First.B) / (Steps - 1)) };
        const auto Colorformat = isAnimated ? Colorformat_t::R8G8B8A8 : Colorformat_t::B8G8R8;
        const auto Palettecount = isAnimated * Steps;
        void *Buffer{};

        GDIBitmap_t Bitmap{};
        Bitmap.Palettecount = Steps * isAnimated;
        Bitmap.isAnimated = isAnimated;
        Bitmap.Width = Steps;
        Bitmap.Height = 1;

        // We could provide an option for creating as a non-animated palette, but pretty much all are animated in HHS.
        if (isAnimated)
        {
            const auto BMP = (BITMAPINFO *)alloca(sizeof(BITMAPINFOHEADER) + 3 * sizeof(RGBQUAD));
            BMP->bmiHeader = BITMAPINFOHEADER{ sizeof(BITMAPINFOHEADER), Steps, -1, 1, 4 + 4 * (std::bit_width(Steps) > 4), BI_RGB };
            Bitmap.DIB = CreateDIBSection(Devicecontext, BMP, DIB_PAL_INDICES * isAnimated, &Buffer, NULL, NULL);

            const auto LOGPalette = (LOGPALETTE *)alloca(sizeof(PALETTEENTRY) * (Palettecount + 1));
            LOGPalette->palNumEntries = Palettecount;
            LOGPalette->palVersion = 0x300;

            // Simple lerp.
            if (Smoothfactor == 0)
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    LOGPalette->palPalEntry[i] = {
                        static_cast<uint8_t>(First.R + Index * Delta.R),
                        static_cast<uint8_t>(First.G + Index * Delta.G),
                        static_cast<uint8_t>(First.B + Index * Delta.B),
                        uint8_t(isAnimated)
                    };
                }
            }
            else
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    const auto Normalized = static_cast<float>(Index) / static_cast<float>(Steps - 1);
                    const auto X = Normalized * 2.0f - 1.0f;
                    const auto F = [X, Order]() -> float
                    {
                        if (Order == 1) return Blend::Smoothstep<1>(X);
                        if (Order == 2) return Blend::Smoothstep<2>(X);
                        if (Order == 3) return Blend::Smoothstep<3>(X);
                        if (Order == 4) return Blend::Smoothstep<4>(X);
                        if (Order == 5) return Blend::Smoothstep<5>(X);
                        if (Order == 6) return Blend::Smoothstep<6>(X);

                        std::unreachable();
                    }();

                    LOGPalette->palPalEntry[i] = {
                        static_cast<uint8_t>(First.R + Delta.R * Index + Delta.R * F),
                        static_cast<uint8_t>(First.G + Delta.G * Index + Delta.G * F),
                        static_cast<uint8_t>(First.B + Delta.B * Index + Delta.B * F),
                        uint8_t(isAnimated)
                    };
                }
            }

            // Allocate the palette in system memory.
            Bitmap.Palette = CreatePalette(LOGPalette);

            // GDI does not support 2-bits per pixel, and 1 is pretty useless for a gradient.
            for (uint32_t i = 0; i < Steps; ++i)
            {
                if (std::bit_width(Steps) > 4) ((uint8_t *)Buffer)[i] = uint8_t(i);
                else ((uint8_t *)Buffer)[i / 2] |= uint8_t(i << (4 * !(i & 1)));
            }
        }
        else
        {
            const auto BMP = (BITMAPINFO *)alloca(sizeof(BITMAPINFOHEADER) + 3 * sizeof(RGBQUAD));
            BMP->bmiHeader = BITMAPINFOHEADER{ sizeof(BITMAPINFOHEADER), Steps, -1, 1, 24, BI_RGB };
            Bitmap.DIB = CreateDIBSection(Devicecontext, BMP, DIB_RGB_COLORS, &Buffer, NULL, NULL);

            // Simple lerp.
            if (Smoothfactor == 0)
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    ((RGBTRIPLE *)Buffer)[i] = {
                        static_cast<uint8_t>(First.B + Index * Delta.B),
                        static_cast<uint8_t>(First.G + Index * Delta.G),
                        static_cast<uint8_t>(First.R + Index * Delta.R)
                    };
                }
            }
            else
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    const auto Normalized = static_cast<float>(Index) / static_cast<float>(Steps - 1);
                    const auto X = Normalized * 2.0f - 1.0f;
                    const auto F = [X, Order]() -> float
                    {
                        if (Order == 1) return Blend::Smoothstep<1>(X);
                        if (Order == 2) return Blend::Smoothstep<2>(X);
                        if (Order == 3) return Blend::Smoothstep<3>(X);
                        if (Order == 4) return Blend::Smoothstep<4>(X);
                        if (Order == 5) return Blend::Smoothstep<5>(X);
                        if (Order == 6) return Blend::Smoothstep<6>(X);

                        std::unreachable();
                    }();

                    ((RGBTRIPLE *)Buffer)[i] = {
                        static_cast<uint8_t>(First.B + Delta.B * Index + Delta.B * F),
                        static_cast<uint8_t>(First.G + Delta.G * Index + Delta.G * F),
                        static_cast<uint8_t>(First.R + Delta.R * Index + Delta.R * F)
                    };
                }
            }
        }

        DeleteDC(Devicecontext);
        return std::make_unique(Bitmap);
    }

    // We generally don't use 32-BPP, so create a mask from an image for transparency.
    // std::unique_ptr<Realizedbitmap_t> Createmask(const Atlasbitmap_t *Source, ARGB_t Backgroundcolor);
    // std::unique_ptr<Realizedbitmap_t> Createmask(const Realizedbitmap_t *Source, ARGB_t Backgroundcolor);
}

#endif
