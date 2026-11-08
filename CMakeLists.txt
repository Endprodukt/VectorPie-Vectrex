cmake_minimum_required(VERSION 3.21)

project(vectrex_vectorpie VERSION 0.1.0 LANGUAGES C CXX)

# Builds: see BUILD.md.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(VX_BUILD_GAME "Build the emulator program (needs the VectorPie SDK and SDL2)" ON)

# --- the core: upstream's files unchanged (third_party/vectrex_emu), no SDK, no SDL ---------------

set(VX_UP ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vectrex_emu/core)
# The translation units as upstream's build_linux.sh lists them, vtest.c through src/core/vx_core.c
# (the other .c files are #included by these).
add_library(vxcore STATIC
    src/core/vx_core.c
    ${VX_UP}/vecx.c
    ${VX_UP}/e6809.c
    ${VX_UP}/e8910.c
    ${VX_UP}/cm33.c
    ${VX_UP}/rp2350.c
    ${VX_UP}/host.c
    ${VX_UP}/arm11.c
    ${VX_UP}/usbhost.c
)
target_include_directories(vxcore PUBLIC src PRIVATE ${VX_UP})
# upstream's own flags (build_linux.sh): its results are checked with them
target_compile_options(vxcore PRIVATE -O3 -w -ffp-contract=off -fgnu89-inline)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(vxcore PUBLIC Threads::Threads)
if(UNIX)
    target_link_libraries(vxcore PUBLIC m)
endif()

# --- tools -----------------------------------------------------------------------------------------

add_executable(vxtest tools/vxtest/main.c)
target_link_libraries(vxtest PRIVATE vxcore)
target_compile_options(vxtest PRIVATE -Wall -Wextra)

# --- the program: on the VectorPie SDK (a window on a PC, the vector board on the cabinet) ----------

if(VX_BUILD_GAME)
    set(VX_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/VectorPie/sdk"
        CACHE PATH "VectorPie SDK source directory")
    if(NOT EXISTS "${VX_SDK_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "VectorPie SDK not found in ${VX_SDK_DIR}: see BUILD.md "
                            "(or configure with -DVX_BUILD_GAME=OFF for the core and tools only)")
    endif()
    if(VX_PI_BUILD)
        # cmake/toolchain-pi-clang.cmake: the cabinet's own SDL2, by its file.
        add_library(SDL2::SDL2 SHARED IMPORTED GLOBAL)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0"
            IMPORTED_SONAME "libSDL2-2.0.so.0"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include/SDL2")
    endif()
    set(VP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(VP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${VX_SDK_DIR}" vectorpie EXCLUDE_FROM_ALL)

    add_library(vxout STATIC
        src/platform/app.c
        src/platform/audio.c
        src/platform/browser.c
        src/platform/config.c
        src/platform/roms.c
        src/platform/controls.c
        src/platform/menu.c
        src/render/draw.c
    )
    target_link_libraries(vxout PUBLIC vxcore vectorpie)
    target_compile_options(vxout PRIVATE -Wall -Wextra)

    add_executable(vectrex src/main.c)
    target_link_libraries(vectrex PRIVATE vxout)
    vp_program(vectrex)

    # package files next to the program (vp_asset_path looks there)
    configure_file(package/game.ini ${CMAKE_CURRENT_BINARY_DIR}/game.ini COPYONLY)
    configure_file(package/vectrex.ini ${CMAKE_CURRENT_BINARY_DIR}/vectrex.ini COPYONLY)
    configure_file(package/names.ini ${CMAKE_CURRENT_BINARY_DIR}/names.ini COPYONLY)
    configure_file(package/system/bios.bin ${CMAKE_CURRENT_BINARY_DIR}/system/bios.bin COPYONLY)
endif()

if(VX_BUILD_GAME)
    # Checks of the settings' layering and the digital ramp (no screen, no controls).
    add_executable(vxunit tools/vxunit/main.c)
    target_link_libraries(vxunit PRIVATE vxout)
    vp_program(vxunit)
endif()
