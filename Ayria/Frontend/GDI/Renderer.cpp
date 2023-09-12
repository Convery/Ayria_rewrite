/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-08-31
    License: MIT

    A standing question when it comes to GDI is how to handle textures.
    One can create a pattern-brush from a bitmap, or draw to a mask
    and then bitblt from the bitmap to the target.
    We employ the latter strategy.

    An alternative would be to use clipping regions, either from
    basic shapes or through GDI.BeginPath()..
    Needs more benchmarking.
*/

#include <Utilities/Utilities.hpp>
#include "../Rendering.hpp"

namespace Rendering
{
    struct GDIBitmap_t;

    // GDI is internally ref-counted so deletions are deferred, but we need to keep track of the selected object.
    struct GDIRAII_t
    {
        HDC Devicecontext;
        HGDIOBJ Object, Previous;

        GDIRAII_t(HDC DC, HGDIOBJ Current) : Devicecontext(DC), Object(Current), Previous(SelectObject(DC, Current)) {}
        ~GDIRAII_t() { SelectObject(Devicecontext, Object); }
        operator HBRUSH() const { return HBRUSH(Object); }
        operator HPEN() const { return HPEN(Object); }
        operator HGDIOBJ() const { return Object; }
    };
    struct DCPenRAII_t : GDIRAII_t
    {
        COLORREF Old;

        DCPenRAII_t(HDC DC, ARGB_t Color, HGDIOBJ Object) : GDIRAII_t(DC, Object)
        {
            Old = SetDCPenColor(DC, COLORREF(Color));
        }
        ~DCPenRAII_t()
        {
            SetDCPenColor(Devicecontext, Old);
        }
    };
    struct BrushRAII_t : GDIRAII_t
    {
        COLORREF Old;

        BrushRAII_t(HDC DC, ARGB_t Color) : GDIRAII_t(DC, GetStockObject(DC_BRUSH))
        {
            Old = SetDCBrushColor(DC, COLORREF(Color));
        }
        ~BrushRAII_t()
        {
            SetDCBrushColor(Devicecontext, Old);
        }
    };
    struct PaletteRAII_t
    {
        HDC Devicecontext{};
        HPALETTE Previous{};

        PaletteRAII_t(HDC DC, HPALETTE Current) : Devicecontext(DC)
        {
            Previous = SelectPalette(DC, Current, FALSE);
            RealizePalette(DC);
        }
        ~PaletteRAII_t()
        {
            SelectPalette(Devicecontext, Previous, FALSE);
            RealizePalette(Devicecontext);
        }
    };

    // Try to re-use as many objects as possible rather than deallocating.
    static GDIRAII_t Stockobject(HDC Devicecontext, int ID)
    {
        return GDIRAII_t{ Devicecontext, GetStockObject(ID) };
    }
    static BrushRAII_t Createbrush(HDC Devicecontext, ARGB_t Color)
    {
        return BrushRAII_t{ Devicecontext, Color };
    }
    static std::variant<GDIRAII_t, DCPenRAII_t> Createpen(HDC Devicecontext, ARGB_t Color, uint8_t Linewidth = 1)
    {
        if (Linewidth == 1) [[likely]] return DCPenRAII_t{ Devicecontext, Color, GetStockObject(DC_PEN) };
        else
        {
            static Hashmap<COLORREF, Hashmap<uint8_t, HGDIOBJ>> Pens{};
            const auto Entry = &Pens[COLORREF(Color)][Linewidth];

            if (*Entry == nullptr) *Entry = CreatePen(PS_SOLID, Linewidth, COLORREF(Color));
            return GDIRAII_t{ Devicecontext, *Entry };
        }
    }
    static GDIRAII_t Createfont(HDC Devicecontext, std::string_view Name, uint8_t Size)
    {
        if (Name.empty())
        {
            return GDIRAII_t{ Devicecontext, getDefaultfont(Size) };
        }

        static Hashmap<std::string_view, Hashmap<uint8_t, HFONT>> Fonts{};
        const auto Entry = &Fonts[Name][Size];

        if (*Entry == nullptr) *Entry = (HFONT)getFont({ Name.data(), Name.size() }, Size);
        return GDIRAII_t{ Devicecontext, *Entry };
    }

    // For somewhat cleaner code.
    bool isColor(const Texture_t &Texture) { return std::holds_alternative<ARGB_t>(Texture); }
    bool isNULL(const Texture_t &Texture) { return std::holds_alternative<std::monostate>(Texture); }
    bool isBitmap(const Texture_t &Texture) { return std::holds_alternative<std::reference_wrapper<Realizedbitmap_t>>(Texture); }

    // NOTE(tcn): Optimization point, need to benchmark on more hardware.
    static void Clear(HDC Device, vec4i Rect)
    {
        constexpr size_t Strategy = 3;
        GdiFlush();

        if constexpr (Strategy == 0)
        {
            static const auto Brush = CreateSolidBrush(COLORREF(Clearcolor));
            const auto Area = (RECT)Rect;

            FillRect(Device, &Area, Brush);
        }

        if constexpr (Strategy == 1)
        {
            const auto Backgroundcolor = SetBkColor(Device, Clearcolor);
            const auto Mode = SetBkMode(Device, OPAQUE);
            const auto Area = (RECT)Rect;

            auto Tmp = ExtTextOutW(Device, 0, 0, ETO_OPAQUE, &Area, nullptr, 0, nullptr);

            SetBkColor(Device, Backgroundcolor);
            SetBkMode(Device, Mode);
        }

        if constexpr (Strategy == 2)
        {
            static const auto Brush = CreateSolidBrush(COLORREF(Clearcolor));
            const auto Old = SelectObject(Device, Brush);

            Rectangle(Device, Rect.x, Rect.y, Rect.z, Rect.w);

            SelectObject(Device, Old);
        }

        if constexpr (Strategy == 3)
        {
            const auto RAII1 = Createbrush(Device, Clearcolor);
            const auto RAII2 = Stockobject(Device, NULL_PEN);

            Rectangle(Device, Rect.x, Rect.y, Rect.z, Rect.w);
        }
    }

