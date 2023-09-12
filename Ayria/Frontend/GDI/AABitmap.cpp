/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-28
    License: MIT

    Windows bitmaps are based on OS/2, so 24-bit BPP requires BGR and draws bottom-up.
*/

#include <Utilities/Utilities.hpp>
#include "../Rendering.hpp"

#if defined (_WIN32)
namespace Rendering
{
    // Windows SDK doesn't export this anymore..
    #if !defined (DIB_PAL_INDICES)
    constexpr auto DIB_PAL_INDICES = 2;
    #endif

    // Framebuffers can generally be greatly improved by platform specific implementations.
    std::pair<Handle_t, uint8_t *> Createframebuffer(uint16_t Width, uint16_t Height, Colorformat_t Pixelformat)
    {
        const auto Devicecontext = CreateCompatibleDC(nullptr);
        const auto Masks = getColormasks(Pixelformat);
        void *Buffer{};

        // Deduce the number of bits per pixel.
        const auto BPP = [Pixelformat]()
        {
            switch (Pixelformat)
            {
                using enum Colorformat_t;

                case B8G8R8A8:
                case R8G8B8A8:
                case A8R8G8B8:
                case A8B8G8R8:
                    return 32;

                case B8G8R8:
                case R8G8B8:
                    return 24;

                case B5G6R5:
                case B5G5R5:
                case R5G6B5:
                case R5G5B5:
                    return 16;

                case PALETTE8: return 8;
                case PALETTE4: return 4;

                case MONOCHROME:
                case BINARY:
                case MASK:
                    return 1;

                default:
                    ASSERT(false);
                    return 1;
            }
        }();

        // Bitfields are not supported for 24-BPP bitmaps in Windows.
        const auto BMP = BITMAPV4HEADER{ sizeof(BITMAPV4HEADER), Width, -(Height), 1, WORD(BPP), DWORD(BI_BITFIELDS * (BPP > 8 && BPP != 24)), 0, 0, 0, 0, 0, Masks[0], Masks[1], Masks[2], Masks[3] };
        const auto DIB = CreateDIBSection(Devicecontext, (BITMAPINFO * const)&BMP, DIB_PAL_INDICES * (BPP > 1 && BPP <= 8), &Buffer, nullptr, 0);

        // Usually unnecessary on modern systems..
        GdiFlush();

        DeleteDC(Devicecontext);
        return { DIB, (uint8_t *)Buffer };
    }
    Handle_t Createbitmap(uint16_t Width, uint16_t Height, Colorformat_t Pixelformat, const uint8_t *Pixeldata)
    {
        const auto Devicecontext = GetDC(nullptr);
        const auto Masks = getColormasks(Pixelformat);

        // Deduce the number of bits per pixel.
        const auto BPP = [Pixelformat]()
        {
            switch (Pixelformat)
            {
                using enum Colorformat_t;

                case B8G8R8A8:
                case R8G8B8A8:
                case A8R8G8B8:
                case A8B8G8R8:
                    return 32;

                case B8G8R8:
                case R8G8B8:
                    return 24;

                case B5G6R5:
                case B5G5R5:
                case R5G6B5:
                case R5G5B5:
                    return 16;

                case PALETTE8: return 8;
                case PALETTE4: return 4;

                case MONOCHROME:
                case BINARY:
                case MASK:
                    return 1;

                default:
                    ASSERT(false);
                    return 1;
            }
        }();

        // Bitfields are not supported for 24-BPP bitmaps in Windows.
        const auto BMP = BITMAPV4HEADER{ sizeof(BITMAPV4HEADER), Width, -(Height), 1, WORD(BPP), DWORD(BI_BITFIELDS * (BPP > 8 && BPP != 24)), 0, 0, 0, 0, 0, Masks[0], Masks[1], Masks[2], Masks[3] };

        Handle_t Handle;
        if (!Pixeldata) Handle = CreateDIBSection(Devicecontext, (BITMAPINFO * const)&BMP, DIB_PAL_INDICES * (BPP > 1 && BPP <= 8), nullptr, nullptr, 0);
        else Handle = CreateDIBitmap(Devicecontext, (BITMAPINFOHEADER * const)&BMP, CBM_INIT, Pixeldata, (BITMAPINFO * const)&BMP, DIB_PAL_INDICES * (BPP > 1 && BPP <= 8));

        DeleteDC(Devicecontext);
        return Handle;
    }

