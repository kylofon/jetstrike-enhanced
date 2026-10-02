cmake_minimum_required(VERSION 3.24)
project(jsport C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Port of JS_CDROM.EXE (JetStrike CD, 1994): flat 32-bit Watcom C, VGA mode X 320x240,
# Sound Blaster mixer + CD audio (ripped to Game/MUSIC). See PORTING.md.

find_package(SDL3 REQUIRED CONFIG)

file(GLOB JS_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(jsport ${JS_SOURCES})
target_include_directories(jsport PRIVATE src)
target_link_libraries(jsport PRIVATE SDL3::SDL3)
if(MSVC)
    target_compile_options(jsport PRIVATE /W3)
else()
    target_compile_options(jsport PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(jsport PRIVATE -Wno-format-truncation)
    endif()
endif()
if(WIN32)
    add_custom_command(TARGET jsport POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:jsport> $<TARGET_FILE_DIR:jsport>
        COMMAND_EXPAND_LISTS)
    # And what those load in turn (MSYS2's SDL3.dll needs libiconv-2.dll etc.), so the game starts
    # outside the MSYS2 shell.
    get_filename_component(JS_DLL_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    add_custom_command(TARGET jsport POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:jsport> "-DSEARCH=${JS_DLL_DIR}"
                -DCMAKE_OBJDUMP=${CMAKE_OBJDUMP} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_dlls.cmake)
endif()
