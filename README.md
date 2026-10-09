# JetStrike Enhanced

*JetStrike* (1994, Shadow Software / Rasputin Software, DOS CD-ROM edition) on SDL3 with a larger playfield. The
game still draws its original pixels at 1:1, but the mission view is 640×360 instead of 320×240, so you see twice
as much of the map across and more of the sky and ground. It is built on the faithful C port
[jetstrike-sdl3](https://github.com/kylofon/jetstrike-sdl3) and reads the original game's files at run time. The
original data is not redistributed: you need your own copy of the DOS CD-ROM version.

## How to play (Windows)

You need the installed files of the DOS CD version (`JS_CDROM.EXE`, `INTRO\INTRO.EXE` and the `DATA`, `GFX`,
`MAP`, `MISC` and `PLANE` folders) and, for the music, the CD image (`.cue` with `.img`/`.bin`). They are not
included.

1. Download `jsenh-…-win64.zip` from the [latest release](https://github.com/kylofon/jetstrike-enhanced/releases/latest).
2. Put your game files in a folder named `Game`.
3. Copy all files from the zip into the folder that holds `Game`, so that `JetStrikeEnhanced.exe` sits next to it:

   ```text
   JetStrikeEnhanced\
   ├── Game\                    <- your original game files (JS_CDROM.EXE, INTRO\, DATA\, GFX\, MAP\, MISC\, PLANE\)
   │   └── MUSIC\               <- the CD music, made by the launcher's "Rip CD music..." (optional)
   ├── JetStrikeEnhanced.exe    <- the launcher
   ├── jsenh.exe                <- the game
   ├── SDL3.dll
   └── (the other files from the zip)
   ```

4. Double-click `JetStrikeEnhanced.exe`. To get the music, press **Rip CD music…** once and choose the CD image's
   `.cue` file: the 14 CD audio tracks are written to `Game\MUSIC`. Choose the view size, options and keys, and
   press **Play**. (`jsenh.exe` also starts on its own with `Game` beside it.)

Keep the folder somewhere you can write, such as Documents, not Program Files: the game writes `JS.CFG` and the
saved games (`js_save.000` … `js_save.009`) in the game folder. If Windows says "Windows protected your PC", click
**More info**, then **Run anyway**. Alt+Enter switches to full screen.

## What is different from the original

* **A larger view.** The mission screen is 640×360 by default (a 640×295 playfield above the original HUD panel,
  which is centred). The launcher offers 320×240 (the original), 480×270, 640×360, 640×480, 960×540 or a custom
  size (`--view WxH`: width a multiple of 16 from 320 to 960, height 240 to 540). The window is the view times a
  whole number, as large as fits the desktop.
* The plane stays centred across the wider view and the camera never goes below the original's bottom edge. The
  parallax backdrop wraps across the wider view.
* "On screen" grows with the view: units, bullets, explosions, messages and the edges where airbase vehicles
  appear follow the new view edges. Distances measured from the player (enemy AI, missile lock, refuelling) are
  unchanged, so the rules are the same; a wider view can show more enemies at once.
* Enemy shells are drawn on the playfield anywhere in the view (the original drew them on the HUD page), and
  ground units follow the map's tiles instead of reading the drawn screen.
* The menus, briefings, story screens and the intro are the original 320×240 screens.
* Sound effects play at 3 906 Hz, the rate the original really programs.
* `--view 320x240` gives the original view, pixel for pixel the same as jetstrike-sdl3.

## The launcher

`JetStrikeEnhanced.exe` replaces the original's `CONFIG.EXE` and starts the game (`jsport/launcher/README.md`):

* **Game files**: the game folder and `jsenh.exe`, with a check that the files are there, and whether the CD
  music has been ripped. **Rip CD music…** writes the tracks from a CD image (`.cue` + raw `.img`/`.bin`).
* **JS.CFG**: CD music, sound effects, detail (parallax backdrops), joystick, and the 18 game keys, saved in the
  original's `JS.CFG` format.
* **Port options**: view size, window size, full screen, skip the intro.

Settings are remembered in `%APPDATA%\JetStrikeEnhanced\settings.ini`.

## Controls

The flight keys are set in the launcher (the original keys: arrows to fly, Space to fire the gun, Alt / Ctrl the
left / right weapon, Enter switches hover / agile, U undercarriage, E eject, P pause, Tab autothrottle, B
briefing, keypad `*` + arrows look around, Backspace follow view, Shifts change page in the selection screens).
Fixed keys: 1 … 9, 0 throttle; F1 … F10 save / load slot at the briefing; Space / Enter confirm; land and stop at
your base and hold Down to rearm; Esc or D ends the intro. A gamepad works as the joystick.

## How it was made

[PLAN.md](PLAN.md) has the Enhanced phases and decisions; [PORT_PLAN.md](PORT_PLAN.md) is the faithful port's
plan. `FORMATS.md` and `port/formats/` (file formats), `port/RE_GUIDE.md` (executable map), `port/spec/` (the
subsystem specs the C code follows function by function), `port/QUIRKS.md` (the original's bugs), and
`jsport/PORTING.md` (the port's structure, building and testing). `tools/snapcheck` checks that the 320×240 view
still matches the faithful port frame for frame.

## Building

Windows with MSYS2 `mingw64` (gcc, ninja, `mingw-w64-x86_64-sdl3`, and for the launcher
`mingw-w64-x86_64-wxwidgets3.2-msw`):

```bash
cmake -S jsport -B jsport/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DJS_LAUNCHER=ON
cmake --build jsport/build
```

## Licence

The port's code is MIT licensed (`LICENSE`). JetStrike is © Shadow Software / Rasputin Software; none of its
files are included.