    // As we already export the bitmap creation, we mainly just do parsing and palette-animation here.
    struct GDIBitmap_t final : Realizedbitmap_t
    {
        HPALETTE Palette{};

        GDIBitmap_t(const Bitmapheader_t &Info, Handle_t Surface) : Realizedbitmap_t(Info, Surface)
        {
            if (Palettecount) Palette = (HPALETTE)GetCurrentObject(HDC(Surface), OBJ_PAL);
        }
        GDIBitmap_t(const Palettebitmap_t *Bitmap) : Realizedbitmap_t(Bitmap)
        {
            const auto Devicecontext = CreateCompatibleDC(nullptr);
            const auto Pixels = Bitmap->getPixels();
            const auto BPP = Bitmap->getBPP();

            // Initialize the palette as needed.
            if (Palettecount)
            {
                // Over-allocate so that we can re-use the first entry.
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
                        LOGPalette->palPalEntry[i] = { BYTE((Pixel & Masks[0]) >> Shifts[0]),
                                                       BYTE((Pixel & Masks[1]) >> Shifts[1]),
                                                       BYTE((Pixel & Masks[2]) >> Shifts[2]),
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
                SelectPalette(Devicecontext, Palette, FALSE);
            }

            // If the color-format isn't natively supported by GDI..
            if (BPP == 24 && Colorformat != Colorformat_t::B8G8R8)
            {
                const auto Buffer = std::make_unique<RGBTRIPLE[]>(Width * Height);
                const auto Source = (RGBTRIPLE *)Pixels;

                for (size_t i = 0; i < (Width * Height); ++i)
                {
                    auto Temp = Source[i];
                    std::swap(Temp.rgbtRed, Temp.rgbtBlue);
                    Buffer[i] = Temp;
                }

                // GDI is internally refcounted, so deletes are deferred.
                const auto BMP = Createbitmap(Width, Height, Colorformat_t::B8G8R8, (uint8_t *)Buffer.get());
                SelectObject(Devicecontext, BMP);
                DeleteBitmap(BMP);
            }
            else
            {
                Handle_t BMP;

                if (Palettecount && Palettecount <= 16) BMP = Createbitmap(Width, Height, Colorformat_t::PALETTE4, Pixels);
                else if (Palettecount) BMP = Createbitmap(Width, Height, Colorformat_t::PALETTE8, Pixels);
                else BMP = Createbitmap(Width, Height, Colorformat_t(Colorformat), Pixels);

                // GDI is internally refcounted, so deletes are deferred.
                SelectObject(Devicecontext, BMP);
                DeleteBitmap(BMP);
            }

            // Alias the Handle_t.
            Surface = Devicecontext;
        }
        GDIBitmap_t(QOIBitmap_t *Bitmap) : Realizedbitmap_t(Bitmap)
        {
            const auto Devicecontext = CreateCompatibleDC(nullptr);
            const auto Pixels = Bitmap->getPixels();
            const auto BPP = Bitmap->getBPP();

            // If the color-format isn't natively supported by GDI.
            if (BPP == 24 && Colorformat != Colorformat_t::B8G8R8)
            {
                const auto Buffer = std::make_unique<RGBTRIPLE[]>(Width * Height);
                const auto Source = (RGBTRIPLE *)Pixels;

                for (size_t i = 0; i < (Width * Height); ++i)
                {
                    auto Temp = Source[i];
                    std::swap(Temp.rgbtRed, Temp.rgbtBlue);
                    Buffer[i] = Temp;
                }

                // GDI is internally refcounted, so deletes are deferred.
                const auto BMP = Createbitmap(Width, Height, Colorformat_t::B8G8R8, (uint8_t *)Buffer.get());
                SelectObject(Devicecontext, BMP);
                DeleteBitmap(BMP);
            }
            else
            {
                // GDI is internally refcounted, so deletes are deferred.
                const auto BMP = Createbitmap(Width, Height, Colorformat_t(Colorformat), Pixels);
                SelectObject(Devicecontext, BMP);
                DeleteBitmap(BMP);
            }

            // Alias the Handle_t.
            Surface = Devicecontext;
        }
        GDIBitmap_t(std::string_view Filepath)
        {
            // TODO(tcn): See if we can get away without loading anything from disk.
            assert(false);
        }
        GDIBitmap_t() = default;

        // In some context, 'animation' means total replacing, for us it's just a rotation.
        void Animatepalette(int8_t Offset) override
        {
            // NOTE(tcn): We might want to nullsub for !isAnimated instead..
            ASSERT(isAnimated && Palettecount);

            // Maximum of 256 * 4 bytes.
            const auto Entries = (PALETTEENTRY *)alloca(sizeof(PALETTEENTRY) * Palettecount);
            GetPaletteEntries(Palette, 0, Palettecount, Entries);

            // Shift the palette and update.
            std::rotate(Entries, Entries + Offset, Entries + Palettecount);
            AnimatePalette(Palette, 0, Palettecount, Entries);
        }
        void Copypalette(Handle_t Othercontext) override
        {
            if (Palette)
            {
                SelectPalette(HDC(Othercontext), Palette, FALSE);
            }
        }
        ~GDIBitmap_t() override
        {
            // No-op if the values are null.
            DeletePalette(Palette);
            DeleteDC(HDC(Surface));
        }
    };

