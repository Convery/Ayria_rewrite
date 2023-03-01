/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-27
    License: MIT
*/

#pragma once
#include <Ayria.hpp>

namespace Frontend
{
    // Helpers for other code to cleanly terminate the frontend.
    namespace Winconsole { extern std::atomic_flag isActive; }

    // Q3-style console.
    std::thread CreateWinconsole();
}
