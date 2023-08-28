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
    // QOI pixel-data follows the header.
    struct QOIBitmap_t
    {
        QOI::Header_t *Header{};
        std::unique_ptr<Blob_t> Decodedimage{};

        uint8_t getBPP() const
        {
            return Header->Channels * 8;
        }
        uint8_t *getPixels() const
        {
            if (!Decodedimage)
            {
                const auto Totalsize = QOI::Decodesize(*Header) + sizeof(Header);
                Decodedimage = std::make_unique<Blob_t>(QOI::Decode(Blob_view_t((uint8_t *)Header, Totalsize)));
            }

            return Decodedimage->data();
        }
    };

    // Stored as [Header][Palette][Pixels]
    struct Palettebitmap_t
    {
        uint16_t Width{}, Height{};
        uint16_t Pixeloffset{};
        struct
        {
            uint16_t Palettecount : 8{};    // != 0 If a palette is used.
            uint16_t Colorformat : 4{};     // See AAColor::Colorformat_t enum.
            uint16_t isAnimated : 1{};      // Palette may animate.
            uint16_t RESERVED : 3{};        // Padding.
        };

        constexpr uint8_t getBPP() const
        {
            if (Palettecount)
            {
                if (Palettecount <= 2) return 1;
                if (Palettecount <= 4) return 2;
                if (Palettecount <= 16) return 4;
                if (Palettecount <= 256) return 8;
            }
            switch ((Colorformat_t)Colorformat)
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
            }

            // Probably zero initialized..
            std::unreachable();
        }
        constexpr uint8_t *getPixels() const
        {
            return (uint8_t *)(this) + sizeof(*this) + Pixeloffset;
        }
        constexpr uint32_t *getPalette() const
        {
            return (uint32_t *)((uint8_t *)(this) + sizeof(*this));
        }
    };

    // Platform dependent implementation.
    struct Realizedbitmap_t
    {
        std::variant<std::monostate_t, Palettebitmap_t *, QOIBitmap_t *> Header;
        uint16_t Width{}, Height{};
        struct
        {
            uint16_t Palettecount : 8{};    // != 0 If a palette is used.
            uint16_t Colorformat : 4{};     // See AAColor::Colorformat_t enum.
            uint16_t isAnimated : 1{};      // Palette may animate.
            uint16_t RESERVED : 3{};        // Padding.
        };

        Realizedbitmap_t(const Palettebitmap_t *Bitmap) : Header(Bitmap)
        {
            Width = Header->Width; Height = Header->Height;
            Palettecount = Header->Palettecount;
            Colorformat = Header->Colorformat;
            isAnimated = Header->isAnimated;
            RESERVED = Header->RESERVED;
        }
        Realizedbitmap_t(const QOIBitmap_t *Bitmap) : Header(Bitmap)
        {
            Width = Header->Header->Width;
            Height = Header->Header->Height;
            Colorformat = Header->Header->Channels == 3 ? Colorformat_t::R8G8B8 : Colorformat_t::R8G8B8A8;
        }
        Realizedbitmap_t(std::string_view Filepath) {}
        Realizedbitmap_t() = default;

        virtual void Animatepalette(int8_t Offset) {}
    };
    extern std::unique_ptr<Realizedbitmap_t> Realize(const Palettebitmap_t *Bitmap);
    extern std::unique_ptr<Realizedbitmap_t> Realize(const QOIBitmap_t *Bitmap);
    extern std::unique_ptr<Realizedbitmap_t> Realize(std::string_view Filepath);

    // Platform dependant, but its own class for overloading.
    struct Atlasbitmap_t
    {
        // Questionable lifetime, assume the user knows what they are doing.
        std::reference_wrapper<Realizedbitmap_t> Parent;
        vec4i Region;

        Atlasbitmap_t(const std::unique_ptr<Realizedbitmap_t> &parent, vec4i region) : Parent(*parent), Region(region) {}
    };

    // For when we don't want to embed anything, result resolution being Steps * 1.
    extern std::unique_ptr<Realizedbitmap_t> Creategradient(ARGB_t First, ARGB_t Last, size_t Steps, bool isAnimated = false, uint8_t Smoothfactor = 0);

    // We generally don't use 32-BPP, so create a mask from an image for transparency.
    extern std::unique_ptr<Realizedbitmap_t> Createmask(const Atlasbitmap_t *Source, ARGB_t Backgroundcolor);
    extern std::unique_ptr<Realizedbitmap_t> Createmask(const Realizedbitmap_t *Source, ARGB_t Backgroundcolor);
}
