# jsport: JetStrike SDL3 port

C port of `JS_CDROM.EXE` (JetStrike CD, 1994; Watcom C, DOS/4GW, flat 32-bit) on SDL3. The original
data is read at runtime from the game folder (`Game/`, not in the repository). Specs: `../port/spec/`,
formats: `../FORMATS.md` and `../port/formats/`, bug policy: `../port/QUIRKS.md` (every original bug is kept).

Status: phase 5 step D. `main` (0x146f4) runs the real front end (step A) and the mission (step B: `Mission_Setup`, the
frame loop of `Game_Run` in its original order (`Mission_Run`, frame.c), flight model, `Player_Update`, landing / ditching /
crash / ejection, HUD, MP2 triggers, objectives, terrain damage, engine sound, `WeaponSelect_Screen`). Step C added the
weapons (weapons.c: every fire kind, the gun, projectiles, flares, explosions, particles, recon photo pages). Step D
completes the mission: enemy aircraft (spawn, movement, AI, guns, missiles, bombing, kill scoring and bonus), enemy
missiles (with the flare decoy), ground gunships, enemy shells, ejected pilots, convoys (Enemy_SetupSpriteIds,
Truck_LoadSpx), the MP2 gun / flak / SAM emplacements, target-zone vehicles, commandos, the emplacement under
construction, the airbase (fuel truck, jeep, fire engine, rearm truck -> WeaponSelect_Screen, crew, runway repair, base-hit
losses), the bonus crate with its twelve handlers, the prize balloon, the alien abduction (enemies.c) and the support
aircraft (support.c: tow plane, Fat Albert, B52, ship, big bomber, ground pickup; Tanker_Update with refuelling). No stub
of the original is left: every function the frame loop calls is ported.

## Build

Same toolchain as `srport`: MSYS2 MinGW-w64 (gcc, ninja, cmake) with SDL3 from
`C:/msys64/mingw64/lib/cmake/SDL3` (`find_package(SDL3 CONFIG)`).

```
export PATH=/c/msys64/mingw64/bin:$PATH
cmake -S jsport -B jsport/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build jsport/build
```

The build copies `SDL3.dll` and what it loads (`libiconv-2.dll`, ...) next to `jsport.exe`
(`cmake/copy_dlls.cmake`). Warnings: `-Wall -Wextra` must stay clean.

## Run

```
jsport/build/jsport.exe [--game-dir DIR] [--scale N] [--fullscreen] [--sb-rate 19920|3906] [--lzw-dump OUTDIR]
```

| option | meaning |
|---|---|
| `--game-dir` | the original game folder (default `Game`); CD tracks in `DIR/MUSIC/TRACKnn.WAV` (`tools/cdrip.py`) |
| `--scale` | window size 320x240 x N (default 3); Alt+Enter toggles full screen |
| `--sb-rate` | Sound Blaster mixer rate: 19920 Hz as designed (default), 3906 = the rate the original actually programs (sound.md Q1) |
| `--lzw-dump` | developer check, see Verification |

## Source layout (`src/`)

