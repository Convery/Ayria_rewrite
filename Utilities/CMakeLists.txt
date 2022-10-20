cmake_minimum_required(VERSION 3.11)

# Get the modulename from the directory.
get_filename_component(Directory ${CMAKE_CURRENT_LIST_DIR} NAME)
string(REPLACE " " "_" Directory ${Directory})
set(MODULENAME ${Directory})
set(CMAKE_DEBUG_POSTFIX "")

# Include the top of the dir.
include_directories("${CMAKE_CURRENT_LIST_DIR}")

# Just pull all the files from the directory.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.cpp *.c *.asm)

add_library(${MODULENAME} STATIC ${SOURCES})
add_definitions(-DMODULENAME="${MODULENAME}")
set_target_properties(${MODULENAME} PROPERTIES PREFIX "")
set_target_properties(${MODULENAME} PROPERTIES COMPILE_FLAGS "${EXTRA_CMPFLAGS}" LINK_FLAGS "${EXTRA_LNKFLAGS}")

# Create an executable for unittests.
add_executable("libTest" ${SOURCES})
add_definitions(-DMODULENAME="libTest")
set(MODULE_LIBS ${MODULE_LIBS} Utilities)
set_target_properties("libTest" PROPERTIES PREFIX "")
target_link_libraries("libTest" ${PLATFORM_LIBS} ${MODULE_LIBS})
set_target_properties("libTest" PROPERTIES COMPILE_FLAGS "${EXTRA_CMPFLAGS}" LINK_FLAGS "${EXTRA_LNKFLAGS}")