    // The actual implementation.
    namespace GDI
    {
        constexpr auto Maskcolor = ARGB_t(0xFF, 0xFF, 0xFF);

        // Calculte the size using current HDC settings, ignores newline.
        inline SIZE Textsize(HDC Devicecontext, std::wstring_view String)
        {
            SIZE Result{};
            GetTextExtentPoint32W(Devicecontext, String.data(), int(String.size()), &Result);
            return Result;
        }

        // For textured operations we create a mask and bitblt the texture, so it's worth extracting it.
        inline void MaskedBlt(HDC Destination, HDC Source, HDC Mask, vec2i Size, vec2i DstOffset, vec2i SrcOffset)
        {
            BitBlt(Destination, DstOffset.x, DstOffset.y, DstOffset.x + Size.x, DstOffset.y + Size.y, Mask, SrcOffset.x, SrcOffset.y, SRCAND);      // AND
            BitBlt(Destination, DstOffset.x, DstOffset.y, DstOffset.x + Size.x, DstOffset.y + Size.y, Source, SrcOffset.x, SrcOffset.y, SRCPAINT);  // OR
        }
        inline void MaskedBlt(HDC Destination, const std::reference_wrapper<Atlasbitmap_t> &Source, HDC Mask, vec2i Size, vec2i Offset = {})
        {
            return MaskedBlt(Destination, HDC(Source.get().Surface), Mask, Size, Offset, Source.get().Subset.ab);
        }
        inline void MaskedBlt(HDC Destination, const std::reference_wrapper<Realizedbitmap_t> &Source, HDC Mask, vec2i Size, vec2i Offset = {})
        {
            return MaskedBlt(Destination, HDC(Source.get().Surface), Mask, Size, Offset, {});
        }
        inline void MaskedBlt(HDC Destination, const std::reference_wrapper<Atlasbitmap_t> &Source, const std::reference_wrapper<Realizedbitmap_t> &Mask, vec2i Size, vec2i Offset = {})
        {
            return MaskedBlt(Destination, HDC(Source.get().Surface), HDC(Mask.get().Surface), Size, Offset, Source.get().Subset.ab);
        }
        inline void MaskedBlt(HDC Destination, const std::reference_wrapper<Realizedbitmap_t> &Source, const std::reference_wrapper<Realizedbitmap_t> &Mask, vec2i Size, vec2i Offset = {})
        {
            return MaskedBlt(Destination, HDC(Source.get().Surface), HDC(Mask.get().Surface), Size, Offset, {});
        }

