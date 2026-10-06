// jscfg.cpp -- JS.CFG and the key names.
#include "jscfg.h"

#include <wx/defs.h>
#include <wx/event.h>
#include <wx/ffile.h>
#include <wx/log.h>

#include <map>

namespace {

// Game/JS.CFG as shipped on the CD (the original keys as CONFIG.EXE writes them).
const uint16_t SHIPPED[CFG_WORDS] = {
    1, 1,                                                   // music, effects
    0x12, 0x1c, 0x1e, 0x16, 0x26, 0x20, 0x19, 0x0f,         // E Enter A U L D P Tab
    0x30, 0x01, 0x48, 0x50, 0x4b, 0x4d, 0x2a, 0x36,         // B Esc Up Down Left Right LShift RShift
    0,                                                      // joystick off
    0x115, 0x108, 0x423, 0x41c,                             // calibration: left, up, right, down
    1,                                                      // detail
    0x39, 0x38, 0x1d, 0x37, 0x0e,                           // Space Alt Ctrl KP* Backspace
    5,                                                      // SB IRQ
};

const std::map<int, const char*>& Names() {
    static const std::map<int, const char*> names = {
        {0x01, "Esc"}, {0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"}, {0x07, "6"},
        {0x08, "7"}, {0x09, "8"}, {0x0a, "9"}, {0x0b, "0"}, {0x0c, "-"}, {0x0d, "="}, {0x0e, "Backspace"},
        {0x0f, "Tab"}, {0x10, "Q"}, {0x11, "W"}, {0x12, "E"}, {0x13, "R"}, {0x14, "T"}, {0x15, "Y"},
        {0x16, "U"}, {0x17, "I"}, {0x18, "O"}, {0x19, "P"}, {0x1a, "["}, {0x1b, "]"}, {0x1c, "Enter"},
        {0x1d, "Ctrl"}, {0x1e, "A"}, {0x1f, "S"}, {0x20, "D"}, {0x21, "F"}, {0x22, "G"}, {0x23, "H"},
        {0x24, "J"}, {0x25, "K"}, {0x26, "L"}, {0x27, ";"}, {0x28, "'"}, {0x29, "`"}, {0x2a, "Left Shift"},
        {0x2b, "\\"}, {0x2c, "Z"}, {0x2d, "X"}, {0x2e, "C"}, {0x2f, "V"}, {0x30, "B"}, {0x31, "N"},
        {0x32, "M"}, {0x33, ","}, {0x34, "."}, {0x35, "/"}, {0x36, "Right Shift"}, {0x37, "Keypad *"},
        {0x38, "Alt"}, {0x39, "Space"}, {0x3a, "Caps Lock"}, {0x3b, "F1"}, {0x3c, "F2"}, {0x3d, "F3"},
        {0x3e, "F4"}, {0x3f, "F5"}, {0x40, "F6"}, {0x41, "F7"}, {0x42, "F8"}, {0x43, "F9"}, {0x44, "F10"},
        {0x45, "Num Lock"}, {0x46, "Scroll Lock"}, {0x47, "Home / Keypad 7"}, {0x48, "Up / Keypad 8"},
        {0x49, "Page Up / Keypad 9"}, {0x4a, "Keypad -"}, {0x4b, "Left / Keypad 4"}, {0x4c, "Keypad 5"},
        {0x4d, "Right / Keypad 6"}, {0x4e, "Keypad +"}, {0x4f, "End / Keypad 1"}, {0x50, "Down / Keypad 2"},
        {0x51, "Page Down / Keypad 3"}, {0x52, "Insert / Keypad 0"}, {0x53, "Delete / Keypad ."},
        {0x57, "F11"}, {0x58, "F12"},
    };
    return names;
}

// wxWidgets key codes -> set-1, where the raw scancode isn't available (outside Windows).
int FromKeyCode(int code) {
    static const char* const ROW1 = "QWERTYUIOP";
    static const char* const ROW2 = "ASDFGHJKL";
    static const char* const ROW3 = "ZXCVBNM";
    for (int i = 0; ROW1[i]; ++i) if (code == ROW1[i]) return 0x10 + i;
    for (int i = 0; ROW2[i]; ++i) if (code == ROW2[i]) return 0x1e + i;
    for (int i = 0; ROW3[i]; ++i) if (code == ROW3[i]) return 0x2c + i;
    if (code >= '1' && code <= '9') return 0x02 + (code - '1');
    if (code >= WXK_F1 && code <= WXK_F10) return 0x3b + (code - WXK_F1);
    switch (code) {
    case '0': return 0x0b;
    case '-': return 0x0c;
    case '=': return 0x0d;
    case '[': return 0x1a;
    case ']': return 0x1b;
    case ';': return 0x27;
    case '\'': return 0x28;
    case '`': return 0x29;
    case '\\': return 0x2b;
    case ',': return 0x33;
    case '.': return 0x34;
    case '/': case WXK_NUMPAD_DIVIDE: return 0x35;
    case WXK_ESCAPE: return 0x01;
    case WXK_BACK: return 0x0e;
    case WXK_TAB: return 0x0f;
    case WXK_RETURN: case WXK_NUMPAD_ENTER: return 0x1c;
    case WXK_CONTROL: return 0x1d;
    case WXK_SHIFT: return 0x2a;
    case WXK_NUMPAD_MULTIPLY: return 0x37;
    case WXK_ALT: return 0x38;
    case WXK_SPACE: return 0x39;
    case WXK_CAPITAL: return 0x3a;
    case WXK_NUMLOCK: return 0x45;
    case WXK_SCROLL: return 0x46;
    case WXK_HOME: case WXK_NUMPAD7: case WXK_NUMPAD_HOME: return 0x47;
    case WXK_UP: case WXK_NUMPAD8: case WXK_NUMPAD_UP: return 0x48;
    case WXK_PAGEUP: case WXK_NUMPAD9: case WXK_NUMPAD_PAGEUP: return 0x49;
    case WXK_NUMPAD_SUBTRACT: return 0x4a;
    case WXK_LEFT: case WXK_NUMPAD4: case WXK_NUMPAD_LEFT: return 0x4b;
    case WXK_NUMPAD5: case WXK_NUMPAD_BEGIN: return 0x4c;
    case WXK_RIGHT: case WXK_NUMPAD6: case WXK_NUMPAD_RIGHT: return 0x4d;
    case WXK_NUMPAD_ADD: return 0x4e;
    case WXK_END: case WXK_NUMPAD1: case WXK_NUMPAD_END: return 0x4f;
    case WXK_DOWN: case WXK_NUMPAD2: case WXK_NUMPAD_DOWN: return 0x50;
    case WXK_PAGEDOWN: case WXK_NUMPAD3: case WXK_NUMPAD_PAGEDOWN: return 0x51;
    case WXK_INSERT: case WXK_NUMPAD0: case WXK_NUMPAD_INSERT: return 0x52;
    case WXK_DELETE: case WXK_NUMPAD_DECIMAL: case WXK_NUMPAD_DELETE: return 0x53;
    case WXK_F11: return 0x57;
    case WXK_F12: return 0x58;
    default: return 0;
    }
}

}  // namespace

const KeySlot KEY_SLOTS[] = {
    {CFG_KEY0 + 10, "Rotate anti-clockwise / helicopter up", "The nose turns anti-clockwise; a helicopter climbs."},
    {CFG_KEY0 + 11, "Rotate clockwise / helicopter down", "The nose turns clockwise; a helicopter descends."},
    {CFG_KEY0 + 12, "Throttle down / helicopter left", "Less throttle; a helicopter flies left."},
    {CFG_KEY0 + 13, "Throttle up / helicopter right", "More throttle; a helicopter flies right."},
    {CFG_KEY0 + 14, "Helicopter turn left", "Turns a helicopter round to face left."},
    {CFG_KEY0 + 15, "Helicopter turn right", "Turns a helicopter round to face right."},
    {CFG_GUNS, "Fire guns", "Held: the gun fires. Space and Enter confirm in menus whatever this is."},
    {CFG_FIRE_LEFT, "Fire left weapon", "One press, one shot from the left pylon."},
    {CFG_FIRE_RIGHT, "Fire right weapon", "One press, one shot from the right pylon."},
    {CFG_KEY0 + 3, "Undercarriage up / down", "Raises or lowers the landing gear."},
    {CFG_KEY0 + 7, "Autothrottle on / off", "Switches the autothrottle on or off."},
    {CFG_KEY0 + 1, "Agile (hover) mode", "Switches the agile mode of the aircraft that have one."},
    {CFG_LOOK, "Look around", "Held: look around."},
    {CFG_TARGET, "Change target", "Cycles the follow camera: B52, Fat Albert, ground force, enemy, weapon."},
    {CFG_KEY0 + 8, "Mission briefing", "Held: shows the mission briefing."},
    {CFG_KEY0 + 6, "Pause", "Pauses the game; any key goes on."},
    {CFG_KEY0 + 0, "Eject", "Leaves the aircraft."},
    {CFG_KEY0 + 9, "Self-destruct", "Ends the mission and costs every life."},
};
const int KEY_SLOT_COUNT = static_cast<int>(sizeof KEY_SLOTS / sizeof KEY_SLOTS[0]);

JsCfg DefaultCfg() {
    JsCfg cfg;
    for (int i = 0; i < CFG_WORDS; ++i) cfg[i] = SHIPPED[i];
    return cfg;
}

JsCfg CfgFromBytes(const uint8_t* b) {
    JsCfg cfg;
    for (int i = 0; i < CFG_WORDS; ++i) cfg[i] = static_cast<uint16_t>(b[2 * i] | (b[2 * i + 1] << 8));
    return cfg;
}

std::vector<uint8_t> CfgBytes(const JsCfg& cfg) {
    std::vector<uint8_t> bytes(CFG_SIZE);
    for (int i = 0; i < CFG_WORDS; ++i) {
        bytes[2 * i] = static_cast<uint8_t>(cfg[i] & 0xFF);
        bytes[2 * i + 1] = static_cast<uint8_t>(cfg[i] >> 8);
    }
    return bytes;
}

bool ReadCfg(const wxString& path, JsCfg& cfg, wxString& error) {
    wxFFile f;
    {
        wxLogNull quiet;
        if (!f.Open(path, "rb")) {
            error = "can't be opened";
            return false;
        }
    }
    uint8_t bytes[CFG_SIZE];
    if (f.Read(bytes, CFG_SIZE) != CFG_SIZE) {
        error = "is shorter than 60 bytes";
        return false;
    }
    cfg = CfgFromBytes(bytes);
    return true;
}

bool WriteCfg(const wxString& path, const JsCfg& cfg, wxString& error) {
    const std::vector<uint8_t> bytes = CfgBytes(cfg);
    wxFFile f;
    wxLogNull quiet;
    if (!f.Open(path, "wb") || f.Write(bytes.data(), bytes.size()) != bytes.size() || !f.Close()) {
        error = wxString::Format("Couldn't write %s.", path);
        return false;
    }
    return true;
}

bool FixCalibration(JsCfg& cfg) {
    if (cfg[CFG_JOY_LEFT] != 0 && cfg[CFG_JOY_UP] != 0 && cfg[CFG_JOY_LEFT] < cfg[CFG_JOY_RIGHT] &&
        cfg[CFG_JOY_UP] < cfg[CFG_JOY_DOWN] && cfg[CFG_JOY_RIGHT] < 0x8000 && cfg[CFG_JOY_DOWN] < 0x8000)
        return false;
    for (int w : {CFG_JOY_LEFT, CFG_JOY_UP, CFG_JOY_RIGHT, CFG_JOY_DOWN}) cfg[w] = SHIPPED[w];
    return true;
}

const std::vector<int>& AssignableKeys() {
    static const std::vector<int> keys = [] {
        std::vector<int> k;
        for (const auto& entry : Names()) k.push_back(entry.first);
        return k;
    }();
    return keys;
}

wxString KeyName(int scancode) {
    auto it = Names().find(scancode);
    return it != Names().end() ? wxString(it->second) : wxString::Format("Scancode 0x%02X", scancode);
}

int ScancodeOf(const wxKeyEvent& event) {
#ifdef __WXMSW__
    // lParam of WM_KEYDOWN: bits 16..23 hold the set-1 scancode, without the E0 prefix of the grey keys
    // (which the game's keyboard handler ignores as well: grey arrows are the keypad's).
    const int raw = static_cast<int>((event.GetRawKeyFlags() >> 16) & 0xFF);
    if (Names().count(raw)) return raw;
#endif
    const int code = FromKeyCode(event.GetKeyCode());
    return Names().count(code) ? code : 0;
}
