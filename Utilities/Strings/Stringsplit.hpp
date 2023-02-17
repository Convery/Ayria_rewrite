/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-07-18
    License: MIT

    A simple utility for splitting/tokenizing strings.
*/

#pragma once
#include <Utilities/Utilities.hpp>

// Standard commandline parsing.
constexpr std::vector<std::string_view> Tokenizestring(std::string_view Input)
{
    std::vector<std::string_view> Tokens{};
    bool Quoted{};

    while(!Input.empty())
    {
        const auto P1 = Input.find('\"');
        const auto P2 = Input.find(' ');

        if (Quoted)
        {
            // Malformed quote-sequence, stop parsing.
            if (P1 == std::string_view::npos) [[unlikely]]
                return Tokens;

            if (P1) Tokens.emplace_back(Input.substr(0, P1));
            Input.remove_prefix(P1 + 1);
            Quoted = false;
        }
        else
        {
            if (P2 < P1)
            {
                if (P2) Tokens.emplace_back(Input.substr(0, P2));
                Input.remove_prefix(P2 + 1);
            }
            else if (P1 != std::string_view::npos)
            {
                if (P1) Tokens.emplace_back(Input.substr(0, P1));
                Input.remove_prefix(P1 + 1);
                Quoted = true;
            }
            else
            {
                break;
            }
        }
    }

    // Any remaining.
    if (!Input.empty())
        Tokens.emplace_back(Input);

    return Tokens;
}
constexpr std::vector<std::wstring_view> Tokenizestring(std::wstring_view Input)
{
    std::vector<std::wstring_view> Tokens{};
    bool Quoted{};

    while (!Input.empty())
    {
        const auto P1 = Input.find(L'\"');
        const auto P2 = Input.find(L' ');

        if (Quoted)
        {
            // Malformed quote-sequence, stop parsing.
            if (P1 == std::wstring_view::npos) [[unlikely]]
                return Tokens;

            if (P1) Tokens.emplace_back(Input.substr(0, P1));
            Input.remove_prefix(P1 + 1);
            Quoted = false;
        }
        else
        {
            if (P2 < P1)
            {
                if (P2) Tokens.emplace_back(Input.substr(0, P2));
                Input.remove_prefix(P2 + 1);
            }
            else if (P1 != std::wstring_view::npos)
            {
                if (P1) Tokens.emplace_back(Input.substr(0, P1));
                Input.remove_prefix(P1 + 1);
                Quoted = true;
            }
            else
            {
                break;
            }
        }
    }

    // Any remaining.
    if (!Input.empty())
        Tokens.emplace_back(Input);

    return Tokens;
}
constexpr std::vector<std::u8string_view> Tokenizestring(std::u8string_view Input)
{
    std::vector<std::u8string_view> Tokens{};
    bool Quoted{};

    while(!Input.empty())
    {
        const auto P1 = Input.find('\"');
        const auto P2 = Input.find(' ');

        if (Quoted)
        {
            // Malformed quote-sequence, stop parsing.
            if (P1 == std::string_view::npos) [[unlikely]]
                return Tokens;

            if (P1) Tokens.emplace_back(Input.substr(0, P1));
            Input.remove_prefix(P1 + 1);
            Quoted = false;
        }
        else
        {
            if (P2 < P1)
            {
                if (P2) Tokens.emplace_back(Input.substr(0, P2));
                Input.remove_prefix(P2 + 1);
            }
            else if (P1 != std::string_view::npos)
            {
                if (P1) Tokens.emplace_back(Input.substr(0, P1));
                Input.remove_prefix(P1 + 1);
                Quoted = true;
            }
            else
            {
                break;
            }
        }
    }

    // Any remaining.
    if (!Input.empty())
        Tokens.emplace_back(Input);

    return Tokens;
}

// Tokens of length 0 are dropped unless PreserveNULL = true.
constexpr std::vector<std::string_view> Stringsplit(std::string_view Input, std::string_view Needle, bool PreserveNULL = false)
{
    std::vector<std::string_view> Tokens{};

    while (!Input.empty())
    {
        const auto Length = Input.find(Needle);
        if (Length == std::string_view::npos) break;

        if (PreserveNULL || Length) Tokens.emplace_back(Input.substr(0, Length));
        Input.remove_prefix(Length + Needle.size());
    }

    // Any remaining.
    if (!Input.empty())
        Tokens.emplace_back(Input);

    return Tokens;
}
constexpr std::vector<std::wstring_view> Stringsplit(std::wstring_view Input, std::wstring_view Needle, bool PreserveNULL = false)
{
    std::vector<std::wstring_view> Tokens{};

    while (!Input.empty())
    {
        const auto Length = Input.find(Needle);
        if (Length == std::wstring_view::npos) break;

        if (PreserveNULL || Length) Tokens.emplace_back(Input.substr(0, Length));
        Input.remove_prefix(Length + Needle.size());
    }

    // Any remaining.
    if (!Input.empty())
        Tokens.emplace_back(Input);

    return Tokens;
}
constexpr std::vector<std::u8string_view> Stringsplit(std::u8string_view Input, std::u8string_view Needle, bool PreserveNULL = false)
{
    std::vector<std::u8string_view> Tokens{};

    while (!Input.empty())
    {
        const auto Length = Input.find(Needle);
        if (Length == std::string_view::npos) break;

        if (PreserveNULL || Length) Tokens.emplace_back(Input.substr(0, Length));
        Input.remove_prefix(Length + Needle.size());
    }

    // Any remaining.
    if (!Input.empty())
        Tokens.emplace_back(Input);

    return Tokens;
}

// Common overloads.
constexpr std::vector<std::string_view> Stringsplit(std::string_view Input, char Needle, bool PreserveNULL = false)
{
    return Stringsplit(Input, std::string_view{ &Needle, 1 }, PreserveNULL);
}
constexpr std::vector<std::wstring_view> Stringsplit(std::wstring_view Input, wchar_t Needle, bool PreserveNULL = false)
{
    return Stringsplit(Input, std::wstring_view{ &Needle, 1 }, PreserveNULL);
}
constexpr std::vector<std::u8string_view> Stringsplit(std::u8string_view Input, char8_t Needle, bool PreserveNULL = false)
{
    return Stringsplit(Input, std::u8string_view{ &Needle, 1 }, PreserveNULL);
}