        // A circle is just a special ellipse..
        inline void drawEllipse_solid(HDC Devicecontext, vec2i Position, vec2i Size, const Texture_t &Fill)
        {
            const auto Dimensions = vec4i{ Position, Position + Size };

            std::visit(cmp::Overload{
                    [](std::monostate) { assert(false); },
                    [&](const ARGB_t &Value)
                    {
                        const auto RAII1 = Createbrush(Devicecontext, Value);
                        const auto RAII2 = Stockobject(Devicecontext, NULL_PEN);
                        Ellipse(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w);
                    },
                    [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        drawEllipse_solid(MaskDC, {}, Size, Maskcolor);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Position);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    },
                    [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        drawEllipse_solid(MaskDC, {}, Size, Maskcolor);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Position);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    }
            }, Fill);
        }
        inline void drawEllipse_outline(HDC Devicecontext, vec2i Position, vec2i Size, uint8_t Linewidth, const Texture_t &Outline)
        {
            const auto Dimensions = vec4i{ Position, Position + Size };

            std::visit(cmp::Overload{
                    [](std::monostate) { assert(false); },
                    [&](const ARGB_t &Value)
                    {
                        const auto RAII1 = Stockobject(Devicecontext, NULL_BRUSH);
                        const auto RAII2 = Createpen(Devicecontext, Value, Linewidth);
                        Ellipse(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w);
                    },
                    [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        drawEllipse_outline(MaskDC, {}, Size, Linewidth, Maskcolor);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Position);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    },
                    [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        drawEllipse_outline(MaskDC, {}, Size, Linewidth, Maskcolor);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Position);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    }
            }, Outline);
        }
        inline void drawEllipse(HDC Devicecontext, vec2i Position, vec2i Size, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline)
        {
            // Literally nothing for us to do here..
            ASSERT(!(std::holds_alternative<std::monostate>(Outline) && std::holds_alternative<std::monostate>(Fill)));

            // Simplest case.
            if (std::holds_alternative<ARGB_t>(Fill) && std::holds_alternative<ARGB_t>(Outline)) [[likely]]
            {
                const auto Dimensions = vec4i{ Position, Position + Size };
                const auto RAII1 = Createbrush(Devicecontext, std::get<ARGB_t>(Fill));
                const auto RAII2 = Createpen(Devicecontext, std::get<ARGB_t>(Outline), Linewidth);

                Ellipse(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w);
                return;
            }

            // Needs to be done as separate ops.
            if (!std::holds_alternative<std::monostate>(Fill))
            {
                drawEllipse_solid(Devicecontext, Position, Size, Fill);
            }
            if (!std::holds_alternative<std::monostate>(Outline))
            {
                drawEllipse_outline(Devicecontext, Position, Size, Linewidth, Outline);
            }
        }

        // Not to be confused with the GDI.BeginPath() commands.
        inline void drawPath(HDC Devicecontext, const std::vector<POINT> &Points, uint8_t Linewidth, const Texture_t &Texture)
        {
            std::visit(cmp::Overload{
                [](std::monostate) { assert(false); },
                [&](const ARGB_t &Value)
                {
                    const auto RAII1 = Stockobject(Devicecontext, NULL_BRUSH);
                    const auto RAII2 = Createpen(Devicecontext, Value, Linewidth);

                    Polyline(Devicecontext, Points.data(), int(Points.size()));
                },
                [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                {
                    // Find the maximum dimensions.
                    POINT Min {INT16_MAX, INT16_MAX }, Max{INT16_MIN, INT16_MIN};
                    for (const auto &Point : Points)
                    {
                        // Branchless min-max.
                        Min.x = cmp::min(Min.x, Point.x);
                        Min.y = cmp::min(Min.y, Point.y);
                        Max.x = cmp::max(Max.x, Point.x);
                        Max.y = cmp::max(Max.y, Point.y);
                    }
                    const POINT Size = { (Max.x - Min.x), (Max.y - Min.y) };

                    const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                    const auto MaskDC = CreateCompatibleDC(Devicecontext);
                    SelectObject(MaskDC, Mask);

                    const auto RAII1 = Stockobject(MaskDC, NULL_BRUSH);
                    const auto RAII2 = Createpen(MaskDC, Maskcolor, Linewidth);
                    SetViewportOrgEx(MaskDC, -(Size.x), -(Size.y), nullptr);

                    Polyline(MaskDC, Points.data(), int(Points.size()));
                    MaskedBlt(Devicecontext, Value, MaskDC, Size, Min);

                    DeleteObject(Mask);
                    DeleteDC(MaskDC);
                },
                [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                {
                    // Find the maximum dimensions.
                    POINT Min {INT16_MAX, INT16_MAX }, Max{INT16_MIN, INT16_MIN};
                    for (const auto &Point : Points)
                    {
                        // Branchless min-max.
                        Min.x = cmp::min(Min.x, Point.x);
                        Min.y = cmp::min(Min.y, Point.y);
                        Max.x = cmp::max(Max.x, Point.x);
                        Max.y = cmp::max(Max.y, Point.y);
                    }
                    const POINT Size = { (Max.x - Min.x), (Max.y - Min.y) };

                    const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                    const auto MaskDC = CreateCompatibleDC(Devicecontext);
                    SelectObject(MaskDC, Mask);

                    const auto RAII1 = Stockobject(MaskDC, NULL_BRUSH);
                    const auto RAII2 = Createpen(MaskDC, Maskcolor, Linewidth);
                    SetViewportOrgEx(MaskDC, -(Size.x), -(Size.y), nullptr);

                    Polyline(MaskDC, Points.data(), int(Points.size()));
                    MaskedBlt(Devicecontext, Value, MaskDC, Size, Min);

                    DeleteObject(Mask);
                    DeleteDC(MaskDC);
                }
            }, Texture);
        }
        inline void drawLine(HDC Devicecontext, vec2i Start, vec2i Stop, uint8_t Linewidth, const Texture_t &Texture)
        {
            // As GDI does internal batching, the overhead here is minimal.
            return drawPath(Devicecontext, { Start, Stop }, Linewidth, Texture);
        }

        // GDI automatically closes the polygon if the endpoint is not the startpoint.
        inline void drawPolygon_solid(HDC Devicecontext, const std::vector<POINT> &Points, const Texture_t &Fill)
        {
            // Find the maximum dimensions.
            POINT Min {INT16_MAX, INT16_MAX }, Max{INT16_MIN, INT16_MIN};
            for (const auto &Point : Points)
            {
                // Branchless min-max.
                Min.x = cmp::min(Min.x, Point.x);
                Min.y = cmp::min(Min.y, Point.y);
                Max.x = cmp::max(Max.x, Point.x);
                Max.y = cmp::max(Max.y, Point.y);
            }
            const POINT Size = { (Max.x - Min.x), (Max.y - Min.y) };

            std::visit(cmp::Overload{
                    [](std::monostate) { assert(false); },
                    [&](const ARGB_t &Value)
                    {
                        const auto RAII1 = Createbrush(Devicecontext, Value);
                        const auto RAII2 = Stockobject(Devicecontext, NULL_PEN);
                        Polygon(Devicecontext, Points.data(), int(Points.size()));
                    },
                    [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        const auto RAII1 = Createbrush(MaskDC, Maskcolor);
                        const auto RAII2 = Stockobject(MaskDC, NULL_PEN);
                        SetViewportOrgEx(MaskDC, -(Size.x), -(Size.y), nullptr);

                        Polygon(MaskDC, Points.data(), int(Points.size()));
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Min);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    },
                    [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        const auto RAII1 = Createbrush(MaskDC, Maskcolor);
                        const auto RAII2 = Stockobject(MaskDC, NULL_PEN);
                        SetViewportOrgEx(MaskDC, -(Size.x), -(Size.y), nullptr);

                        Polygon(MaskDC, Points.data(), int(Points.size()));
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Min);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    }
            }, Fill);
        }
        inline void drawPolygon(HDC Devicecontext, const std::vector<POINT> &Points, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline)
        {
            // Literally nothing for us to do here..
            ASSERT(!(std::holds_alternative<std::monostate>(Outline) && std::holds_alternative<std::monostate>(Fill)));

            // Simplest case.
            if (std::holds_alternative<ARGB_t>(Fill) && std::holds_alternative<ARGB_t>(Outline)) [[likely]]
            {
                const auto RAII1 = Createbrush(Devicecontext, std::get<ARGB_t>(Fill));
                const auto RAII2 = Createpen(Devicecontext, std::get<ARGB_t>(Outline), Linewidth);

                Polygon(Devicecontext, Points.data(), int(Points.size()));
                return;
            }

            // Needs to be done as separate ops.
            if (!std::holds_alternative<std::monostate>(Fill))
            {
                drawPolygon_solid(Devicecontext, Points, Fill);
            }
            if (!std::holds_alternative<std::monostate>(Outline))
            {
                drawPath(Devicecontext, Points, Linewidth, Outline);
            }
        }

        // NOTE(tcn): In graphics-mode GM_ADVANCED the bottomright is inclusive, in GM_COMPATIBLE it's exclusive.
        inline void drawRectangle_solid(HDC Devicecontext, vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Texture)
        {
            std::visit(cmp::Overload{
                [](std::monostate) { assert(false); },
                [&](const ARGB_t &Value)
                {
                    const auto Dimensions = vec4i{ Topleft, Bottomright };
                    const auto RAII1 = Createbrush(Devicecontext, Value);
                    const auto RAII2 = Stockobject(Devicecontext, NULL_PEN);

                    if (Rounding == 0) Rectangle(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w);
                    else RoundRect(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w, Rounding, Rounding);
                },
                [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                {
                    const auto Size = Bottomright - Topleft;

                    const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                    const auto MaskDC = CreateCompatibleDC(Devicecontext);
                    SelectObject(MaskDC, Mask);

                    const auto RAII1 = Createbrush(MaskDC, Maskcolor);
                    const auto RAII2 = Stockobject(MaskDC, NULL_PEN);

                    if (Rounding == 0) Rectangle(MaskDC, 0, 0, Size.x, Size.y);
                    else RoundRect(MaskDC, 0, 0, Size.x, Size.y, Rounding, Rounding);

                    MaskedBlt(Devicecontext, Value, MaskDC, Size, Topleft);

                    DeleteObject(Mask);
                    DeleteDC(MaskDC);
                },
                [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                {
                    const auto Size = Bottomright - Topleft;

                    const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                    const auto MaskDC = CreateCompatibleDC(Devicecontext);
                    SelectObject(MaskDC, Mask);

                    const auto RAII1 = Createbrush(MaskDC, Maskcolor);
                    const auto RAII2 = Stockobject(MaskDC, NULL_PEN);

                    if (Rounding == 0) Rectangle(MaskDC, 0, 0, Size.x, Size.y);
                    else RoundRect(MaskDC, 0, 0, Size.x, Size.y, Rounding, Rounding);

                    MaskedBlt(Devicecontext, Value, MaskDC, Size, Topleft);

                    DeleteObject(Mask);
                    DeleteDC(MaskDC);
                }
            }, Texture);
        }
        inline void drawRectangle_outline(HDC Devicecontext, vec2i Topleft, vec2i Bottomright, uint8_t Rounding, uint8_t Linewidth, const Texture_t &Outline)
        {
            std::visit(cmp::Overload{
                [](std::monostate) { assert(false); },
                [&](const ARGB_t &Value)
                {
                    if (Rounding == 0) drawPath(Devicecontext, { Topleft, { Bottomright.x, Topleft.y }, Bottomright, {Topleft.x, Bottomright.y}, Topleft }, Linewidth, Value);
                    else
                    {
                        const auto Dimensions = vec4i{ Topleft, Bottomright };
                        const auto RAII1 = Stockobject(Devicecontext, NULL_BRUSH);
                        const auto RAII2 = Createpen(Devicecontext, Value, Linewidth);

                        if (Rounding == 0) Rectangle(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w);
                        else RoundRect(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w, Rounding, Rounding);
                    }
                },
                [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                {
                    const auto Size = Bottomright - Topleft;

                    // Without rounding, we can do with a simple polyline.
                    if (Rounding == 0)
                    {
                        drawPath(Devicecontext, { Topleft, { Bottomright.x, Topleft.y }, Bottomright, { Topleft.x, Bottomright.y } }, Linewidth, Outline);
                    }
                    else
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        const auto RAII1 = Stockobject(MaskDC, NULL_BRUSH);
                        const auto RAII2 = Createpen(MaskDC, Maskcolor, Linewidth);

                        RoundRect(MaskDC, 0, 0, Size.x, Size.y, Rounding, Rounding);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Topleft);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    }
                },
                [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                {
                    const auto Size = Bottomright - Topleft;

                    // Without rounding, we can do with a simple polyline.
                    if (Rounding == 0)
                    {
                        drawPath(Devicecontext, { Topleft, { Bottomright.x, Topleft.y }, Bottomright, { Topleft.x, Bottomright.y } }, Linewidth, Outline);
                    }
                    else
                    {
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        SelectObject(MaskDC, Mask);

                        const auto RAII1 = Stockobject(MaskDC, NULL_BRUSH);
                        const auto RAII2 = Createpen(MaskDC, Maskcolor, Linewidth);

                        RoundRect(MaskDC, 0, 0, Size.x, Size.y, Rounding, Rounding);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Topleft);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    }
                }
            }, Outline);
        }
        inline void drawRectangle(HDC Devicecontext, vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline)
        {
            // Literally nothing for us to do here..
            ASSERT(!(std::holds_alternative<std::monostate>(Outline) && std::holds_alternative<std::monostate>(Fill)));

            // Simplest case.
            if (std::holds_alternative<ARGB_t>(Fill) && std::holds_alternative<ARGB_t>(Outline)) [[likely]]
            {
                const auto Dimensions = vec4i{ Topleft, Bottomright };
                const auto RAII1 = Createbrush(Devicecontext, std::get<ARGB_t>(Fill));
                const auto RAII2 = Createpen(Devicecontext, std::get<ARGB_t>(Outline), Linewidth);

                if (Rounding == 0) Rectangle(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w);
                else RoundRect(Devicecontext, Dimensions.x, Dimensions.y, Dimensions.z, Dimensions.w, Rounding, Rounding);
                return;
            }

            // Needs to be done as separate ops.
            if (!std::holds_alternative<std::monostate>(Fill))
            {
                drawRectangle_solid(Devicecontext, Topleft, Bottomright, Rounding, Fill);
            }
            if (!std::holds_alternative<std::monostate>(Outline))
            {
                drawRectangle_outline(Devicecontext, Topleft, Bottomright, Rounding, Linewidth, Outline);
            }
        }

        // Arcs are drawn counter-clockwise.
        inline void drawArc(HDC Devicecontext, vec2i Centerpoint, vec2i Angles, uint8_t Radius, uint8_t Linewidth, const Texture_t &Texture)
        {
            const auto Focalpoint0 = vec2i{ Centerpoint.x + Radius * std::cosf(float((Angles.x + Angles.y) * std::numbers::pi / 180.0f)), Centerpoint.y + Radius * std::sinf(float((Angles.x + Angles.y) * std::numbers::pi / 180.0f)) };
            const auto Focalpoint1 = vec2i{ Centerpoint.x + Radius * std::cosf(float(Angles.x * std::numbers::pi / 180.0f)), Centerpoint.y + Radius * std::sinf(float(Angles.y * std::numbers::pi / 180.0f)) };
            const auto Boundingbox = vec4i{ Centerpoint.x - Radius, Centerpoint.y - Radius, Centerpoint.x + Radius, Centerpoint.y + Radius };

            // Extracted for readability.
            const auto Arc_precalc = [Linewidth](HDC Devicecontext, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, const ARGB_t &Color)
            {
                const auto RAII1 = Stockobject(Devicecontext, NULL_BRUSH);
                const auto RAII2 = Createpen(Devicecontext, Color, Linewidth);

                Arc(Devicecontext, x1, y1, x2, y2, x3, y3, x4, y4);
            };

            std::visit(cmp::Overload{
                    [](std::monostate) { assert(false); },
                    [&](const ARGB_t &Value)
                    {
                        Arc_precalc(Devicecontext, Boundingbox.x, Boundingbox.y, Boundingbox.z, Boundingbox.w, Focalpoint0.x, Focalpoint0.y, Focalpoint1.x, Focalpoint1.y, Value);
                    },
                    [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                    {
                        const auto Size = Boundingbox.cd - Boundingbox.ab;
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);

                        SelectObject(MaskDC, Mask);
                        SetViewportOrgEx(MaskDC, -(Size.x), -(Size.y), nullptr);

                        Arc_precalc(MaskDC, Boundingbox.x, Boundingbox.y, Boundingbox.z, Boundingbox.w, Focalpoint0.x, Focalpoint0.y, Focalpoint1.x, Focalpoint1.y, Maskcolor);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Centerpoint);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    },
                    [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                    {
                        const auto Size = Boundingbox.cd - Boundingbox.ab;
                        const auto MaskDC = CreateCompatibleDC(Devicecontext);
                        const auto Mask = CreateBitmap(Size.x, Size.y, 1, 1, nullptr);

                        SelectObject(MaskDC, Mask);
                        SetViewportOrgEx(MaskDC, -(Size.x), -(Size.y), nullptr);

                        Arc_precalc(MaskDC, Boundingbox.x, Boundingbox.y, Boundingbox.z, Boundingbox.w, Focalpoint0.x, Focalpoint0.y, Focalpoint1.x, Focalpoint1.y, Maskcolor);
                        MaskedBlt(Devicecontext, Value, MaskDC, Size, Centerpoint);

                        DeleteObject(Mask);
                        DeleteDC(MaskDC);
                    }
            }, Texture);
        }

        // NOTE(tcn): There are some other drawing methods available that are easier to use, but for performance we have this mess..
        void drawText_raw(HDC Devicecontext, vec2i Position, vec4i Boundingbox, std::wstring_view String, const Texture_t &Stringtexture, const Texture_t &Backgroundtexture)
        {
            ASSERT(!std::holds_alternative<std::monostate>(Stringtexture));

            const auto GDIRect = (RECT)Boundingbox;
            const auto Boxsize = Boundingbox.cd - Boundingbox.ab;
            const auto Oldmode = SetBkMode(Devicecontext, TRANSPARENT + isColor(Backgroundtexture));

            // Common case.
            if (isColor(Stringtexture) && (isColor(Backgroundtexture) || isNULL(Backgroundtexture))) [[likely]]
            {
                const auto Textcolor = SetTextColor(Devicecontext, std::get<ARGB_t>(Backgroundtexture));
                const auto Backgroundcolor = SetBkColor(Devicecontext, isColor(Backgroundtexture) ? COLORREF(std::get<ARGB_t>(Backgroundtexture)) : 0);

                ExtTextOutW(Devicecontext, Position.x, Position.y, ETO_CLIPPED | (ETO_OPAQUE * isColor(Backgroundtexture)), &GDIRect, String.data(), int(String.size()), nullptr);

                SetBkColor(Devicecontext, Backgroundcolor);
                SetTextColor(Devicecontext, Textcolor);
                SetBkMode(Devicecontext, Oldmode);
                return;
            }

            // The background needs to be rendered as a separate op.
            if (!isNULL(Backgroundtexture))
            {
                drawRectangle_solid(Devicecontext, Boundingbox.ab, Boundingbox.cd, 0, Backgroundtexture);
            }

            std::visit(cmp::Overload{
                [](std::monostate) { assert(false); },
                [&](const ARGB_t &Value)
                {
                    const auto Textcolor = SetTextColor(Devicecontext, Value);

                    ExtTextOutW(Devicecontext, Position.x, Position.y, ETO_CLIPPED, &GDIRect, String.data(), int(String.size()), nullptr);

                    SetTextColor(Devicecontext, Textcolor);
                },
                [&](const std::reference_wrapper<Atlasbitmap_t> &Value)
                {
                    const auto Mask = CreateBitmap(Boxsize.x, Boxsize.y, 1, 1, nullptr);
                    const auto MaskDC = CreateCompatibleDC(Devicecontext);
                    SelectObject(MaskDC, Mask);

                    const auto Textcolor = SetTextColor(Devicecontext, Maskcolor);
                    const auto Offsetrect = (RECT)(vec4i{ Boundingbox.ab - Boxsize, Boundingbox.cd - Boxsize });
                    ExtTextOutW(Devicecontext, Position.x - Boxsize.x, Position.y - Boxsize.y, ETO_CLIPPED, &Offsetrect, String.data(), int(String.size()), nullptr);
                    SetTextColor(Devicecontext, Textcolor);

                    MaskedBlt(Devicecontext, Value, MaskDC, Boxsize, Boundingbox.ab);

                    DeleteObject(Mask);
                    DeleteDC(MaskDC);
                },
                [&](const std::reference_wrapper<Realizedbitmap_t> &Value)
                {
                    const auto Mask = CreateBitmap(Boxsize.x, Boxsize.y, 1, 1, nullptr);
                    const auto MaskDC = CreateCompatibleDC(Devicecontext);
                    SelectObject(MaskDC, Mask);

                    const auto Textcolor = SetTextColor(Devicecontext, Maskcolor);
                    const auto Offsetrect = (RECT)(vec4i{ Boundingbox.ab - Boxsize, Boundingbox.cd - Boxsize });
                    ExtTextOutW(Devicecontext, Position.x - Boxsize.x, Position.y - Boxsize.y, ETO_CLIPPED, &Offsetrect, String.data(), int(String.size()), nullptr);
                    SetTextColor(Devicecontext, Textcolor);

                    MaskedBlt(Devicecontext, Value, MaskDC, Boxsize, Boundingbox.ab);

                    DeleteObject(Mask);
                    DeleteDC(MaskDC);
                }
            }, Stringtexture);

            SetBkMode(Devicecontext, Oldmode);
        }
        void drawText(HDC Devicecontext, vec2i Position, std::wstring_view String, const Texture_t &Stringtexture, const Texture_t &Backgroundtexture, Interface_t::Textoptions_t Options)
        {
            ASSERT(!std::holds_alternative<std::monostate>(Stringtexture));

            // Set the options for text.
            auto Currentalign = GetTextAlign(Devicecontext);
            if (Options.Centered) Currentalign = (Currentalign & ~(TA_LEFT | TA_RIGHT)) | TA_CENTER;
            if (Options.Leftalign) Currentalign = (Currentalign & ~(TA_CENTER | TA_RIGHT)) | TA_LEFT;
            if (Options.Rightalign) Currentalign = (Currentalign & ~(TA_LEFT | TA_CENTER)) | TA_RIGHT;
            const auto Previousalign = SetTextAlign(Devicecontext, Currentalign);

            if (Options.Multiline)
            {
                // For performance, we should handle newlines ourselves here.
                const auto Substrings = Stringsplit(String, L'\n');
                for (const auto &Substring : Substrings)
                {
                    const auto Stringsize = Textsize(Devicecontext, Substring);
                    const auto GDIRect = vec4i{ Position.x, Position.y, Position.x + Stringsize.cx, Position.y + Stringsize.cy };

                    drawText_raw(Devicecontext, Position, GDIRect, Substring, Stringtexture, {});
                    Position.y += Stringsize.cy;
                }
            }
            else
            {
                const auto Stringsize = Textsize(Devicecontext, String);
                const auto GDIRect = vec4i{ Position.x, Position.y, Position.x + Stringsize.cx, Position.y + Stringsize.cy };
                drawText_raw(Devicecontext, Position, GDIRect, String, Stringtexture, {});
            }

            SetTextAlign(Devicecontext, Previousalign);
        }
        void drawText(HDC Devicecontext, vec4i Boundingbox, std::wstring_view String, const Texture_t &Stringtexture, const Texture_t &Backgroundtexture, Interface_t::Textoptions_t Options)
        {
            ASSERT(!std::holds_alternative<std::monostate>(Stringtexture));
            const auto Boxsize = Boundingbox.cd - Boundingbox.ab;

            // Set the options for text.
            auto Currentalign = GetTextAlign(Devicecontext);
            if (Options.Justified) SetTextJustification(Devicecontext, 0, Boxsize.x);
            if (Options.Centered) Currentalign = (Currentalign & ~(TA_LEFT | TA_RIGHT)) | TA_CENTER;
            if (Options.Leftalign) Currentalign = (Currentalign & ~(TA_CENTER | TA_RIGHT)) | TA_LEFT;
            if (Options.Rightalign) Currentalign = (Currentalign & ~(TA_LEFT | TA_CENTER)) | TA_RIGHT;
            const auto Previousalign = SetTextAlign(Devicecontext, Currentalign);

            if (Options.Multiline)
            {
                auto Position = vec2i{ Boundingbox.x + (Boxsize.x / 2) * Options.Centered, Boundingbox.y };
                drawRectangle_solid(Devicecontext, Boundingbox.ab, Boundingbox.cd, 0, Backgroundtexture);

                // For performance, we should handle newlines ourselves here.
                const auto Substrings = Stringsplit(String, L'\n');
                for (const auto &Substring : Substrings)
                {
                    const auto Stringsize = Textsize(Devicecontext, Substring);
                    const auto GDIRect = vec4i{ Position.x, Position.y, Position.x + Stringsize.cx, Position.y + Stringsize.cy };

                    drawText_raw(Devicecontext, Position, GDIRect, Substring, Stringtexture, {});
                    Position.y += Stringsize.cy;
                }
            }
            else
            {
                const auto Position = vec2i{ Boundingbox.x + (Boxsize.x / 2) * Options.Centered, Boundingbox.y };
                drawText_raw(Devicecontext, Position, Boundingbox, String, Stringtexture, {});
            }

            SetTextAlign(Devicecontext, Previousalign);
        }

        // If the Image uses a palette, it will be copied to the DC.
        void drawImage_stretched(HDC Devicecontext, vec4i Destionation, const Texture_t &Image)
        {
            ASSERT(isBitmap(Image));

            if (std::holds_alternative<std::reference_wrapper<Atlasbitmap_t>>(Image))
            {
                const auto Wrapper = std::get<std::reference_wrapper<Atlasbitmap_t>>(Image);
                const auto dstSize = Destionation.cd - Destionation.ab;

                const auto Subset = Wrapper.get().Subset;
                const auto srcSize = Subset.cd - Subset.ab;

                Wrapper.get().Copypalette(Devicecontext);
                StretchBlt(Devicecontext, Destionation.x, Destionation.y, dstSize.x, dstSize.y, HDC(Wrapper.get().Surface), Subset.x, Subset.y, srcSize.x, srcSize.y, SRCCOPY);
            }
            else
            {
                const auto Wrapper = std::get<std::reference_wrapper<Realizedbitmap_t>>(Image);
                const auto srcSize = vec2i{ Wrapper.get().Width, Wrapper.get().Height };
                const auto dstSize = Destionation.cd - Destionation.ab;

                Wrapper.get().Copypalette(Devicecontext);
                StretchBlt(Devicecontext, Destionation.x, Destionation.y, dstSize.x, dstSize.y, HDC(Wrapper.get().Surface), 0, 0, srcSize.x, srcSize.y, SRCCOPY);
            }
        }
        void drawImage_tiled(HDC Devicecontext, vec4i Destionation, const Texture_t &Image)
        {
            ASSERT(isBitmap(Image));

            // NOTE(tcn): More research is needed for how GDI handles clipping, it might be more effective than clamping.
            if (std::holds_alternative<std::reference_wrapper<Atlasbitmap_t>>(Image))
            {
                const auto Wrapper = std::get<std::reference_wrapper<Atlasbitmap_t>>(Image);
                const auto Subset = Wrapper.get().Subset;
                const auto srcSize = Subset.cd - Subset.ab;

                for (int y = Destionation.y; y < Destionation.w; y += srcSize.y)
                {
                    for (int x = Destionation.x; x < Destionation.z; x += srcSize.x)
                    {
                        const auto Clampedwidth = (x + srcSize.x > Destionation.z) ? (Destionation.x - x) : srcSize.x;
                        const auto Clampedheight = (y + srcSize.y > Destionation.w) ? (Destionation.y - y) : srcSize.y;

                        Wrapper.get().Copypalette(Devicecontext);
                        BitBlt(Devicecontext, x, y, Clampedwidth, Clampedheight, HDC(Wrapper.get().Surface), Subset.x, Subset.y, SRCCOPY);
                    }
                }
            }
            else
            {
                const auto Wrapper = std::get<std::reference_wrapper<Realizedbitmap_t>>(Image);
                const auto srcSize = vec2i{ Wrapper.get().Width, Wrapper.get().Height };

                for (int y = Destionation.y; y < Destionation.w; y += srcSize.y)
                {
                    for (int x = Destionation.x; x < Destionation.z; x += srcSize.x)
                    {
                        const auto Clampedwidth = (x + srcSize.x > Destionation.z) ? (Destionation.x - x) : srcSize.x;
                        const auto Clampedheight = (y + srcSize.y > Destionation.w) ? (Destionation.y - y) : srcSize.y;

                        Wrapper.get().Copypalette(Devicecontext);
                        BitBlt(Devicecontext, x, y, Clampedwidth, Clampedheight, HDC(Wrapper.get().Surface), 0, 0, SRCCOPY);
                    }
                }
            }
        }
        void drawImage(HDC Devicecontext, vec2i Position, const Texture_t &Image)
        {
            ASSERT(isBitmap(Image));

            if (std::holds_alternative<std::reference_wrapper<Atlasbitmap_t>>(Image))
            {
                const auto Wrapper = std::get<std::reference_wrapper<Atlasbitmap_t>>(Image);
                const auto Subset = Wrapper.get().Subset;
                const auto Size = Subset.cd - Subset.ab;

                Wrapper.get().Copypalette(Devicecontext);
                BitBlt(Devicecontext, Position.x, Position.y, Size.x, Size.y, HDC(Wrapper.get().Surface), Subset.x, Subset.y, SRCCOPY);
            }
            else
            {
                const auto Wrapper = std::get<std::reference_wrapper<Realizedbitmap_t>>(Image);
                const auto srcSize = vec2i{ Wrapper.get().Width, Wrapper.get().Height };

                Wrapper.get().Copypalette(Devicecontext);
                BitBlt(Devicecontext, Position.x, Position.y, srcSize.x, srcSize.y, HDC(Wrapper.get().Surface), 0, 0, SRCCOPY);
            }
        }
    }

    // Legacy GDI rendering, part of the Windows kernel.
    struct GDIRenderer_t : Interface_t
    {
        HDC Devicecontext{};
        vec2i Imagesize{};
        Handle_t DIB{};

        // AA scaling is a factor of 2.
        bool useAA() const { return !!DIB; }
        vec2i Upscale(vec2i Point) const
        {
            return { Point.x << 1, Point.y << 1 };
        }
        vec2i Downscale(vec2i Point) const
        {
            return { Point.x >> 1, Point.y >> 1 };
        }

        // If no surface is provided, the renderer shall create its own to fit the viewport.
        GDIRenderer_t(vec4i Viewport, Handle_t Surface) : Imagesize(Viewport.cd - Viewport.ab)
        {
            // We have no way to know how large our buffers should be..
            ASSERT(Viewport || Surface);

            // This is somewhat bad practice as a viewport should be provided..
            // But in-case the caller is forwarding a surface from somewhere..
            if (!Viewport && Surface) [[unlikely]]
            {
                BITMAP Headerinfo{};
                GetObject(GetCurrentObject(HDC(Surface), OBJ_BITMAP), sizeof(Headerinfo), &Headerinfo);

                Viewport = { {0, 0}, {Headerinfo.bmWidth, Headerinfo.bmHeight} };
                Imagesize = { Headerinfo.bmWidth, Headerinfo.bmHeight };
            }

            // Either render to the provided surface or our own.
            if (!Surface) Devicecontext = CreateCompatibleDC(NULL);
            else Devicecontext = HDC(Surface);

            // Use inclusive coordinates.
            SetGraphicsMode(Devicecontext, GM_ADVANCED);

            // If the top-left coordinate is not (0, 0), translate future rendercalls.
            if (Viewport.ab) SetViewportOrgEx(Devicecontext, -(Viewport.x), -(Viewport.y), nullptr);

            // Early exit if we are re-using the provided surface.
            if (Surface) return;

            // For downscale AA, we need to adjust our properties.
            Imagesize *= 2;

            // Create our oversized bitmap.
            DIB = Createbitmap(Imagesize.x, Imagesize.y, Colorformat_t::B8G8R8, nullptr);
            SelectObject(Devicecontext, DIB);

            // GDI defers deletion while it's selected into the DC.
            Clear(Devicecontext, { {0, 0}, {Imagesize} });
            DeleteBitmap(DIB);
        }

        void Present(Handle_t Surface)
        {
            if (useAA())
            {
                SetStretchBltMode((HDC)Surface, HALFTONE);
                StretchBlt((HDC)Surface, 0, 0, Imagesize.x / 2, Imagesize.y / 2, Devicecontext, 0, 0, Imagesize.x, Imagesize.y, SRCCOPY);
            }
            else
            {
                BitBlt((HDC)Surface, 0, 0, Imagesize.x, Imagesize.y, Devicecontext, 0, 0, SRCCOPY);
            }
        }

        // Directly rendering to the internal surface (or parent).
        void drawEllipse(vec2i Position, vec2i Size, const Texture_t &Fill)
        {
            if (useAA())
            {
                Position = Upscale(Position);
                Size = Upscale(Size);
            }

            GDI::drawEllipse_solid(Devicecontext, Position, Size, Fill);
        }
        void drawLine(vec2i Start, vec2i Stop, uint8_t Linewidth, const Texture_t &Texture)
        {
            if (useAA())
            {
                Start = Upscale(Start);
                Stop = Upscale(Stop);
                Linewidth *= 2;
            }

            GDI::drawLine(Devicecontext, Start, Stop, Linewidth, Texture);
        }
        void drawRectangle(vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Texture)
        {
            if (useAA())
            {
                Topleft = Upscale(Topleft);
                Bottomright = Upscale(Bottomright);
            }

            GDI::drawRectangle_solid(Devicecontext, Topleft, Bottomright, Rounding, Texture);
        }
        void drawArc(vec2i Centerpoint, vec2i Angles, uint8_t Radius, uint8_t Linewidth, const Texture_t &Texture)
        {
            if (useAA())
            {
                Centerpoint = Upscale(Centerpoint);
                Linewidth *= 2;
                Radius *= 2;
            }

            // NOTE(tcn): To draw an arch going between (100, 0), (50, 50), (100, 100): drawArc(HDC, {50, 50}, {0, 90}, 50, ...)
            return GDI::drawArc(Devicecontext, Centerpoint, Angles, Radius, Linewidth, Texture);
        }
        void drawEllipse(vec2i Position, vec2i Size, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline)
        {
            if (useAA())
            {
                Position = Upscale(Position);
                Size = Upscale(Size);
                Linewidth *= 2;
            }

            GDI::drawEllipse(Devicecontext, Position, Size, Fill, Linewidth, Outline);
        }
        void drawRectangle(vec2i Topleft, vec2i Bottomright, uint8_t Rounding, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline)
        {
            if (useAA())
            {
                Linewidth *= 2;
                Topleft = Upscale(Topleft);
                Bottomright = Upscale(Bottomright);
            }

            GDI::drawRectangle(Devicecontext, Topleft, Bottomright, Rounding, Fill, Linewidth, Outline);
        }

        // For some backends; transparency can be slow, so try to provide a background if possible.
        void drawText(vec2i Position, std::wstring_view String, const Texture_t &Stringtexture, uint8_t Fontsize, std::string_view Fontname, const Texture_t &Backgroundtexture, Textoptions_t Options)
        {
            // Upscale as needed.
            if (useAA())
            {
                Position = Upscale(Position);
                Fontsize *= 2;
            }

            // Resolve the GDI object.
            const auto RAII = Createfont(Devicecontext, Fontname, Fontsize);
            GDI::drawText(Devicecontext, Position, String, Stringtexture, Backgroundtexture, Options);
        }
        void drawText(vec4i Boundingbox, std::wstring_view String, const Texture_t &Stringtexture, uint8_t Fontsize, std::string_view Fontname, const Texture_t &Backgroundtexture, Textoptions_t Options)
        {
            // Upscale as needed.
            if (useAA())
            {
                Boundingbox.ab = Upscale(Boundingbox.ab);
                Boundingbox.cd = Upscale(Boundingbox.cd);
                Fontsize *= 2;
            }

            // Resolve the GDI object.
            const auto RAII = Createfont(Devicecontext, Fontname, Fontsize);
            GDI::drawText(Devicecontext, Boundingbox, String, Stringtexture, Backgroundtexture, Options);
        }

        // Takes any contigious array for simplicity.
        void drawPath(std::span<const vec2i> Points, uint8_t Linewidth, const Texture_t &Texture)
        {
            if (useAA())
            {
                std::vector<POINT> GDIPoints(Points.size());

                for (size_t i = 0; i < Points.size(); ++i)
                    GDIPoints[i] = Upscale(Points[i]);

                GDI::drawPath(Devicecontext, GDIPoints, Linewidth * 2, Texture);
            }
            else
            {
                const std::vector<POINT> GDIPoints(Points.begin(), Points.end());
                GDI::drawPath(Devicecontext, GDIPoints, Linewidth, Texture);
            }
        }
        void drawPolygon(std::span<const vec2i> Points, const Texture_t &Fill, uint8_t Linewidth, const Texture_t &Outline)
        {
            // Need to close the polygon manually.
            const auto Close = Points.first<1>()[0] != Points.first<1>()[0];

            if (useAA())
            {
                std::vector<POINT> GDIPoints(Points.size() + Close);

                for (size_t i = 0; i < Points.size(); ++i)
                    GDIPoints[i] = Upscale(Points[i]);

                if (Close) GDIPoints[Points.size()] = Upscale(Points[0]);

                GDI::drawPolygon(Devicecontext, GDIPoints, Fill, Linewidth * 2, Outline);
            }
            else
            {
                if (!Close)
                {
                    const std::vector<POINT> GDIPoints(Points.begin(), Points.end());
                    GDI::drawPolygon(Devicecontext, GDIPoints, Fill, Linewidth, Outline);
                }
                else
                {
                    std::vector<POINT> GDIPoints(Points.size() + Close);
                    GDIPoints.assign(Points.begin(), Points.end());
                    GDIPoints[Points.size()] = Points[0];

                    GDI::drawPolygon(Devicecontext, GDIPoints, Fill, Linewidth, Outline);
                }
            }
        }

        // Images can also be used for drawing gradients and other such background textures.
        void drawImage_stretched(vec4i Destionation, const Texture_t &Image)
        {
            GDI::drawImage_stretched(Devicecontext, Destionation, Image);
        }
        void drawImage_tiled(vec4i Destionation, const Texture_t &Image)
        {
            GDI::drawImage_tiled(Devicecontext, Destionation, Image);
        }
        void drawImage(vec2i Position, const Texture_t &Image)
        {
            GDI::drawImage(Devicecontext, Position, Image);
        }
    };

    std::unique_ptr<Interface_t> Createrenderer(const vec4i &Viewport, Handle_t Surface)
    {
        return std::make_unique<GDIRenderer_t>(Viewport, Surface);
    }
}