| file | content |
|---|---|
| `main.c` | options, `--lzw-dump`, start-up |
| `dseg.c/.h` | the data-segment image: LE object 5 of `JS_CDROM.EXE` loaded at start-up, `D8/D16/D32/DS32/DSTR(addr)` accessors |
| `game.h`, `game_flow.c` | game_flow.md: `Game_Run`, menus, zones, save/load, briefing, plane select, debrief, end game |
| `level.c/.h` | level.md subset: map buffers, `Map_LoadMxp`, tile accessors, Bertha stamp; `Enemy_LoadSpx`, `Enemy_SetupSpriteIds` + `Truck_LoadSpx` (convoy set-up) |
| `host.c/.h`, `host_int.h` | host layer API, init / shutdown, event pump, case-insensitive file lookup, fatal errors |
| `host_timer.c` | virtual VGA retrace clock, 59.94 Hz (25.175 MHz / 800 / 525) |
| `host_video.c` | window, 320x240 presentation (4:3, square pixels), PNG snapshots |
| `host_input.c` | SDL keys -> set-1 scancode bytes (E0 prefixes, focus loss releases), scripted keys, mouse, gamepad |
| `host_audio.c` | SDL audio device: Sound Blaster mixer stream (u8 mono, pulled), CD music stream, WAV dump |
| `platform.c/.h` | `js_main` (= `main` 0x146f4), FatalError, JS.CFG, Kbd_ISR + key slots, input (ReadControls, PollMenu, WaitKey_*, GetFKey), joystick on the SDL gamepad, Watcom `rand` / `Rand`, helpers (platform.md) |
| `files.c/.h` | `Platform_Fopen` (DOS 8.3 truncation + case-insensitive open), `File_LoadWhole` |
| `lzw.c/.h` | `LZW_Unpack` 0x50000 (LE object 2), byte-identical to `tools/jsunpack.py` |
| `mission.h` | mission globals (player, level objects, HUD, engine) as named image macros; prototypes of the mission files |
| `frame.c` | game_flow.md §8: `Mission_Run` = the frame loop of `Game_Run` (93 steps in order, flight model of player.md §3 inlined), `Fog_Refresh` |
| `player.c` | player.md: tables, controls, `Player_Update`, glider, takeoff assist, ditching, engine fire, damage, ejection, sprite helpers, crash debris |
| `hud.c` | player.md §10: `Hud_DrawPanel`, messages, radar, target arrow, briefing overlay, lamps |
| `level_mission.c` | level.md: `Tileset_Load(Tlx)`, `Parallax_Load`, MP2 trigger (+ `Map_TriggerColumnAhead`), `Map_CraterAt` / `Map_DamageColumn`, objectives, carrier, Bertha, agent drop / crate, Aerolimits gates, runway light, base radar; the gun's `Ray_Trace` 0x4501e; `Level_DrawOverviewMap` and `Mission_CompleteScreen` |
| `engine.c` | sound.md §5: engine loop and pitch, warning sequence, mission sound triggers |
| `weaponsel.c` | game_flow.md §7.2: `WeaponSelect_Screen` and helpers |
| `weapons.c` | weapons.md: `Weapon_Fire` and the launch routines, `Player_Weapons`, `Projectiles_Update` + flight routines, bullets, flares, `Explosion_Damage` / `Explosion_Terrain` / debris; enemies.md §10.1 `Flamer_Update`, §14.1 particles, §15 `AgentSmoke_Update` |
| `enemies.c` | enemies.md: EnemyBomber_Spawn, EnemyAir_Update (+ Heading_TurnToward, Map_ScanAround, Sfx_RandomAmbient, the lock reticle), Enemy_DropBomb, EnemyMissiles / EnemyGround / EnemyShells / EnemyPilots / Convoy / TargetVehicles / Commandos / Building updates, MP2 Gun / Flak / SAM draw + fire, Airbase_Update, AirbaseCrew_Update, BaseRepair_Update, BaseHit_Losses, Bonus_Spawn / Bonus_Update / Bonus_Award + 12 handlers, Pickup_Update, Alien_Update |
| `support.c` | player.md §6 / §9.4: SupportAircraft_Update (tow plane, Fat Albert, B52, ship, big bomber, flares via weapons.c, ground pickup), Tanker_Update |
| `video.c/.h` | mode X model: `vram[0x40000]`, `g_Palette` (image) + separate `dac`, CRTC (start, pel pan, line compare); every video.md routine: blitters, sprite bank / queue, tiles + `Level_DrawBackground`, palette and fades, fonts and text, lines / rects, `Video_FlipPage`, `Pic_LoadHudPanel` |
| `pic.c/.h` | `Pic_LoadPax` 0x11a1a |
| `sound.c/.h` | SB 4-channel mixer, Sfx_* API, `CD_PlayTrack` / `CD_Stop` |
| `symbols.h` | generated by `tools/gen_symbols.py` from `port/js_symbols.csv`: `FN_<name>` (function addresses), `G_<name>` (global addresses, a leading `g_` dropped); for comments, traces and the data-segment image |

