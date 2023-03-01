/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-27
    License: MIT
*/

#include "Frontend.hpp"

static HWND Consolehandle{}, Inputhandle{}, Bufferhandle{};
static constexpr size_t InputID = 1, BufferID = 2;
static vec2u Windowsize{};
static WNDPROC oldLine{};

static LRESULT __stdcall Inputproc(HWND Handle, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_SETFOCUS)
    {
        SendMessageW(Bufferhandle, EM_SETSEL, (WPARAM)-1, -1);
        return 0;
    }

    if (Message == WM_CHAR && wParam == VK_RETURN)
    {
        wchar_t Input[1024]{};
        const size_t Len = GetWindowTextW(Inputhandle, Input, 1024);
        Communication::Console::execCommand(std::wstring_view{ Input, Len });

        SetWindowTextW(Inputhandle, L"");
        return 0;
    }

    if (Message == WM_CHAR)
    {
        if (wParam == 0xA7 || wParam == 0xBD || wParam == L'~' || wParam == VK_OEM_5)
            return 0;
    }

    return CallWindowProcW(oldLine, Handle, Message, wParam, lParam);
}
static LRESULT __stdcall Consoleproc(HWND Handle, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_DESTROY)
    {
        Frontend::Winconsole::isActive.clear();
    }

    if (Message == WM_NCLBUTTONDOWN)
    {
        SendMessageW(Bufferhandle, EM_SETSEL, (WPARAM)-1, -1);
        SetFocus(Inputhandle);
    }

    if (Message == WM_ACTIVATE)
    {
        if (LOWORD(wParam) == WA_INACTIVE) SendMessageW(Bufferhandle, EM_SETSEL, (WPARAM)-1, -1);
        SetFocus(Inputhandle);
    }

    return DefWindowProcW(Handle, Message, wParam, lParam);
}

namespace Frontend
{
    namespace Winconsole { std::atomic_flag isActive{}; }

    std::thread CreateWinconsole()
    {
        return std::thread([]()
        {
            constexpr DWORD Style = WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX;

            WNDCLASSW Windowclass{};
            Windowclass.lpfnWndProc = Consoleproc;
            Windowclass.lpszClassName = L"Windows_console";
            Windowclass.hIcon = LoadIconW(NULL, (LPCWSTR)1);
            Windowclass.hbrBackground = (HBRUSH)COLOR_WINDOW;
            Windowclass.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassW(&Windowclass);

            const auto Device = GetDC(GetDesktopWindow());
            const auto Height = GetDeviceCaps(Device, VERTRES);
            const auto Width = GetDeviceCaps(Device, HORZRES);
            ReleaseDC(GetDesktopWindow(), Device);

            RECT Windowrect{ 0, 0, 820, 450 };
            AdjustWindowRect(&Windowrect, Style, FALSE);

            const vec2u Position{ (Width - 820) / 2, (Height - 450) / 2 };
            Windowsize = { Windowrect.right - Windowrect.left + 1,  Windowrect.bottom - Windowrect.top + 1 };

            Consolehandle = CreateWindowExW(NULL, Windowclass.lpszClassName, L"Console", Style,
                                            Position.x, Position.y, Windowsize.x, Windowsize.y, NULL, NULL, NULL, NULL);
            assert(Consolehandle); // WTF?

            constexpr auto Linestyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL;
            constexpr auto Bufferstyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL;
            Bufferhandle = CreateWindowExW(NULL, L"edit", NULL, Bufferstyle, 6, 5, 806, 418, Consolehandle, (HMENU)BufferID, NULL, NULL);
            Inputhandle = CreateWindowExW(WS_EX_WINDOWEDGE, L"edit", NULL, Linestyle, 6, 426, 808, 20, Consolehandle, (HMENU)InputID, NULL, NULL);
            assert(Bufferhandle); assert(Inputhandle);

            const auto DC = GetDC(Consolehandle);
            const auto Lineheight = -((8 * GetDeviceCaps(DC, LOGPIXELSY)) / 55);
            const auto Font = CreateFontW(Lineheight, 0, 0, 0, FW_MEDIUM, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_NATURAL_QUALITY, 0, L"Consolas");
            ReleaseDC(Consolehandle, DC);

            oldLine = (WNDPROC)SetWindowLongPtrW(Inputhandle, GWLP_WNDPROC, (LONG_PTR)Inputproc);

            SendMessageW(Bufferhandle, WM_SETFONT, (WPARAM)Font, NULL);
            SendMessageW(Inputhandle, WM_SETFONT, (WPARAM)Font, NULL);

            ShowWindow(Consolehandle, SW_SHOWDEFAULT);
            UpdateWindow(Consolehandle);
            SetFocus(Inputhandle);

            Winconsole::isActive.test_and_set();
            while (Winconsole::isActive.test())
            {
                MSG Message;
                while (PeekMessageW(&Message, Consolehandle, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&Message);
                    DispatchMessageW(&Message);
                }

                static uint32_t Lastmessage{};
                if (Lastmessage != Communication::Console::LastmessageID)
                {
                    // If the user have selected text, skip.
                    const auto Pos = SendMessageW(Bufferhandle, EM_GETSEL, NULL, NULL);
                    if (HIWORD(Pos) == LOWORD(Pos))
                    {
                        std::u8string Concatenated;
                        Lastmessage = Communication::Console::LastmessageID;

                        for (const auto &String : Communication::Console::getMessages() |  std::views::keys)
                        {
                            if (String.empty()) [[unlikely]]
                                continue;

                            Concatenated += String;
                            Concatenated += u8"\r\n";
                        }

                        const auto Unicode = Encoding::toUNICODE(Concatenated);
                        SetWindowTextW(Bufferhandle, Unicode.c_str());
                        SendMessageW(Bufferhandle, WM_VSCROLL, SB_BOTTOM, 0);
                    }
                }

                // Sleep for a frame or two.
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        });
    }
}