    // Wrappers.
    std::unique_ptr<Realizedbitmap_t> Realize(const Bitmapheader_t &Info, Handle_t Surface)
    {
        return std::make_unique<GDIBitmap_t>(Info, Surface);
    }
    std::unique_ptr<Realizedbitmap_t> Realize(const Palettebitmap_t *Bitmap)
    {
        return std::make_unique<GDIBitmap_t>(Bitmap);
    }
    std::unique_ptr<Realizedbitmap_t> Realize(QOIBitmap_t *Bitmap)
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
        ASSERT(Steps && !(isAnimated && Steps > 256));

        const ARGB_t Delta{ 0xFF, uint8_t((Last.R - First.R) / (Steps - 1)), uint8_t((Last.G - First.G) / (Steps - 1)), uint8_t((Last.B - First.B) / (Steps - 1)) };
        const auto Colorformat = isAnimated ? (Steps <= 16 ? Colorformat_t::PALETTE4 : Colorformat_t::PALETTE8) : Colorformat_t::B8G8R8;
        const auto Palettecount = isAnimated * Steps;

        GDIBitmap_t Bitmap{};
        Bitmap.Surface = CreateCompatibleDC(nullptr);
        Bitmap.Colorformat = (uint16_t)Colorformat;
        Bitmap.Palettecount = Palettecount;
        Bitmap.isAnimated = isAnimated;
        Bitmap.Width = uint16_t(Steps);
        Bitmap.Height = 1;

        // As each pixel (hopefully) represents a unique color, palettes are only useful for animations.
        if (isAnimated)
        {
            // Over-allocate so that we can re-use the first entry.
            const auto LOGPalette = (LOGPALETTE *)alloca(sizeof(PALETTEENTRY) * (Palettecount + 1));
            LOGPalette->palNumEntries = WORD(Palettecount);
            LOGPalette->palVersion = 0x300;

            // Simple lerp.
            if (Smoothfactor == 0)
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    LOGPalette->palPalEntry[i] = {
                        static_cast<uint8_t>(First.R + i * Delta.R),
                        static_cast<uint8_t>(First.G + i * Delta.G),
                        static_cast<uint8_t>(First.B + i * Delta.B),
                        uint8_t(isAnimated)
                    };
                }
            }
            else
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    const auto Normalized = static_cast<float>(i) / static_cast<float>(Steps - 1);
                    const auto X = Normalized * 2.0f - 1.0f;
                    const auto F = [X, Smoothfactor]() -> float
                    {
                        if (Smoothfactor == 1) return Blend::Smoothstep<1>(X);
                        if (Smoothfactor == 2) return Blend::Smoothstep<2>(X);
                        if (Smoothfactor == 3) return Blend::Smoothstep<3>(X);
                        if (Smoothfactor == 4) return Blend::Smoothstep<4>(X);
                        if (Smoothfactor == 5) return Blend::Smoothstep<5>(X);
                        if (Smoothfactor == 6) return Blend::Smoothstep<6>(X);

                        std::unreachable();
                    }();

