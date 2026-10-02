// icon.cpp -- the app icon: a grey jet seen from above over a blue sea, in the game's VGA greys and
// blues, drawn at 16x16 and scaled by whole pixels so every size shows the same picture. app.ico
// holds the same drawing for Explorer; make_icon.py writes it.
#include "icon.h"

#include <wx/image.h>

namespace {

// '.' sea, '#' hull, 'W' wing shadow, 'c' canopy, 'o' afterburner.
const char* const ROWS[16] = {
    "................",
    ".......##.......",
    ".......##.......",
    "......#cc#......",
    "......#cc#......",
    "......####......",
    ".....######.....",
    "...##########...",
    ".##############.",
    ".WWWWWW##WWWWWW.",
    ".......##.......",
    ".......##.......",
    ".....######.....",
    "....##WWWW##....",
    ".......oo.......",
    ".......oo.......",
};

unsigned long Colour(char c) {
    switch (c) {
    case '#': return 0xAAAAAA;
    case 'W': return 0x555555;
    case 'c': return 0x55FFFF;
    case 'o': return 0xFFAA00;
    default: return 0x0055AA;
    }
}

wxImage JetTile() {
    wxImage tile(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const unsigned long rgb = Colour(ROWS[y][x]);
            tile.SetRGB(x, y, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
    return tile;
}

}  // namespace

wxBitmap AppBitmap(int size) { return wxBitmap(JetTile().Scale(size, size, wxIMAGE_QUALITY_NEAREST)); }

wxIconBundle AppIcons() {
    wxIconBundle icons;
    for (int size : {16, 20, 24, 32, 48, 64, 256}) {
        wxIcon icon;
        icon.CopyFromBitmap(AppBitmap(size));
        icons.AddIcon(icon);
    }
    return icons;
}
