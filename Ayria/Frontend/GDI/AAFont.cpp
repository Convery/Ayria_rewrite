/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-09-11
    License: MIT
*/

#include <Utilities/Utilities.hpp>
#include "../Rendering.hpp"

#if defined (_WIN32)
namespace Rendering
{
    Handle_t getFont(const std::string &Name, uint8_t Size)
    {
        return CreateFontA(Size, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, Name.c_str());
    }
    void Registerfont(Blob_view_t Fontdata)
    {
        DWORD Fontcount;
        (void)AddFontMemResourceEx(PVOID(Fontdata.data()), (DWORD)Fontdata.size(), NULL, &Fontcount);
    }
    Handle_t getDefaultfont(uint8_t Size)
    {
        static Hashmap<uint8_t, Handle_t> Fonts{};
        const auto Entry = &Fonts[Size];

        if (*Entry == NULL) *Entry = getFont("Consolas", Size);
        return *Entry;
    }
}
#endif
