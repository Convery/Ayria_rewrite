/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-26
    License: MIT

    The renderer for HHS made use of deferred rendering to have shared logic
    between the multiple backends. For this public release we've decided to
    go with a more direct approach as we wont have as many elements in the GUI.

    The drawing operations is somewhat simplified, e.g. drawLine() used to provide
    an option for outlining to keep the center transparent, but it was rarely useful
    in practice, so we'll leave it up to the user to implement a workaround if needed.

    As a consequence of moving away from deferred rendering, the Z-position is removed.
    The composer will have to mange the render-order and occlusion checks.
*/

#pragma once
#include "AAColor.hpp"
#include "AABitmap.hpp"
#include <Utilities/Utilities.hpp>

namespace Rendering
{
    // No need to have a separate header for this.
    extern Handle_t getFont(const std::string &Name, uint8_t Size);
    extern void Registerfont(Blob_view_t Fontdata);
    extern Handle_t getDefaultfont(uint8_t Size);

    // As we should never use a pure magenta, we can use it for 24-BPP transparency.
    constexpr ARGB_t Clearcolor(0xFF, 0, 0xFF);

    // Forward declaration for what the renderers should support as a texture.
    using Texture_t = std::variant<std::monostate, ARGB_t,
                                   std::reference_wrapper<Atlasbitmap_t>,
                                   std::reference_wrapper<Realizedbitmap_t>>;

    // Framebuffers can generally be greatly improved by platform-specific implementations.
    extern std::pair<Handle_t, uint8_t *> Createframebuffer(uint16_t Width, uint16_t Height, Colorformat_t Pixelformat);
    extern Handle_t Createbitmap(uint16_t Width, uint16_t Height, Colorformat_t Pixelformat, const uint8_t *Pixeldata);

    // Renderers provide AA using a 2X downsample, coordinates are global.
    struct Interface_t
    {
        // If no surface is provided, the renderer shall create its own to fit the viewport.
        Interface_t(vec4i Viewport, Handle_t Surface) { ASSERT(false); }
        virtual ~Interface_t() = default;
        Interface_t() = default;

        //
        virtual void Present(Handle_t Surface) { ASSERT(false); }

        // Directly rendering to the internal surface (or parent).
        virtual void drawEllipse(vec2i Position, vec2i Size, const Texture_t &Fill) { ASSERT(false); }
        virtual void drawLine(vec2i Start, vec2i Stop, uint8_t Linewidth, const Texture_t &Texture) { ASSERT(false); }
        virtual void drawRectangle(vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Texture) { ASSERT(false); }
        virtual void drawArc(vec2i Centerpoint, vec2i Angles, uint8_t Radius, uint8_t Linewidth, const Texture_t &Texture) { ASSERT(false); }
        virtual void drawEllipse(vec2i Position, vec2i Size, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline) { ASSERT(false); }
        virtual void drawRectangle(vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline) { ASSERT(false); }

        // For some backends; transparency can be slow, so try to provide a background if possible.
        using Textoptions_t = struct { uint16_t Centered : 1, Justified : 1, Leftalign : 1, Rightalign : 1, Multiline : 1; };
        virtual void drawText(vec2i Position, std::wstring_view String, const Texture_t &Stringtexture, uint8_t Fontsize, std::string_view Fontname = {}, const Texture_t &Backgroundtexture = {}, Textoptions_t Options = {}) { ASSERT(false); }
        virtual void drawText(vec4i Boundingbox, std::wstring_view String, const Texture_t &Stringtexture, uint8_t Fontsize, std::string_view Fontname = {}, const Texture_t &Backgroundtexture = {}, Textoptions_t Options = {}) { ASSERT(false); }

        // Takes any contigious array for simplicity.
        virtual void drawPath(std::span<const vec2i> Points, uint8_t Linewidth, const Texture_t &Texture) { ASSERT(false); }
        virtual void drawPolygon(std::span<const vec2i> Points, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline) { ASSERT(false); }

        // Images can also be used for drawing gradients and other such background textures.
        virtual void drawImage_stretched(vec4i Destionation, const Texture_t &Image) { ASSERT(false); }
        virtual void drawImage_tiled(vec4i Destionation, const Texture_t &Image) { ASSERT(false); }
        virtual void drawImage(vec2i Position, const Texture_t &Image) { ASSERT(false); }
    };

    // Implemented in a subdirectory.
    extern std::unique_ptr<Interface_t> Createrenderer(const vec4i &Viewport, Handle_t Surface);
}
