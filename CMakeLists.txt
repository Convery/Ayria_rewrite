cmake_minimum_required(VERSION 3.11)

# Future-proofing VCPKG..
list(APPEND VCPKG_FEATURE_FLAGS "versions")

# Overide the submodule install with VCPKG_ROOT
if (DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
else()
    set(CMAKE_TOOLCHAIN_FILE "./vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# Override VCPKG's dynamic linking
if (DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET MATCHES "-static$")
    set(VCPKG_TARGET_TRIPLET "${VCPKG_TARGET_TRIPLET}-static")
endif()

# Convery (tcn@ayria.se)
project(AYRIA)

# Needed for static analysis.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Reduce the number of dependencies.
set(AYRIA_MINIMAL_BUILD OFF)
if(AYRIA_MINIMAL_BUILD)
    add_definitions(-DAYRIA_MINIMAL_BUILD=1)
endif()

# Use the latest standard at this time.
set(CMAKE_CXX_STANDARD 23)
enable_language(ASM_MASM)

# Export to the a gitignored directory.
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/Bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/Bin)

# Separate release and debug-builds.
mark_as_advanced(CMAKE_IMPORT_LIBRARY_SUFFIX CMAKE_DEBUG_POSTFIX)
set(CMAKE_DEBUG_POSTFIX "d")

# Per compiler instructions.
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")

    # Windows compilation.
    if(CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
        set(EXTRA_CMPFLAGS "${EXTRA_CMPFLAGS} /Zc:__cplusplus /permissive- /EHc-s- /Oiysyb3 /GF /Gy /Gw /GR- /GL /utf-8 /Brepro /fp:except-")
        set(EXTRA_LNKFLAGS "${EXTRA_LNKFLAGS} /time /LTCG /debug:full /filealign:512 /align:2097152 /opt:ref,icf=8,lbr /incremental:no /Brepro /cgthreads:8")
    else()
        set(EXTRA_CMPFLAGS "${EXTRA_CMPFLAGS} /Zc:__cplusplus /permissive- /EHc-s- /Gy /utf-8 /fp:except- /fsanitize=address") # /d2TPReport
        set(EXTRA_LNKFLAGS "${EXTRA_LNKFLAGS} /time /LTCG /debug:full /opt:ref,lbr /incremental:no /cgthreads:8")
    endif()

    # Need to specify the arch for x86.
    if(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
        set(EXTRA_CMPFLAGS "${EXTRA_CMPFLAGS} /arch:SSE2")
    endif()

    # Force static runtime, warnings, and override default flags.
    foreach(Flag
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE)
        string(REGEX REPLACE "/W[0-4]" "/W4" ${Flag} "${${Flag}}")
        string(REPLACE "/EHsc" "/EHc-s-" ${Flag} "${${Flag}}")
        string(REPLACE "/GR " "/GR- " ${Flag} "${${Flag}}")
        string(REPLACE "/Zi " "/Z7 " ${Flag} "${${Flag}}")
        string(REPLACE "/Ob2" "/Ob3" ${Flag} "${${Flag}}")
        string(REPLACE "/Ob1" "/Ob3" ${Flag} "${${Flag}}")
        string(REPLACE "/MD" "/MT" ${Flag} "${${Flag}}")
    endforeach()
else()
    # Assume GNU-GCC/CLANG.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG} -g")

    # For some reason my CMAKE fails to differentiate between x86 and x64.
    if(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
        set(EXTRA_CMPFLAGS ${EXTRA_CMPFLAGS} "-m32")
        set(EXTRA_LNKFLAGS ${EXTRA_LNKFLAGS} "-m32 -static-libgcc -static-libstdc++")
    else()
        set(EXTRA_CMPFLAGS ${EXTRA_CMPFLAGS} "-m64")
        set(EXTRA_LNKFLAGS ${EXTRA_LNKFLAGS} "-m64 -static-libgcc -static-libstdc++")
    endif()
endif()

# Platform libraries to be linked.
if(WIN32)
    set(PLATFORM_LIBS ws2_32 gdi32 Msimg32 Crypt32 Winmm)
else()
    set(PLATFORM_LIBS dl pthread)
endif()

# Try to add the lib directories based on a required package.
find_library(STD_LIB_RAW libssl.lib HINTS ${PROJECT_SOURCE_DIR})
find_path(STD_INC openssl/ssl.h  HINTS ${PROJECT_SOURCE_DIR})
get_filename_component(STD_LIB ${STD_LIB_RAW} DIRECTORY)
include_directories(${STD_INC})
link_directories(${STD_LIB})

# Global utilities.
include_directories("${PROJECT_SOURCE_DIR}")
set(MODULE_LIBS ${MODULE_LIBS} Utilities)
add_subdirectory(Utilities)

# Add the sub-projects.
add_subdirectory(Ayria)
#add_subdirectory(Injector)
#add_subdirectory(Localnetworking)
#add_subdirectory(Platformwrapper)

## Examples.
#add_subdirectory(Plugintemplate)
