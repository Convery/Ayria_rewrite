/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-03-08
    License: MIT

    Inspired by WINAPI's WaitOnAddress.
*/

#include "Backend.hpp"

static std::vector<void(__cdecl *)()> Callback{};
static std::vector<void *> Source{}, Compare{};
static std::vector<uint8_t> Datasize{};
static Spinlock_t Threadguard{};

namespace Backend
{
    void onMemorywrite(void *pSource, void *pCompare, uint8_t Size, void(__cdecl *pCallback)())
    {
        std::scoped_lock Lock(Threadguard);
        assert(pCallback);

        Callback.emplace_back(pCallback);
        Compare.emplace_back(pCompare);
        Source.emplace_back(pSource);
        Datasize.emplace_back(Size);

        assert(Source.size() == Compare.size() == Callback.size() == Datasize.size());
    }
}

static void __cdecl Poll()
{
    std::scoped_lock Lock(Threadguard);

    for (size_t i = 0; i < Source.size(); ++i)
    {
        const auto pCompare = Compare[i];
        const auto pSource = Source[i];
        const auto Size = Datasize[i];

        if (0 != std::memcmp(pSource, pCompare, Size)) [[unlikely]]
        {
            Callback[i]();
        }
    }
}

// Poll on every frame.
static struct Startup_t {
    Startup_t() { Backend::Backgroundtasks::addPeriodictask(Poll, 1); }
} Startup{};
