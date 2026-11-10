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
        src/platform/coloredit.c
        src/platform/controls.c
        src/platform/menu.c
        src/platform/port2.c
        src/render/colors.c
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
    file(GLOB VX_PROFILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/colors/*.ini)
    foreach(f ${VX_PROFILES})
        get_filename_component(n ${f} NAME)
        configure_file(${f} ${CMAKE_CURRENT_BINARY_DIR}/colors/${n} COPYONLY)
    endforeach()
    configure_file(package/system/bios.bin ${CMAKE_CURRENT_BINARY_DIR}/system/bios.bin COPYONLY)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/package/system/sp0256-al2.bin)    # (the speech ROM: not in the repository)
        configure_file(package/system/sp0256-al2.bin ${CMAKE_CURRENT_BINARY_DIR}/system/sp0256-al2.bin COPYONLY)
    endif()
endif()

if(VX_BUILD_GAME)
    # Checks of the settings' layering and the digital ramp (no screen, no controls).
    add_executable(vxunit tools/vxunit/main.c)
    target_link_libraries(vxunit PRIVATE vxout)
    vp_program(vxunit)
endif()

if(VX_BUILD_GAME)
    # vxcolor: the colour editor from the command line, for an AI or a script (docs/AI_GUIDE.md).
    add_executable(vxcolor tools/vxcolor/main.c)
    target_link_libraries(vxcolor PRIVATE vxout)
    target_compile_options(vxcolor PRIVATE -Wall -Wextra)
    vp_program(vxcolor)
endif()

if(VX_BUILD_GAME AND WIN32)
    # Vectrex Colour Studio: the PC's colour editor for the cabinet (src/studio/studio.cpp; Dear ImGui
    # 1.91.5 in third_party/imgui, MIT).
    set(VX_IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    add_executable(vxstudio WIN32
        src/studio/studio.cpp
        ${VX_IMGUI}/imgui.cpp ${VX_IMGUI}/imgui_draw.cpp ${VX_IMGUI}/imgui_tables.cpp ${VX_IMGUI}/imgui_widgets.cpp
        ${VX_IMGUI}/backends/imgui_impl_sdl2.cpp ${VX_IMGUI}/backends/imgui_impl_sdlrenderer2.cpp)
    target_include_directories(vxstudio PRIVATE ${VX_IMGUI} src)
    set_target_properties(vxstudio PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON OUTPUT_NAME "Vectrex Colour Studio")
    target_link_libraries(vxstudio PRIVATE vxout)
    vp_program(vxstudio)
endif()

if(VX_BUILD_GAME)
    # A game without a screen: its picture with the colour profile as SVG (tools/vxshot/main.c).
    add_executable(vxshot tools/vxshot/main.c)
    target_link_libraries(vxshot PRIVATE vxout)
    vp_program(vxshot)
endif()