                    LOGPalette->palPalEntry[i] = {
                        static_cast<uint8_t>(First.R + Delta.R * i + Delta.R * F),
                        static_cast<uint8_t>(First.G + Delta.G * i + Delta.G * F),
                        static_cast<uint8_t>(First.B + Delta.B * i + Delta.B * F),
                        uint8_t(isAnimated)
                    };
                }
            }

            // Allocate the palette in system memory.
            const auto Palette = CreatePalette(LOGPalette);
            SelectPalette((HDC)Bitmap.Surface, Palette, FALSE);
            Bitmap.Palette = Palette;

            // GDI does not support 2-bits per pixel, and 1 is pretty useless for a gradient.
            const auto Pixelbuffer = std::make_unique<uint8_t[]>(Steps >> uint8_t(Colorformat == Colorformat_t::PALETTE4));
            for (uint32_t i = 0; i < Steps; ++i)
            {
                if (std::bit_width(Steps) > 4) Pixelbuffer[i] = uint8_t(i);
                else Pixelbuffer[i / 2] |= uint8_t(i << (4 * !(i & 1)));
            }

            // GDI is internally refcounted, so deletes are deferred.
            const auto BMP = Createbitmap(uint16_t(Steps), 1, Colorformat, Pixelbuffer.get());
            SelectObject(HDC(Bitmap.Surface), BMP);
            DeleteBitmap(BMP);
        }
        else
        {
            const auto Pixelbuffer = std::make_unique<RGBTRIPLE[]>(Steps);

            // Simple lerp.
            if (Smoothfactor == 0)
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    Pixelbuffer[i] = {
                        static_cast<uint8_t>(First.B + i * Delta.B),
                        static_cast<uint8_t>(First.G + i * Delta.G),
                        static_cast<uint8_t>(First.R + i * Delta.R)
                    };
                }
            }
            else
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    const auto Normalized = static_cast<float>(i) / static_cast<float>(Steps - 1);
                    const auto X = Normalized * 2.0f - 1.0f;
                    const auto F = [X, Smoothfactor]() -> float
                    {
                        if (Smoothfactor == 1) return Blend::Smoothstep<1>(X);
                        if (Smoothfactor == 2) return Blend::Smoothstep<2>(X);
                        if (Smoothfactor == 3) return Blend::Smoothstep<3>(X);
                        if (Smoothfactor == 4) return Blend::Smoothstep<4>(X);
                        if (Smoothfactor == 5) return Blend::Smoothstep<5>(X);
                        if (Smoothfactor == 6) return Blend::Smoothstep<6>(X);

                        std::unreachable();
                    }();

                    Pixelbuffer[i] = {
                        static_cast<uint8_t>(First.B + Delta.B * i + Delta.B * F),
                        static_cast<uint8_t>(First.G + Delta.G * i + Delta.G * F),
                        static_cast<uint8_t>(First.R + Delta.R * i + Delta.R * F)
                    };
                }
            }

            // GDI is internally refcounted, so deletes are deferred.
            const auto BMP = Createbitmap(uint16_t(Steps), 1, Colorformat, (uint8_t *)Pixelbuffer.get());
            SelectObject(HDC(Bitmap.Surface), BMP);
            DeleteBitmap(BMP);
        }

        return std::make_unique<Realizedbitmap_t>(Bitmap);
    }

    // We generally don't use 32-BPP, so create a mask from an image for transparency.
    std::unique_ptr<Realizedbitmap_t> Createmask(const Atlasbitmap_t *Source, ARGB_t Backgroundcolor)
    {
        const auto MaskDC = CreateCompatibleDC((HDC)Source->Surface);
        const auto Size = Source->Subset.cd - Source->Subset.ab;
        const auto SourceDC = (HDC)Source->Surface;

        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
        SelectObject(MaskDC, Mask);

        // For a 32-BPP bitmap, we need to drop the alpha channel.
        if (Source->getBPP() == 32) [[unlikely]]
        {
            const auto TempBMP = CreateBitmap(Size.x, Size.y, 1, 24, nullptr);
            const auto TempDC = CreateCompatibleDC(nullptr);
            SelectObject(TempDC, TempBMP);

            // Reduce the color-space.
            BitBlt(TempDC, 0, 0, Size.x, Size.y, SourceDC, Source->Subset.x, Source->Subset.y, SRCCOPY);

            // Background becomes black, forground white.
            SetBkColor(TempDC, COLORREF(Backgroundcolor));
            SetTextColor(TempDC, RGB(0xFF, 0xFF, 0xFF));

            // Create the mask by reducing to 1-BPP.
            BitBlt(MaskDC, 0, 0, Size.x, Size.y, TempDC, 0, 0, SRCCOPY);

            DeleteObject(TempBMP);
            DeleteDC(TempDC);
        }
        else
        {
            // Background becomes black, forground white.
            const auto BG = SetBkColor(SourceDC, COLORREF(Backgroundcolor));
            const auto FG = SetTextColor(SourceDC, RGB(0xFF, 0xFF, 0xFF));

            // Create the mask by reducing to 1-BPP.
            BitBlt(MaskDC, 0, 0, Size.x, Size.y, SourceDC, Source->Subset.x, Source->Subset.y, SRCCOPY);

            // Restore the colors.
            SetBkColor(SourceDC, BG);
            SetTextColor(SourceDC, FG);
        }

        GDIBitmap_t Bitmap{};
        Bitmap.Colorformat = (uint16_t)Colorformat_t::MASK;
        Bitmap.Surface = MaskDC;
        Bitmap.Height = Size.y;
        Bitmap.Width = Size.x;

        // GDI is refcounted so this delete is deferred.
        DeleteBitmap(Mask);

        return std::make_unique<Realizedbitmap_t>(Bitmap);
    }
    std::unique_ptr<Realizedbitmap_t> Createmask(const Realizedbitmap_t *Source, ARGB_t Backgroundcolor)
    {
        const auto MaskDC = CreateCompatibleDC((HDC)Source->Surface);
        const auto SourceDC = (HDC)Source->Surface;

        const auto Mask = Createbitmap(Source->Width, Source->Height, Colorformat_t::MASK, nullptr);
        SelectObject(MaskDC, Mask);

        // For a 32-BPP bitmap, we need to drop the alpha channel.
        if (Source->getBPP() == 32) [[unlikely]]
        {
            const auto TempBMP = CreateBitmap(Source->Width, Source->Height, 1, 24, nullptr);
            const auto TempDC = CreateCompatibleDC(nullptr);
            SelectObject(TempDC, TempBMP);

            // Reduce the color-space.
            BitBlt(TempDC, 0, 0, Source->Width, Source->Height, SourceDC, 0, 0, SRCCOPY);

            // Background becomes black, forground white.
            SetBkColor(TempDC, COLORREF(Backgroundcolor));
            SetTextColor(TempDC, RGB(0xFF, 0xFF, 0xFF));

            // Create the mask by reducing to 1-BPP.
            BitBlt(MaskDC, 0, 0, Source->Width, Source->Height, TempDC, 0, 0, SRCCOPY);

            DeleteObject(TempBMP);
            DeleteDC(TempDC);
        }
        else
        {
            // Background becomes black, forground white.
            const auto BG = SetBkColor(SourceDC, COLORREF(Backgroundcolor));
            const auto FG = SetTextColor(SourceDC, RGB(0xFF, 0xFF, 0xFF));

            // Create the mask by reducing to 1-BPP.
            BitBlt(MaskDC, 0, 0, Source->Width, Source->Height, SourceDC, 0, 0, SRCCOPY);

            // Restore the colors.
            SetBkColor(SourceDC, BG);
            SetTextColor(SourceDC, FG);
        }

        GDIBitmap_t Bitmap{};
        Bitmap.Colorformat = (uint16_t)Colorformat_t::MASK;
        Bitmap.Height = Source->Height;
        Bitmap.Width = Source->Width;
        Bitmap.Surface = MaskDC;

        // GDI is refcounted so this delete is deferred.
        DeleteBitmap(Mask);

        return std::make_unique<Realizedbitmap_t>(Bitmap);
    }
}
#endif
