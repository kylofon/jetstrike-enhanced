// game.h -- what a game folder holds, where jsenh is, and starting it.
#pragma once

#include <wx/string.h>

// A game folder as the launcher sees it.
struct GameFolder {
    wxString missing;       // the first of the game's files or folders that isn't there, empty if all are
    bool haveCfg = false;   // JS.CFG is there
    wxString cfgPath;       // <folder>/JS.CFG (as found, or as the launcher writes it)
    wxString musicDir;      // <folder>/MUSIC (as found, or as the rip writes it)
    int tracks = 0;         // MUSIC/TRACK02..15.WAV present
    wxString firstMissingTrack;
};

const int FIRST_TRACK = 2, LAST_TRACK = 15;

GameFolder ReadGameFolder(const wxString& dir);

// The folder the launcher runs from.
wxString LauncherDir();

// Where things are by default: "Game" and jsenh(.exe) beside the launcher.
wxString DefaultGameDir();
wxString DefaultProgram();

struct GameOptions {
    wxString program;       // jsenh(.exe)
    wxString gameDir;       // --game-dir
    int scale = 3;          // --scale: the window is 320x240 times this
    bool fullscreen = false;  // --fullscreen
    int sbRate = 19920;     // --sb-rate 19920|3906
    bool noIntro = false;   // --no-intro
};

// Starts the game. On failure returns false and says why in `error`.
bool LaunchGame(const GameOptions& options, wxString& error);
