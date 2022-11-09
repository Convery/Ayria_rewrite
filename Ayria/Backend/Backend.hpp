/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-11-09
    License: MIT
*/

#pragma once
#include <Ayria.hpp>

// Simple callbacks from a background thread.
namespace Backend::Tasks
{
    using Callback_t = void(__cdecl *)();

    // Startup tasks are only ran once, at Initialize().
    void addPeriodictask(Callback_t Callback, uint32_t PeriodMS = 1000);
    void addStartuptask(Callback_t Callback);

    // Called from usercode (in or after main).
    void Initialize();
    void Terminate();
}
