# The game's launcher, "JetStrike": the game folder, the CD music (ripped from a .cue/.img), the settings
# CONFIG.EXE kept in JS.CFG, the port's options, then jsport.
#
#   cmake -S launcher -B launcher/build -G Ninja && cmake --build launcher/build
#
# or, from the main project, -DJS_LAUNCHER=ON builds it next to jsport.exe.
# Needs wxWidgets 3.2 (MSYS2: mingw-w64-x86_64-wxwidgets3.2-msw; Debian and Ubuntu: libwxgtk3.2-dev).
cmake_minimum_required(VERSION 3.24)
project(jslauncher LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

# MSYS2 and wxWidgets' own CMake builds install a package config; distributions
# such as Debian only ship wx-config, which CMake's FindwxWidgets uses.
find_package(wxWidgets 3.2 CONFIG QUIET COMPONENTS core base)
if(wxWidgets_FOUND AND TARGET wx::wxcore)
    set(JSL_WX_LIBRARIES wx::wxcore wx::wxbase)
else()
    find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
    include(${wxWidgets_USE_FILE})
    set(JSL_WX_LIBRARIES ${wxWidgets_LIBRARIES})
endif()

add_executable(jslauncher WIN32
    app.cpp
    game.cpp
    icon.cpp
    jscfg.cpp
    launcher.cpp
    rip.cpp
    settings.cpp
)
set_target_properties(jslauncher PROPERTIES OUTPUT_NAME "JetStrike" CXX_EXTENSIONS OFF)
target_compile_features(jslauncher PRIVATE cxx_std_17)
target_link_libraries(jslauncher PRIVATE ${JSL_WX_LIBRARIES})
# Built from the main project: beside jsport.exe, where it looks for it.
if(NOT PROJECT_IS_TOP_LEVEL)
    set_target_properties(jslauncher PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

if(WIN32)
    target_sources(jslauncher PRIVATE app.rc)
    target_link_libraries(jslauncher PRIVATE comctl32)
    if(MINGW)
        # The C++ runtime inside the exe, so the build needs fewer DLLs beside it.
        target_link_options(jslauncher PRIVATE -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
    # The wxWidgets DLLs and everything they load, found where the compiler's own DLLs are.
    get_filename_component(JSL_DLL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET jslauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:jslauncher> "-DSEARCH=${JSL_DLL_DIR}"
                -DCMAKE_OBJDUMP=${CMAKE_OBJDUMP} -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/copy_dlls.cmake)
endif()

if(MSVC)
    target_compile_options(jslauncher PRIVATE /W4 /utf-8)
else()
    target_compile_options(jslauncher PRIVATE -Wall -Wextra)
endif()
