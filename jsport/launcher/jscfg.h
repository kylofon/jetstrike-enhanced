// jscfg.h -- JS.CFG, the settings file CONFIG.EXE wrote and the game reads at start-up: 60 bytes, 30
// little-endian words (port/spec/platform.md §3.1, port/formats/data.md). The launcher replaces CONFIG.EXE
// (PLAN.md decision 5) and keeps the file in exactly that format; words it has no control for (the three
// key slots the game never reads, the SB IRQ) are kept as they were.
#pragma once

#include <wx/string.h>

#include <array>
#include <cstdint>
#include <vector>

class wxKeyEvent;

// Word indices (byte offset / 2).
enum CfgWord {
    CFG_MUSIC = 0,          // +0x00 CD music on
    CFG_SFX = 1,            // +0x02 sound effects on
    CFG_KEY0 = 2,           // +0x04..+0x22 16 key scancodes
    CFG_JOY = 18,           // +0x24 joystick on
    CFG_JOY_LEFT = 19,      // +0x26..+0x2c calibration thresholds: left, up, right, down
    CFG_JOY_UP = 20,
    CFG_JOY_RIGHT = 21,
    CFG_JOY_DOWN = 22,
    CFG_DETAIL = 23,        // +0x2e detail / parallax
    CFG_GUNS = 24,          // +0x30 fire guns
    CFG_FIRE_LEFT = 25,     // +0x32 fire left weapon
    CFG_FIRE_RIGHT = 26,    // +0x34 fire right weapon
    CFG_LOOK = 27,          // +0x36 look around
    CFG_TARGET = 28,        // +0x38 change target
    CFG_SB_IRQ = 29,        // +0x3a Sound Blaster IRQ (the port has no use for it)
    CFG_WORDS = 30
};

const size_t CFG_SIZE = CFG_WORDS * 2;

using JsCfg = std::array<uint16_t, CFG_WORDS>;

// The shipped Game/JS.CFG: music and effects on, the original keys, joystick off (with the calibration
// a typical stick gave), detail on, IRQ 5.
JsCfg DefaultCfg();

// Reads `path`. Returns false (and says why) if the file can't be read or is shorter than 60 bytes.
bool ReadCfg(const wxString& path, JsCfg& cfg, wxString& error);
std::vector<uint8_t> CfgBytes(const JsCfg& cfg);
JsCfg CfgFromBytes(const uint8_t* bytes);
bool WriteCfg(const wxString& path, const JsCfg& cfg, wxString& error);

// The calibration only matters with the joystick on: the port puts the SDL gamepad's stick just past these
// thresholds (platform.c Joystick_Poll), so any nonzero left < right, up < down work. Unusable values
// (a file that never had a calibration) become the shipped ones. Returns true if anything changed.
bool FixCalibration(JsCfg& cfg);

// A key the game reads, as the launcher shows it.
struct KeySlot {
    int word;               // CfgWord
    const char* action;     // CONFIG.EXE's words where it has them
    const char* tip;
};
extern const KeySlot KEY_SLOTS[];
extern const int KEY_SLOT_COUNT;

// Every PC set-1 scancode CONFIG.EXE could assign, by scancode.
const std::vector<int>& AssignableKeys();

// "Space", "Left Shift", "Up / Keypad 8" ...; "Scancode 0xNN" for anything else.
wxString KeyName(int scancode);

// The set-1 scancode of a key press (grey keys give the keypad code, as the game's keyboard handler
// treats them), or 0 if it isn't a key the game knows.
int ScancodeOf(const wxKeyEvent& event);
