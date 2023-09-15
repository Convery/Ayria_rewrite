/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-26
    License: MIT

    We have 3 types of images:
    1. Embedded QOI 24/32-BPP image.
    2. Embedded palette (<= 8-BPP) image.
    3. Realized image, ready for rendering.
*/

#pragma once
#include "AAColor.hpp"
#include <Utilities/Utilities.hpp>
#include <Utilities/Encoding/QOI.hpp>

namespace Rendering
{
    // Placeholder for platform-specific handles and such.
    using Handle_t = void *;

    #pragma pack(push, 1)
    // Common header for bitmaps.
    struct Bitmapheader_t
    {
        uint16_t Width{}, Height{};
        struct
        {
            uint16_t Paletteformat : 4{};   // AAColor::Colorformat_t enum.
            uint16_t Palettecount : 8{};    // != 0 If a palette is used.
            uint16_t Pixelformat : 4{};     // AAColor::Colorformat_t enum.
        };
    };

    // Stored as [Header][Palette][Pixels]
    struct Palettebitmap_t : Bitmapheader_t
    {
        // In-case the system needs padding.
        uint16_t Pixeloffset{};

        constexpr uint8_t getBPP() const
        {
            if (Palettecount)
            {
                if (Palettecount <= 2) [[unlikely]] return 1;
                if (Palettecount <= 4) [[unlikely]] return 2;
                if (Palettecount <= 16) return 4;

                return 8;
            }

            return getColorwidth(Pixelformat);
        }
        constexpr void *getPixels() const
        {
            return (uint8_t *)(this) + sizeof(*this) + Pixeloffset;
        }
        constexpr void *getPalette() const
        {
            return ((uint8_t *)(this) + sizeof(*this));
        }
    };
    #pragma pack(pop)

    // QOI pixel-data follows the header.
    struct QOIBitmap_t : Bitmapheader_t
    {
        std::unique_ptr<Blob_t> Decodedimage{};
        Blob_view_t Encodedimage{};

        QOIBitmap_t(QOI::Header_t *Header, size_t Totalsize)
        {
            Pixelformat = (uint16_t)(Header->Channels == 3 ? Colorformat_t::R8G8B8 : Colorformat_t::R8G8B8A8);
            Encodedimage = { (uint8_t *)Header, Totalsize };
            Height = Header->Height;
            Width = Header->Width;
        }
        QOIBitmap_t() = default;

        uint8_t getBPP() const
        {
            return 24 + ((uint16_t)Colorformat_t::R8G8B8 == Pixelformat) * 8;
        }
        void *getPixels()
        {
            if (!Decodedimage) Decodedimage = std::make_unique<Blob_t>(QOI::Decode(Encodedimage, nullptr));
            return Decodedimage->data();
        }
    };

    // Platform dependent implementation.
    struct Realizedbitmap_t : Bitmapheader_t
    {
        std::variant<std::monostate, QOIBitmap_t *, const Palettebitmap_t *> Parent{};
        std::shared_ptr<void> Platformhandle{};

        // Optional, but come implementations require direct access.
        std::span<uint32_t> sPalette{};
        std::span<uint8_t> sPixels{};

        // Renderer-defined operations, assumes Bitmapheader_t::Paletteformat
        virtual void Animatepalette(std::span<uint32_t> Newpalette) {}
        virtual void Animatepalette(uint8_t Rotationoffset) {}
        virtual void Reinitializepalette() {}

        // Parsing of the input needs to be done in the
        Realizedbitmap_t(const Bitmapheader_t &Info, const std::shared_ptr<void> &Handle) noexcept : Bitmapheader_t(Info), Platformhandle(Handle) {}
        Realizedbitmap_t(const Palettebitmap_t *Bitmap) noexcept : Bitmapheader_t(*Bitmap), Parent(Bitmap) {}
        Realizedbitmap_t(QOIBitmap_t *Bitmap) noexcept : Bitmapheader_t(*Bitmap), Parent(Bitmap) {}
        Realizedbitmap_t(const Realizedbitmap_t &Other) noexcept : Bitmapheader_t(Other)
        {
            Platformhandle = Other.Platformhandle;
            sPalette = Other.sPalette;
            sPixels = Other.sPixels;
        }
        Realizedbitmap_t(Realizedbitmap_t &&Other) noexcept : Bitmapheader_t(Other)
        {
            Platformhandle = std::move(Other.Platformhandle);
            Other.Platformhandle.reset();

            sPalette = Other.sPalette;
            sPixels = Other.sPixels;
            Other.sPalette = {};
            Other.sPixels = {};
        }

        Realizedbitmap_t(std::string_view Filepath) noexcept {}
        virtual ~Realizedbitmap_t() = default;
        Realizedbitmap_t() = default;

        uint8_t getBPP() const
        {
            if (Palettecount)
            {
                if (Palettecount <= 2) [[unlikely]] return 1;
                if (Palettecount <= 4) [[unlikely]] return 2;
                if (Palettecount <= 16) return 4;

                return 8;
            }

            switch ((Colorformat_t)Pixelformat)
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

                case MONOCHROME:
                case BINARY:
                case MASK:
                    return 1;

                default:
                    assert(false);
                    std::unreachable();
            }
        }
    };

    // Texture-atlas's are usually used when multiple images share a single palette.
    struct Atlasbitmap_t : Realizedbitmap_t
    {
        vec4i Subset;

        Atlasbitmap_t(const Realizedbitmap_t &Parent, vec4i Region) : Realizedbitmap_t({ Parent }), Subset(Region) {}
    };

    // Returns the platforms own version of the bitmap.
    extern std::unique_ptr<Realizedbitmap_t> Realize(const Bitmapheader_t &Info, const std::shared_ptr<void> &Handle);
    extern std::unique_ptr<Realizedbitmap_t> Realize(const Palettebitmap_t *Bitmap);
    extern std::unique_ptr<Realizedbitmap_t> Realize(std::string_view Filepath);
    extern std::unique_ptr<Realizedbitmap_t> Realize(QOIBitmap_t *Bitmap);

    // For when we don't want to embed anything, result resolution being Steps x Height.
    extern std::unique_ptr<Realizedbitmap_t> Creategradient(ARGB_t First, ARGB_t Last, uint16_t Steps, bool isAnimated = false, uint8_t Smoothfactor = 0, uint16_t Height = 1);

    // We generally don't use 32-BPP, so create a mask from an image for transparency.
    extern std::unique_ptr<Realizedbitmap_t> Createmask(const Atlasbitmap_t *Source, ARGB_t Backgroundcolor);
    extern std::unique_ptr<Realizedbitmap_t> Createmask(const Realizedbitmap_t *Source, ARGB_t Backgroundcolor);
}
