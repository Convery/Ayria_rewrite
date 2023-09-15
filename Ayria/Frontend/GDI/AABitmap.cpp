/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-28
    License: MIT

    Windows bitmaps are based on OS/2, so 24-bit BPP requires BGR and draws bottom-up.
    As windows is very backwards compatible, DIB_PAL_INDICES (2) as a flag should still
    be supported, which would save us <= 512 bytes per palette. As there's no real
    docummentation, we need to research win32kfull.sys:NtGdiSetDIBitsToDeviceInternal
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include "../Rendering.hpp"

#if defined (_WIN32)
namespace Rendering
{
    // Framebuffers can generally be greatly improved by platform specific implementations.
    inline std::pair<Handle_t, std::span<uint8_t>> Createframebuffer(uint16_t Width, uint16_t Height, Colorformat_t Pixelformat)
    {
        const auto ScreenDC = std::unique_ptr<std::remove_pointer<HDC>::type, decltype(&DeleteDC)>(GetDC(nullptr), &DeleteDC);
        const auto Devicecontext = std::unique_ptr<std::remove_pointer<HDC>::type, decltype(&DeleteDC)>(CreateCompatibleDC(ScreenDC.get()), &DeleteDC);

        const auto BPP = getColorwidth(Pixelformat);
        void *Buffer{};

        // We allocate the maximum palette size for compatibility.
        if (BPP > 1 && BPP <= 8)
        {
            const auto Colortablesize = 1 << BPP;
            const auto BMP = alloca(sizeof(BITMAPINFOHEADER) + Colortablesize * sizeof(RGBQUAD));
            std::memset(BMP, 0, sizeof(BITMAPINFOHEADER) + Colortablesize * sizeof(RGBQUAD));

            *(BITMAPINFOHEADER *)BMP = BITMAPINFOHEADER{ sizeof(BITMAPINFOHEADER), Width, -(Height), 1, WORD(BPP) };
            const auto DIB = CreateDIBSection(Devicecontext.get(), (BITMAPINFO * const)BMP, DIB_RGB_COLORS, &Buffer, nullptr, 0);
            assert(DIB && Buffer);

            // Usually unnecessary on modern systems..
            GdiFlush();

            return { DIB, { (uint8_t *)Buffer, size_t(Width * Height * BPP / 8) } };
        }
        else
        {
            const auto Masks = getColormasks(Pixelformat);

            // Bitfields are not supported for 24-BPP bitmaps in Windows.
            const auto BMP = BITMAPV4HEADER{ sizeof(BITMAPV4HEADER), Width, -(Height), 1, WORD(BPP), DWORD(BI_BITFIELDS * (BPP > 1 && BPP != 24)), 0, 0, 0, 0, 0, Masks[0], Masks[1], Masks[2], Masks[3] };
            const auto DIB = CreateDIBSection(Devicecontext.get(), (BITMAPINFO * const)&BMP, DIB_RGB_COLORS, &Buffer, nullptr, 0);
            assert(DIB && Buffer);

            // Usually unnecessary on modern systems..
            GdiFlush();

            return { DIB, { (uint8_t *)Buffer, size_t(Width * Height * BPP / 8) } };
        }
    }

    // As we already export the bitmap creation, we mainly just do parsing here.
    struct GDIBitmap_t final : Realizedbitmap_t
    {
        // std::shared_ptr<HDC> Platformhandle{};

        // Internal storage if needed.
        std::unique_ptr<RGBQUAD[]> Palettestorage{};

        // Helpers.
        bool isPalette() const
        {
            return Pixelformat == Colorformat_t::PALETTE4 || Pixelformat == Colorformat_t::PALETTE8;
        }

        // Renderer-defined operations, assumes Bitmapheader_t::Paletteformat
        void Animatepalette(std::span<uint32_t> Newpalette)
        {
            ASSERT(Platformhandle && isPalette());
            sPalette = Newpalette;

            const auto Context = HDC(Platformhandle.get());
            SetDIBColorTable(Context, 0, UINT(sPalette.size()), (RGBQUAD *)sPalette.data());
        }
        void Animatepalette(uint8_t Rotationoffset)
        {
            ASSERT(Platformhandle && isPalette());
            std::ranges::rotate(sPalette, sPalette.begin() + Rotationoffset);

            const auto Context = HDC(Platformhandle.get());
            SetDIBColorTable(Context, 0, UINT(sPalette.size()), (RGBQUAD *)sPalette.data());
        }
        void Reinitializepalette()
        {
            ASSERT(Platformhandle && isPalette());

            const auto Context = HDC(Platformhandle.get());
            SetDIBColorTable(Context, 0, UINT(sPalette.size()), (RGBQUAD *)sPalette.data());
        }

