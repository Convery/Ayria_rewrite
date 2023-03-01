/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-09-16
    License: MIT
*/

#pragma once
#include <Stdinclude.hpp>

// Packed, remember to keep natural alignment for pointers.
#pragma pack(push, 1)
class alignas (64) Globalstate_t
{
    // Helper to initialize pointers in the same region.
    static inline std::pmr::monotonic_buffer_resource Internal{ 128 };
    template <typename T, typename ...Args> static auto Allocate(Args&& ...va)
    { auto Buffer = Internal.allocate(sizeof(T)); return new (Buffer) T(std::forward<Args>(va)...); }

    public:
    // Set through Platformwrapper.
    uint32_t GameID{}, ModID{};

    // Primary user identifier, either random or based on HWID.
    std::unique_ptr<qDSA::Privatekey_t> Privatekey{ Allocate<qDSA::Privatekey_t>() };
    qDSA::Publickey_t Publickey{};

    // Rarely used (for now), but good to have in the future.
    std::unique_ptr<std::pmr::u8string> Username{ Allocate<std::pmr::u8string>(&Internal) };

    // Configuration flags.
    union
    {
        uint8_t RAW;
        struct
        {
            uint8_t enableExternalconsole : 1;
            uint8_t enableIATHooking : 1;
            uint8_t enableFileshare : 1;
            uint8_t modifiedConfig : 1;
            uint8_t noNetworking : 1;
            uint8_t pruneDB : 1;
        };
    } Configuration;

    // State flags.
    union
    {
        uint8_t RAW;
        struct
        {
            // Social state.
            uint8_t isPrivate : 1;
            uint8_t isAway : 1;

            // Matchmaking state.
            uint8_t isHosting : 1;
            uint8_t isIngame : 1;

            // Internal state.
            uint8_t Pluginflag : 1;
        };
    } State;

    // Helpers for access to the members.
    [[nodiscard]] const std::string &getLongID() const
    {
        static std::string LongID;
        static uint32_t Lasthash{};

        const auto Currenthash = Hash::WW32(Publickey);
        if (Currenthash != Lasthash) [[unlikely]]
        {
            LongID = Base58::Encode(Publickey);
            Lasthash = Currenthash;
        }

        return LongID;
    }
    [[nodiscard]] uint64_t getShortID() const { return (Hash::WW64(getLongID()) << 32) | Hash::WW32(getLongID()); }

    // No destruction for pointers..
    ~Globalstate_t()
    {
        (void)Privatekey.release();
        (void)Username.release();
    }

    // 14 / 6 bytes available here.
    #pragma warning(suppress: 4324)
};
#pragma pack(pop)

// Let's ensure that modifications do not extend the global state too much.
static_assert(sizeof(Globalstate_t) == 64, "Do not cross cache lines with Globalstate_t!");
extern Globalstate_t Global;

#include "Backend/Backend.hpp"
#include "Communication/Communication.hpp"
#include "Frontend/Frontend.hpp"
