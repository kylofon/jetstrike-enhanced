First build of JetStrike Enhanced (Windows x64): the SDL3 port of JetStrike with a larger playfield.

Unzip `jsenh-v0.1.0-win64.zip` into the folder that holds your game files in a folder named `Game`, then start
`JetStrikeEnhanced.exe` (the launcher) or `jsenh.exe`. The original game files are not included: you need your own
copy of the DOS CD-ROM version (and its CD image for the music).

| Download | Needs from your game folder |
|---|---|
| `jsenh-v0.1.0-win64.zip` | `JS_CDROM.EXE`, `INTRO\INTRO.EXE` and the `DATA`, `GFX`, `MAP`, `MISC`, `PLANE` folders; for the music the CD image (`.cue` + `.img`/`.bin`) |

- **A larger view**: missions are shown at 640×360 by default (a 640×295 playfield, the original 320-wide HUD
  panel centred below it), with the original pixels at 1:1. The launcher's **View size** offers 320×240 (the
  original), 480×270, 640×360, 640×480, 960×540 or a custom size (`--view WxH`). The window is the view times a
  whole number, as large as fits the desktop.
- The plane stays centred; the camera stops at the original's bottom edge; the parallax backdrop wraps across the
  wider view. Units, bullets, explosions, messages and the airbase vehicles' entry points follow the new view
  edges. Player-relative distances (enemy AI, missile lock, refuelling) are unchanged; a wider view can show more
  enemies at once.
- Enemy shells are drawn on the playfield anywhere in the view; ground units follow the map's tiles instead of
  reading the drawn screen; sprites are clipped to the playfield.
- Sound effects play at 3 906 Hz, the rate the original really programs (the Sound Blaster rate option of
  jetstrike-sdl3 is gone).
- Menus, briefings, story screens and the intro stay the original 320×240 screens. `--view 320x240` matches
  jetstrike-sdl3 v0.1.0 frame for frame.
- The game folder must be writable: `JS.CFG` and the saved games (`js_save.000` …) are written there. Launcher
  settings are kept apart from jetstrike-sdl3's, in `%APPDATA%\JetStrikeEnhanced`.
- First release: the wide view has not been play-tested through every mission type yet; please report anything
  odd at the screen edges in the issues.
- The zip includes `SDL3.dll` with `libiconv-2.dll`, and the launcher's wxWidgets DLLs with the libraries they
  load. Their licences and sources are listed in `licenses/THIRD-PARTY.txt`.

See the README for build instructions and how the port was made.