        GDIBitmap_t(const Bitmapheader_t &Info, const std::shared_ptr<void> &Handle) noexcept : Realizedbitmap_t(Info, Handle) {}
        GDIBitmap_t(const Palettebitmap_t *Bitmap) noexcept : Realizedbitmap_t(Bitmap)
        {
            const auto [DIB, pBuffer] = Createframebuffer(Width, Height, Palettecount ? (Palettecount > 16 ? Colorformat_t::PALETTE8 : Colorformat_t::PALETTE4) : Colorformat_t(Pixelformat) );
            const auto Pixels = Bitmap->getPixels();
            const auto BPP = Bitmap->getBPP();
            sPixels = pBuffer;

            // Massage the palette to native format.
            if (Palettecount)
            {
                if (Paletteformat != Colorformat_t::B8G8R8A8)
                {
                    Palettestorage = std::make_unique<RGBQUAD[]>(Palettecount);
                    const auto PaletteBPP = getColorwidth(Paletteformat);
                    const auto Shifts = getColorshifts(Paletteformat);
                    const auto Masks = getColormasks(Paletteformat);
                    const auto Palette = Bitmap->getPalette();

                    // Compiler should hoist the BPP check out.
                    for (size_t i = 0; i < Palettecount; ++i)
                    {
                        if (PaletteBPP == 32)
                        {
                            const auto Entry = ((uint32_t *)Palette)[i];
                            Palettestorage[i] =
                            {
                                BYTE((Entry & Masks[2]) >> Shifts[2]),
                                BYTE((Entry & Masks[1]) >> Shifts[1]),
                                BYTE((Entry & Masks[0]) >> Shifts[0])
                            };

                            continue;
                        }

                        if (PaletteBPP == 24)
                        {
                            const auto Triple = ((RGBTRIPLE *)Palette)[i];

                            // Should optimize to a no-op.
                            const uint32_t Entry = (Triple.rgbtBlue) | (Triple.rgbtGreen << 8) | (Triple.rgbtRed << 16);
                            Palettestorage[i] =
                            {
                                BYTE((Entry & Masks[2]) >> Shifts[2]),
                                BYTE((Entry & Masks[1]) >> Shifts[1]),
                                BYTE((Entry & Masks[0]) >> Shifts[0])
                            };

                            continue;
                        }

                        if (PaletteBPP == 16)
                        {
                            const auto Entry = ((uint16_t *)Palette)[i];
                            Palettestorage[i] =
                            {
                                BYTE((Entry & Masks[2]) >> Shifts[2]),
                                BYTE((Entry & Masks[1]) >> Shifts[1]),
                                BYTE((Entry & Masks[0]) >> Shifts[0])
                            };

                            continue;
                        }

                        std::unreachable();
                    }

                    sPalette = { (uint32_t *)Bitmap->getPalette(), Palettecount };
                }
                else
                {
                    sPalette = { (uint32_t *)Palettestorage.get(), Palettecount };
                }
            }

            // As 24-BPP doesn't support masks, we need to convert to native format.
            if (BPP == 24 && Pixelformat != Colorformat_t::B8G8R8)
            {
                const auto Destination = (RGBTRIPLE *)pBuffer.data();
                const auto Source = (RGBTRIPLE *)Pixels;

                for (size_t i = 0; i < (Width * Height); ++i)
                {
                    auto Temp = Source[i];
                    std::swap(Temp.rgbtRed, Temp.rgbtBlue);
                    Destination[i] = Temp;
                }
            }
            else
            {
                std::memcpy(pBuffer.data(), Pixels, Width * Height * 8 / BPP);
            }

            // Create a context with the same properties as the display.
            auto Devicecontext = CreateCompatibleDC(nullptr);
            SelectObject(Devicecontext, DIB);

            // The framebuffer was null-initialized, so we need to update it.
            if (Palettecount) SetDIBColorTable(Devicecontext, 0, UINT(sPalette.size()), (RGBQUAD *)sPalette.data());

            // GDI is refcounted so deletes are deferred while the context exists.
            DeleteObject(DIB);

            // Clean-up automatically when destroyed.
            Platformhandle = std::shared_ptr<void>(Devicecontext, [](HDC DC) { DeleteDC(DC); });
        }
        GDIBitmap_t(std::string_view Filepath) noexcept : Realizedbitmap_t(Filepath)
        {
            // TODO(tcn): See if we can get away with not implementing this.
            assert(false);
        }
        GDIBitmap_t(GDIBitmap_t &&Other) noexcept : Realizedbitmap_t(Other)
        {
            Palettestorage = std::move(Other.Palettestorage);
            Other.Palettestorage.reset();
        }
        GDIBitmap_t(QOIBitmap_t *Bitmap) noexcept : Realizedbitmap_t(Bitmap)
        {
            const auto [DIB, pBuffer] = Createframebuffer(Width, Height, Colorformat_t(Pixelformat));
            const auto Pixels = Bitmap->getPixels();
            const auto BPP = Bitmap->getBPP();

            // As 24-BPP doesn't support masks, we need to convert to native format.
            if (BPP == 24 && Pixelformat != Colorformat_t::B8G8R8)
            {
                const auto Destination = (RGBTRIPLE *)pBuffer.data();
                const auto Source = (RGBTRIPLE *)Pixels;

                for (size_t i = 0; i < (Width * Height); ++i)
                {
                    auto Temp = Source[i];
                    std::swap(Temp.rgbtRed, Temp.rgbtBlue);
                    Destination[i] = Temp;
                }
            }
            else
            {
                std::memcpy(pBuffer.data(), Pixels, Width * Height * 8 / BPP);
            }

            // Create a context with the same properties as the display.
            const auto Devicecontext = CreateCompatibleDC(nullptr);
            SelectObject(Devicecontext, DIB);

            // GDI is refcounted so deletes are deferred while the context exists.
            DeleteObject(DIB);

            // Clean-up automatically when destroyed.
            Platformhandle = std::shared_ptr<void>(Devicecontext, [](HDC DC) { DeleteDC(DC); });
        }
        GDIBitmap_t() = default;

