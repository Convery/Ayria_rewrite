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
    // As we should never use a pure magenta, we can use it for 24-BPP transparency.
    constexpr ARGB_t Clearcolor(0xFF, 0xFF, 0, 0xFF);

    // Forward declaration for what the renderers should support as a texture.
    using Texture_t = std::variant<std::monostate, ARGB_t,
                                   std::reference_wrapper<Atlasbitmap_t>,
                                   std::reference_wrapper<Realizedbitmap_t>>;

    // For compatibility with earlier systems, a font can be referenced by name or handle.
    using Font_t = std::variant<std::monostate, void *, std::string_view>;

    // Renderers provide their own AA strategy or a 2X downsample, coordinates are global.
    template <bool DownscaleAA> struct Interface_t
    {
        // If no surface is provided, the renderer shall create its own to fit the viewport.
        Interface_t(const vec4i &Viewport, void *Surface);

        // Directly rendering to the internal surface (or parent).
        void drawEllipse(vec2i Position, vec2i Size, const Texture_t &Fill);
        void drawLine(vec2i Start, vec2i Stop, uint8_t Linewidth, const Texture_t &Texture);
        void drawRectangle(vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Texture);
        void drawEllipse(vec2i Position, vec2i Size, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline);
        void drawArc(vec2i Position, vec2i Angles, uint8_t Rounding, uint8_t Linewidth, const Texture_t &Texture);
        void drawRectangle(vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline);

        // For some backends; transparency can be slow, so try to provide a background if possible for a BitBlt.
        void drawText(vec2i Position, Font_t Fonthandle, uint8_t Fontsize, std::wstring_view String, const Texture_t &Textcolor, const Texture_t &Background = std::monostate);
        void drawText_centered(vec4i Boundingbox, Font_t Fonthandle, uint8_t Fontsize, std::wstring_view String, const Texture_t &Textcolor, const Texture_t &Background = std::monostate);

        // Takes any contigious array for simplicity.
        template <cmp::Sequential_t Range> requires (std::is_same_v<Range::value_type, vec2i>)
        void drawPath(const Range<vec2i> &Points, uint8_t Linewidth, const Texture_t &Texture);
        template <cmp::Sequential_t Range> requires (std::is_same_v<Range::value_type, vec2i>)
        void drawPolygon(const Range<vec2i> &Points, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline);

        // Images can also be used for drawing gradients and other such background textures.
        void drawImage(vec2i Position, vec2i Size, const Texture_t &Image);
        void drawImage_tiled(vec2i Imagesize, vec4i Destionation, const Texture_t &Image);
        void drawImage_stretched(vec2i Imagesize, vec4i Destionation, const Texture_t &Image);
    };

    // Implemented in a subdirectory.
    extern Interface_t<false> *Createrenderer(const vec4i &Viewport, void *Surface);
    extern Interface_t<true> *CreaterendererAA(const vec4i &Viewport, void *Surface);
}
