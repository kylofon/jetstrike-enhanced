# JetStrike (the launcher)

`JetStrike.exe` starts `jsport`, the SDL3 port of JetStrike (PC CD version, 1994), with the options chosen in its
window, and takes the place of the original's `CONFIG.EXE` (PLAN.md, decision 5). It is built with
[wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own controls, like the Street Rod and Test Drive III
launchers it is modelled on.

## The window

* **Game files**
  * **Folder**: the folder with the original game's files (default `Game` beside the launcher).
  * **Program**: `jsport.exe` (default: beside the launcher).
  * The line below says whether the folder is complete or which file is missing (`JS_CDROM.EXE`,
    `INTRO\INTRO.EXE`, the `DATA`, `GFX`, `MAP`, `MISC` and `PLANE` folders). **Play** stays greyed out until the
    folder and the program are there.
  * **CD music**: whether `MUSIC\TRACK02.WAV` .. `TRACK15.WAV` are there; without them the game plays without
    music. **Rip CD music...** asks for the CD image's `.cue` file and writes the 14 tracks from the image it names
    (raw 2352-byte sectors, `.img` / `.bin`) to the game folder's `MUSIC` folder, with a progress bar. It is
    `tools/cdrip.py` in C++ (`rip.cpp`) and writes the same files byte for byte.
* **Game settings (JS.CFG)**: CD music, sound effects, detail (parallax) and joystick on / off.
* **Keys (JS.CFG)**: the 18 keys the game reads. Pick a key from the list or press **Set...** and the key itself
  (Esc, Tab and Enter count as keys there; Cancel keeps the old one). Grey arrows, Home, End and so on are the
  keypad's keys, and right Ctrl / Alt the left ones, as the game's keyboard handler sees them. A warning shows when
  one key does two things. **Original keys** puts back the shipped ones.
* **Port options**: **Sound Blaster rate** (`--sb-rate`: 19 920 Hz as designed, or 3 906 Hz, what the original
  actually programs), **View size** (`--view WxH`: a preset from 320 × 240, the original, to 960 × 540, or a custom size, width a
  multiple of 16; the default is 640 × 360), **Window size** (`--scale`: the view times 1 to 6; the game lowers it
  until it fits the desktop), **Start in full screen** (`--fullscreen`; Alt+Enter switches)
  and **Skip the intro** (`--no-intro`).
* **Other keys in the game**: the keys that can't be changed.
* **Play** starts the game (and writes `JS.CFG` first if the folder has none: the game stops without it); the
  launcher stays open. **About**: version, author and links.

### JS.CFG

The settings of the two JS.CFG boxes live in the game folder's `JS.CFG`, read when the launcher starts (or the
folder changes) and written at once on every change, in the original format: 60 bytes, 30 little-endian words
(`port/spec/platform.md` §3.1, `port/formats/data.md`), so the DOS game and `CONFIG.EXE` still read it. Keys are
stored as PC set-1 scancodes. Without a `JS.CFG` the launcher shows the shipped file's settings, the ones
`CONFIG.EXE` writes for the original keys (it has no defaults of its own: it asks for every key).

Kept as they are: the three key words the game never reads (A, L, D: CONFIG.EXE's "lock out weapons" and "toggle
smoke", never implemented) and the Sound Blaster IRQ. The joystick needs no calibration: the port reads the first
SDL gamepad (stick or D-pad, south button fires the guns), with the joystick on or off. When the joystick is
switched on and the file's four calibration words are unusable (zero, or not left < right and up < down), the
launcher writes the shipped file's values, which the port only uses as neutral thresholds.

### settings.ini

The rest (folder, program, port options, window position, the folder of the last `.cue`) is remembered in
`%APPDATA%\JetStrike\settings.ini` (`~/.config/jetstrike` on Linux). A folder or program left at its default is
stored empty, so it follows the launcher if the whole folder moves. Delete the file to go back to the defaults.

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`;
Debian and Ubuntu: `libwxgtk3.2-dev`). To build it beside `jsport.exe`, add `-DJS_LAUNCHER=ON` when configuring the
port:

```bash
cmake -S jsport -B jsport/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DJS_LAUNCHER=ON
cmake --build jsport/build
```

It also builds on its own (`cmake -S jsport/launcher -B jsport/launcher/build -G Ninja`); then choose the program
in the window or copy the launcher next to it.

On Windows the build copies every DLL the launcher and the game need beside them (`../cmake/copy_dlls.cmake`): the
two wxWidgets DLLs and the MSYS2 libraries they load. Keep them with the `.exe` files in a release. The C++ runtime
of the launcher itself is linked in.

## Developer checks

Both run without a window and print to the console they are started from (exit code 0 = OK):

```bash
jsport/build/JetStrike.exe --selftest-cfg Game/JS.CFG     # read, write back, compare byte for byte; list the settings
jsport/build/JetStrike.exe --rip Jetstrik/CD/Jetstrike.cue /tmp/music   # rip as the button does
```

2026-10-02: the shipped `JS.CFG` round-trips byte-identically (and equals the launcher's defaults); the rip of
`Jetstrik/CD/Jetstrike.cue` gives 14 files with the same SHA-256 as `tools/cdrip.py`'s `Game/MUSIC/*.WAV`.

## Files

* `app.cpp`: the wxWidgets application and the two developer checks.
* `launcher.h`, `launcher.cpp`: the window, the key capture and the About box.
* `jscfg.h`, `jscfg.cpp`: `JS.CFG`, the key slots, key names and the set-1 scancode of a key press.
* `rip.h`, `rip.cpp`: the `.cue` parser and the CD music rip.
* `game.h`, `game.cpp`: the file checks and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, a jet over the sea drawn in code. `make_icon.py` (Pillow) writes the same
  drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `CMakeLists.txt`: the build, standalone or from the main project.