        // Should have been implicitly deleted, but for the future.
        GDIBitmap_t(const GDIBitmap_t &Other) = delete;
    };

    // Wrappers.
    inline std::unique_ptr<Realizedbitmap_t> Realize(const Bitmapheader_t &Info, const std::shared_ptr<void> &Handle)
    {
        return std::make_unique<GDIBitmap_t>(Info, Handle);
    }
    inline std::unique_ptr<Realizedbitmap_t> Realize(const Palettebitmap_t *Bitmap)
    {
        return std::make_unique<GDIBitmap_t>(Bitmap);
    }
    inline std::unique_ptr<Realizedbitmap_t> Realize(std::string_view Filepath)
    {
        return std::make_unique<GDIBitmap_t>(Filepath);
    }
    inline std::unique_ptr<Realizedbitmap_t> Realize(QOIBitmap_t *Bitmap)
    {
        return std::make_unique<GDIBitmap_t>(Bitmap);
    }

    // For when we don't want to embed anything, result resolution being Steps * Height.
    inline std::unique_ptr<Realizedbitmap_t> Creategradient(ARGB_t First, ARGB_t Last, uint16_t Steps, bool isAnimated, uint8_t Smoothfactor, uint16_t Height)
    {
        ASSERT(Steps && !(isAnimated && Steps > 256));

        const ARGB_t Delta{ 0xFF, uint8_t((Last.R - First.R) / (Steps - 1)), uint8_t((Last.G - First.G) / (Steps - 1)), uint8_t((Last.B - First.B) / (Steps - 1)) };
        const auto Pixelformat = isAnimated ? (Steps <= 16 ? Colorformat_t::PALETTE4 : Colorformat_t::PALETTE8) : Colorformat_t::B8G8R8;
        const auto [DIB, pBuffer] = Createframebuffer(Steps, Height, Colorformat_t(Pixelformat));
        const auto Palettecount = isAnimated * Steps;

        GDIBitmap_t Bitmap{};
        Bitmap.Paletteformat = (uint16_t)Colorformat_t::B8G8R8A8 * isAnimated;
        Bitmap.Pixelformat = (uint16_t)Pixelformat;
        Bitmap.Palettecount = Palettecount;
        Bitmap.Width = uint16_t(Steps);
        Bitmap.Height = Height;

        // As each pixel (hopefully) represents a unique color, palettes are only useful for animations.
        if (isAnimated)
        {
            Bitmap.Palettestorage = std::make_unique<RGBQUAD[]>(Palettecount);
            Bitmap.sPalette = { (uint32_t *)Bitmap.Palettestorage.get(), size_t(Palettecount) };

            // Simple lerp.
            if (Smoothfactor == 0)
            {
                for (size_t i = 0; i < Steps; ++i)
                {
                    Bitmap.Palettestorage[i] = {
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

                    Bitmap.Palettestorage[i] = {
                        static_cast<uint8_t>(First.B + Delta.B * i + Delta.B * F),
                        static_cast<uint8_t>(First.G + Delta.G * i + Delta.G * F),
                        static_cast<uint8_t>(First.R + Delta.R * i + Delta.R * F)
                    };
                }
            }

            // GDI does not support 2-bits per pixel, and 1 is pretty useless for a gradient.
            for (uint32_t y = 0; y < Height; ++y)
            {
                const auto Offset = y * Steps;
                for (uint32_t x = 0; x < Steps; ++x)
                {
                    if (std::bit_width(Steps) > 4) pBuffer[Offset + x] = uint8_t(x);
                    else pBuffer[(Offset + x) >> 1] |= uint8_t(x << (4 * !(x & 1)));
                }
            }
        }
        else
        {
            // Simple lerp.
            if (Smoothfactor == 0)
            {
                for (uint32_t y = 0; y < Height; ++y)
                {
                    const auto Offset = y * Steps;
                    for (size_t x = 0; x < Steps; ++x)
                    {
                        ((RGBTRIPLE *)pBuffer.data())[Offset + x] = {
                            static_cast<uint8_t>(First.B + x * Delta.B),
                            static_cast<uint8_t>(First.G + x * Delta.G),
                            static_cast<uint8_t>(First.R + x * Delta.R)
                        };
                    }
                }
            }
            else
            {
                for (uint32_t y = 0; y < Height; ++y)
                {
                    const auto Offset = y * Steps;
                    for (size_t x = 0; x < Steps; ++x)
                    {
                        const auto Normalized = static_cast<float>(x) / static_cast<float>(Steps - 1);
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

                        ((RGBTRIPLE *)pBuffer.data())[Offset + x] = {
                            static_cast<uint8_t>(First.B + Delta.B * x + Delta.B * F),
                            static_cast<uint8_t>(First.G + Delta.G * x + Delta.G * F),
                            static_cast<uint8_t>(First.R + Delta.R * x + Delta.R * F)
                        };
                    }
                }
            }
        }

        // Create a context with the same properties as the display.
        const auto Devicecontext = CreateCompatibleDC(nullptr);
        SelectObject(Devicecontext, DIB);

        // The framebuffer was null-initialized, so we need to update it.
        if (Palettecount) SetDIBColorTable(Devicecontext, 0, Palettecount, Bitmap.Palettestorage.get());

        // GDI is refcounted so deletes are deferred while the context exists.
        DeleteObject(DIB);

        // Clean-up automatically when destroyed.
        Bitmap.Platformhandle = std::shared_ptr<void>(Devicecontext, [](HDC DC) { DeleteDC(DC); });

        return std::make_unique<GDIBitmap_t>(std::move(Bitmap));
    }

    // We generally don't use 32-BPP, so create a mask from an image for transparency.
    inline std::unique_ptr<Realizedbitmap_t> Createmask(const Atlasbitmap_t *Source, ARGB_t Backgroundcolor)
    {
        const auto SourceDC = *std::static_pointer_cast<HDC>(Source->Platformhandle);
        const auto Size = Source->Subset.cd - Source->Subset.ab;
        const auto MaskDC = CreateCompatibleDC(GetDC(nullptr));

        // GDI is refcounted so deletes are deferred while the context exists.
        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
        SelectObject(MaskDC, Mask);
        DeleteObject(Mask);

        // For a 32-BPP bitmap, we need to drop the alpha channel.
        if (Source->getBPP() == 32) [[unlikely]]
        {
            const auto TempBMP = CreateBitmap(Source->Width, Source->Height, 1, 24, nullptr);
            const auto TempDC = CreateCompatibleDC(SourceDC);
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
        Bitmap.Platformhandle = std::shared_ptr<void>(MaskDC, [](HDC DC) { DeleteDC(DC); });
        Bitmap.Pixelformat = (uint16_t)Colorformat_t::MASK;
        Bitmap.Height = Source->Height;
        Bitmap.Width = Source->Width;

        return std::make_unique<GDIBitmap_t>(std::move(Bitmap));
    }
    inline std::unique_ptr<Realizedbitmap_t> Createmask(const Realizedbitmap_t *Source, ARGB_t Backgroundcolor)
    {
        const auto Mask = CreateBitmap(Source->Width, Source->Height, 1, 1, nullptr);
        const auto SourceDC = *std::static_pointer_cast<HDC>(Source->Platformhandle);
        const auto MaskDC = CreateCompatibleDC(GetDC(nullptr));

        // GDI is refcounted so deletes are deferred while the context exists.
        SelectObject(MaskDC, Mask);
        DeleteObject(Mask);

        // For a 32-BPP bitmap, we need to drop the alpha channel.
        if (Source->getBPP() == 32) [[unlikely]]
        {
            const auto TempBMP = CreateBitmap(Source->Width, Source->Height, 1, 24, nullptr);
            const auto TempDC = CreateCompatibleDC(SourceDC);
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
        Bitmap.Platformhandle = std::shared_ptr<void>(MaskDC, [](HDC DC) { DeleteDC(DC); });
        Bitmap.Pixelformat = (uint16_t)Colorformat_t::MASK;
        Bitmap.Height = Source->Height;
        Bitmap.Width = Source->Width;

        return std::make_unique<GDIBitmap_t>(std::move(Bitmap));
    }
}
#endif