New subsystems get their own files (`level.c`, `player.c`, `weapons.c`, `enemies.c`, `hud.c`, `intro.c`, ...),
one per spec, with a header for the API other files use.

## Conventions

* **One C function per original function**, same name as in `port/js_symbols.csv`, preceded by a
  `/* 0xADDR Name */` comment (plus a one-line summary if useful). Same argument order and meaning; types
  as the spec gives them (`u8/s16/u32...` from `types.h`). Functions that are inlined in the original stay
  static helpers with a comment naming where they come from.
* **Globals**: plain C globals with the original's name (`g_Palette`, `g_KeyDown`), declared in the
  subsystem header with the address in a comment (`/* 0x82ED8 */`). Initial values as in the exe's data
  object. Structs that the original reads as raw memory get `#pragma pack(1)` and the original layout.
* **`/* PORT: ... */`** marks every deviation from the original: hardware replaced (ports, IRQs, DMA,
  MSCDEX), host-crash protection, things dropped (CD check, benchmark). Behaviour that is a bug of the
  original is **not** fixed (QUIRKS.md); it gets a comment naming the quirk.
* **Host crashes**: where the original would fault (divide by zero, NULL `FILE*`, writes outside memory),
  the port stops through `FatalError` / `host_fatal_code` with a message naming the original's failure
  (e.g. `Rand(-1)`, the mixer's rate divide). Writes outside VRAM are dropped (`vput`), as on the PC.
* **Timing**: every wait of the original is a retrace wait (`Video_WaitVSync`, `Video_SetStartAndPan`);
  `host_wait_vretrace` presents the frame, pumps events and sleeps to the next 59.94 Hz boundary. The CPU
  benchmark is not run: `g_VSyncWaits = 2` (logic at 19.98 Hz, platform.md §6.4). Nothing else paces the game.
* **Keyboard**: the host delivers set-1 bytes to `Kbd_ISR` (E0 prefixes included; the ISR ignores them as
  the original does). Key repeats are not delivered (equivalent, platform.md §3.3).
* **Files**: all game files go through `Platform_Fopen` / `File_LoadWhole` with the original's DOS names
  (`"data/jetstrike.spx"` opens `DATA/JETSTRIK.SPX`).
* **Sound**: the mixer runs on SDL's audio thread (`Mixer_Render`, sound.md §2.14); game-thread changes of
  `g_MixChan` go through `host_audio_lock/unlock` (inside `Mixer_SetChannel` etc.).
* **RNG**: `Rand` / `rand_js` / `srand_js` reproduce the Watcom LCG; keep every `Rand` call of the original,
  in order, including the ones whose result is unused.

## Data-segment image

Every non-pointer global of the original lives in `g_dseg`, a byte array that is LE object 5 of
`JS_CDROM.EXE` (0x80000-0x94750), read from the game folder at start-up by `Dseg_Load` (dseg.c: page map
and the internal fixups of the data object, the same logic as `tools/lefile.py`). Initial values, string
constants, tables and the layout are therefore the exe's own, and the original's out-of-bounds reads and
writes (sprite queue entry 256 over `g_Palette`, a JS.CFG key word > 0xff past `g_KeyDown`, `fgets(0x50)` into
0x28-byte name slots, the debrief `strncpy` without terminator, ...) hit the same neighbours.

```c
#define g_Mission   DS32(0x903E0)               /* named globals: a macro on their address (game.h, ...) */
DS32(0x90A08) = -1;                             /* unnamed ones in place, with the spec's address */
fopen(DSTR(0x80EE6), DSTR(0x80C77));            /* strings of the exe: "js.cfg", "rb" */
#define g_SmallCharset ((const char *)DSEG_PTR(D32(0x80011)))   /* a pointer stored in the image */
```

* Pointer-valued globals (malloc'd buffers: `g_PackBuf`, `g_MapGrid`, fonts, the sprite table
  `g_SpriteTab`, ...) stay C pointers outside the image (the image holds 32-bit DOS/4GW addresses). The sprite
  queue keeps the sprite table index where the original stored the header pointer.
* 0x1000 zero bytes follow the image (`DSEG_SLACK`); writes computed past that are dropped.
* `JS_CDROM.EXE` must be the CD version (object layout and a charset fixup are checked).
* GCC's `-Wrestrict` / `-Wstringop-truncation` are off: copies between two globals are copies inside one
  array, and `strncpy` is used as the original does.

## Developer aids (environment variables)

| variable | effect |
|---|---|
| `SDL_VIDEO_DRIVER=dummy`, `SDL_AUDIO_DRIVER=dummy` | headless run (no window, no sound device; timing unchanged) |
| `JS_SNAPSHOT_DIR=dir` | every presented frame >= `JS_SNAPSHOT_MS` (default 2000) ms after the previous one is saved as `dir/snapNNNN.png` |
| `JS_KEYS="<sec>:<xx>[+<xx>...][p\|r],..."` | scripted set-1 keys in hex at that many seconds after start (grey keys `e048`); a tap is held 0.2 s; `p` = press only, `r` = release only: `"2:39,5:01"` = Space at 2 s, Esc at 5 s; `"4:e048p,9:e048r"` holds Up for 5 s |
| `JS_QUIT_AFTER=sec` | exit after that many seconds |
| `JS_AUDIO_DUMP=file.wav` | record the mixer output (u8 mono at the SB rate) |
| `JS_SEED=n` | replaces `time(NULL)` in MainMenu's `srand` (reproducible runs) |
| `JS_VCLOCK=1` | JS_KEYS and JS_QUIT_AFTER count game time (retraces consumed x 1/59.94 s) instead of wall time, so scripted flights stay frame-exact when the host is slower than real time (PNG snapshots, traces) |
| `JS_MISSION=n` | the campaign (COMBAT) starts at mission record n of DATA/M0 (0-based); a record without a map name gets the map of the nearest earlier record preset as the loaded map |
| `JS_SFX_TRACE=1` | prints every `Sfx_Play(id, freq, vol)` call to stdout |
| `JS_TRACE=n` | prints the base / plane stats once and, every n mission frames, the player state and a line with the enemy counts (aircraft 0 position / damage, ground units, missiles, shells, convoy, vehicles, flares, bonus, score, kills, armour, engine fire, lock target, convoy vehicle 0) to stdout |

Headless example (from the repository root):

```
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy JS_SNAPSHOT_DIR=work/snap JS_SNAPSHOT_MS=1000 \
JS_KEYS="1.5:39,3:01" JS_AUDIO_DUMP=work/snap/mix.wav jsport/build/jsport.exe --game-dir Game
```

## Verification

* `python tools/lzw_check.py` runs `jsport --lzw-dump work/lzw_c` and compares every PAX/SPX/TLX/MXP/DX0/DX1
  file with `tools/jsunpack.py`: 187 files, all byte-identical (2026-10-02).
* Headless snapshots of the front end equal the decoded pictures (`tools/jsgfx.py all` -> `work/gfx/`) pixel
  for pixel apart from the text / sprites drawn over them: main menu (MISCON, rows 0..19 black, picture from
  row 20), story (COMBATCO), briefing (JETLOGO + the colour-2 underline), plane select (PLANECH + icons),
  end game (ENDGAME3, exact).
* Scripted walks (JS_KEYS, step A): campaign (story, briefing, plane select, crash debrief + end game,
  back to the menu), training, practice, load game (slot
  missing), Aerolympics options / briefing / plane select / player rotation, quit (credits printed).
* Missions (step B, JS_KEYS + JS_TRACE + snapshots): campaign mission 1 take-off ('0' full throttle, Down to
  rotate), climb, half loop (turn to the right), hop and landing to a stop on the runway, running off the runway
  end / diving into the sea (crash, damage messages, auto-eject, parachute, debrief), training LANDING (autothrottle)
  and NIGHT (night backdrop), F12 weapon select (pick a weapon, DONE, HUD redrawn). Engine loop audible in
  `JS_AUDIO_DUMP`, pitch rising with the throttle.
* Weapons (step C, JS_KEYS + JS_SEED=1 + snapshots + JS_SFX_TRACE): training BOMBING (gun bursts: ammo count, tracers,
  sfx 17 per frame; bombs: release sfx 5, pairs of bombs, explosions with fire / smoke particles and sfx 4/27 + 6,
  craters and tile replacement on the hill, score), rockets (sfx 8, pairs), Hellfire (one per press), cluster bomb (6
  bomblets, chain of explosions), JP233 / Porcupine dispensers (sfx 2 per even frame), flares (sprite 0xa0 behind the
  plane), drop tank / commando / flamer pod without faults, flying into the hill (crash particles, debris, auto-eject);
  campaign mission 1 hop with a camera pod: photo in the air, landing to a stop on the runway -> "RECON PHOTO 1" overview
  page, Space returns to the mission.
* Enemies / support aircraft (step D, JS_VCLOCK=1 + JS_SEED + JS_MISSION + JS_TRACE + snapshots + JS_SFX_TRACE):
  campaign record 10 (Junglemap2: one bomber with 2 bombs, enemy airbase, SAM-launching target vehicles): the bomber flies
  to raid the base, turns on the player, locks on, fires its gun (voice sfx 16/26) and missiles (armour hits); the player's
  automatic flare is released; target vehicles fire shells and SAMs; shooting the bomber down with the gun: explosion,
  `g_Kills` 1, +1000, "ENEMY AIRCRAFT SCRAMBLING" from the enemy base; record 2 (mission 3, the train convoy): strafing
  one car destroys the train (+3 x 250, objective cleared), landing on the runway to a stop -> "MISSION COMPLETED",
  jeep drives to the plane -> mission 4 briefing; record 0 (mission 1): Down held while parked -> the rearm truck drives to
  the plane, sfx 24 and WeaponSelect_Screen open through Airbase_Update; the tanker and the airbase vehicles / crew are
  drawn on the runway.
* `JS_AUDIO_DUMP` with Space at 1.5 s: one 0.95 s burst = 18989 samples at 19920 Hz (slice 5, `Sfx_Play(6, 12000, 0x20)`, shift 5).

## Not ported / PORT decisions so far

* Enemies (PORT): the enemy aircraft gun tracer divides by `abs(PY - y)` / `abs(PX - x)` (enemies.md Q4): kept, the port
  stops with a FatalError where the original takes a DOS/4GW divide exception. The shell dots (Q13) go to absolute VRAM as
  in the original, in colour 0xff (the original's colour byte is an unpushed stack byte). The target vehicles'
  `Video_ReadPixel` page argument is not pushed in the original: the port passes the back page. BaseHit_Losses' uninitialised
  `s` (Q21) keeps its previous value (static, starts 0).
* Weapons (PORT): the secondary-explosion arrays are bounded to 32 entries in `Explosion_Damage` as in `Map_CraterAt`;
  a bullet left of column 0 reads height 0 (the original reads the heap before the table); `Mission_CompleteScreen`
  reads MP2 column < 0 as empty and pumps events in its input waits.
* Pause and the weapon-select fire-release waits pump events (`Platform_Spin`).
* Busy loops on key memory (menu fire release, `Input_AnyKey`, `Input_GetFKey`) call `Platform_Spin`
  (events, present, 0.5 ms sleep).
* Joystick: `Joystick_Poll` reads the first SDL gamepad and places the counts around the JS.CFG thresholds;
  with the joystick off in JS.CFG (the shipped file) a connected gamepad still gives direction bits and fire.
* Save slot F10 (`js_save.00:`): loading says "Save file not found!" as on DOS; saving stops with a FatalError
  where the original writes through a NULL `FILE*` (QUIRKS.md).

* CD check / MSCDEX, the 2 MB memory probe, DPMI, INT 8 / INT 9 vectors, `Video_BenchmarkSpeed`.
* Sound Blaster: detection, DMA ring, IRQ, the 50 Hz chunk throttle (sound.md §2.13); the mono mixer
  formula is exact. GUS path not ported.
* `Sound_StopAll` clears the channels (the one stray byte per channel, Q11, is dropped).
