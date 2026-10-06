# JetStrike Enhanced — plan

Built on the faithful port in [jetstrike-sdl3](https://github.com/kylofon/jetstrike-sdl3) (phases 0-5 done).
**Main goal: a bigger playfield.** The game still draws its original pixels at 1:1, but the view is larger
than the original 320×175 window, so you see more of the map around the plane. Second goal: apply the
bug-fix list from `port/QUIRKS.md` ("For JetStrike Enhanced").

The original: VGA mode X 320×240, with a **320×175 playfield** above a 65-line HUD (`display.pax`, 320×66).
Tiles are 16×16. The map is W×64 tiles (1024 px tall) and wraps horizontally. The parallax backdrop is
320×512. Game logic runs at 19.98 Hz.

## User to-do

What the agents need from you now, most urgent first. Agents add a row when they hand you a task and
remove it when it is done.

| Task | What to do | For | Time |
|---|---|---|---|
| — | Nothing yet. Next is E2.2 (below). | | |

## Status

- **Done:** E0.1 (repo setup: sdl3 history merged, binaries renamed), E0.2 (`tools/snapcheck`: 76 frames in 9 scenarios, deterministic),
  E1.1 (run-time VRAM layout from `--view`; 76/76 at 320×240), E1.2 (integer-scale presentation, desktop-fit
  window, front-end screens in a mission view), E1.3 (76/76 at 320×240 after E1.2), E2.1 (tile window and
  parallax at any view; 76/76 at 320×240), camera bottom limit + vertical box (part of E2.2), HUD centred (E4.1).
- **Next:** E2.2 (rest: blitter clipping, horizontal camera box / lead margin), Sonnet.
- Escalation rule: a subtask that fails twice on Sonnet → new Opus session with a 5-line note
  (symptom, file, what was tried). Never carry an old transcript over.

## Model guide

| Model (app model picker) | Effort | Use for | Relative cost |
|---|---|---|---|
| **Haiku 4.5** | low–medium | Running the snapshot check, regenerating outputs, packaging, release files, PLAN bookkeeping | 1× |
| **Sonnet 5.5** | medium | Default: harnesses, launcher/HUD/settings code, tagging screen-value sites against the spec, QUIRKS fixes | ~3× |
| **Opus 5.5** | high | Video model and stride/page rework, the wide renderer, collision rework, render interpolation, cross-module bugs, escalations | ~5× |

## How to run a subtask (user, every time)

1. Code tab → **New session** in `C:\Coding\JetStrikeEnhanced` (never continue an old session for a new
   subtask: the old context is re-billed on every turn).
2. Pick the model from the subtask table in the model picker; set the effort from the Model guide.
3. Send exactly: `Do E0.2 from PLAN.md.` (with the subtask id).
4. When the agent says it is done: glance at the commit (`git log -1 --stat`), do any U-check it asks
   for, then close the session.

## Ground rules
- **Same game, more view.** The rules, physics and AI distances measured from the player stay as they are.
  The only things that change are values that mean "the screen edge".
- **Original mode still works.** `--view 320x240` must stay pixel-identical to jetstrike-sdl3. This is the
  regression test for every phase.
- One setting, `view_w × view_h` (the whole window including the HUD), checked at startup. Width must be a
  multiple of 16, between 320 and about 960. Height must be between 240 and about 540.
- Front end (menus, briefings, pictures, intro): drawn at 320×240 and pillarboxed or integer-scaled. Only
  the in-mission view gets larger.

## Decisions (user, 2026-10-06)
1. **View 640×360** (playfield 640×295) is the target and the default; 320×240 stays as the regression mode.
2. **"On screen" grows with the view**: `IsOnScreen`, culling and spawn edges use the new view (no separate
   320-wide activation window). Player-relative AI/lock ranges stay as they are (§4).
3. **HUD**: the original 320×66 panel, centred, with side fill.
4. **Repo**: jetstrike-sdl3 history merged in (remote `upstream`); its plan is kept as `PORT_PLAN.md`.

## Resolution options
| View | Playfield | Visible vs original | Notes |
|---|---|---|---|
| 320×240 | 320×175 | 1× | original, regression mode |
| 480×270 (16:9) | 480×205 | 1.5× wide, 1.17× tall | keeps the game tight; 4× = 1920×1080 |
| **640×360 (16:9)** | 640×295 | **2× wide, 1.7× tall** | recommended; 3× = 1920×1080 |
| 640×480 (4:3) | 640×415 | 2×, 2.4× | shows a lot of sky; the backdrop runs out (512 rows) |

Width matters most in a side-scroller: you see threats sooner. Extra height mostly shows sky and ground.
The map is only 1024 px tall, so more than ~400 playfield rows shows the whole altitude band at once.

## What changes (from the code survey of jsport/src)

### 1. Video model (`video.h/.c`, `host_video.c`, `host.h`)
- `HOST_FRAME_W/H` 320/240 → runtime `view_w/view_h`. The window, texture and logical presentation all use
  these. The presentation switches size between front end (320×240) and mission.
- VRAM: the linear model `VIDX = ofs*4 + y*stride + x` already works with any stride. Make the stride
  `view_w + 64` (the original is 384 = 320+64; the 64 is the sprite wrap margin) and give it a larger buffer.
  Page bases come from the HUD height and the playfield height, replacing 0x18C0/0x78C0, `+0x600` and the
  `pagesel*0xC00` step. Keep the radar save area and menu pages after the play pages.
- Split line 175 → `view_h - 65`. The HUD start address (0x1EC0) is computed.
- `compose()`: unchanged in logic, it just uses the new stride and size.

### 2. Level renderer (`video.c` Level_DrawBackground, Tiles_DrawColumns*, frame.c camera)
- `g_TileWindow[16][24]` lives in the data-segment image at 0x831D8. Move it out to its own array,
  `(view_w/16 + 4) × (playfield_h/16 + 3)`, and make the draw loops (24×13, 22×13) use those sizes.
- Map bottom: the window reads rows 64+ as 0 and relies on the HUD to hide them. A taller view would show
  those rows, so clamp the read and draw solid ground (the last row repeated) below row 63.
- Camera: the player screen box `PlayerScrX` 0x20..0x120 (target 0xA0) and `PlayerScrY` 0x50..0xA0 scale
  with the view. Recommended: keep the same lead margin ahead of the plane and centre the rest. The camera Y
  clamp (−0x7D0..0x340) is tied to the map height: lower the top limit by the extra playfield height so the
  ground stays at the bottom.
- Parallax: 320 wide with no horizontal wrap (quirk Q4), 512 rows. Wider views need a `% 320` wrap per
  pixel. Taller views need the last rows extended, or the rows above repeated with sky colour. Keep the
  /4.1666 and /9.25 scroll rates.

### 3. Sprites (`video.c` Sprite_Queue / DrawQueue)
- The wrap offset `x + 0x180` becomes `x + stride`. Keep the `+16` y offset (the page's top tile row).
- Add real clipping to the blitters. The original relied on the 64-px margin and VRAM wrap. This is
  harmless at 320 and needed for wide views.

### 4. Gameplay values tied to the screen (the risky part; about 25 sites + `IsOnScreen`)
Two kinds of values. Look at each site and tag it in the code (`/* ENH: view */`):
- **"Is it on screen"**: change these to the new view. `IsOnScreen` (level_mission.c:94; 0x140, 200, −0x40;
  about 45 callers), explosion culling weapons.c:1515, bullet tracer weapons.c:294 (0x140/0xB0), enemy
  shells enemies.c:868, winch release support.c:347, off-screen tanker arrow support.c:451-472,
  frame.c:105, centred messages hud.c:128/164 and game_flow.c:1835-1961.
- **Spawn edges**: enemies.c:1417-1469 and level_mission.c:583 spawn ground units just outside the screen
  (`CamX+0x14A`, `-0x40`, `-0x154`). Move them to the new edges, otherwise units pop in on screen.
- **Not screen values, keep them**: player-relative AI and lock ranges (enemies.c:274/591/606/831/459/485,
  weapons.c:787/936, support.c:485), base windows frame.c:608/618/705 and the target window
  level_mission.c:485. These are gameplay distances that happen to be 0x140. Confirm each one against the
  spec before deciding.
- **Ground collision reads the page** (`Video_ReadPixel` on `g_BackPage`: enemies.c:1019/1285/1325,
  support.c:322). Replace it with a tile lookup that returns the same values (tile pixel from `g_TilePtrs`).
  The test is that it matches the page read at 320×240 for whole missions.
- Difficulty: a wider "on screen" area wakes up more enemies at once. Check this by play-testing. Fallback:
  keep a separate 320-wide **activation window** centred on the plane and use the view only for drawing.

### 5. HUD (`hud.c`, `video.c` Pic_LoadHudPanel)
- The panel is 320×66 with fixed coordinates. Options: (a) **centre it with side fill** (recommended to
  start), (b) stretch only the radar strip, (c) a new wider panel later (new art).
- The radar shows the whole map, so it does not depend on the view.

### 6. Launcher (`jsport/launcher`)
- Add a "View size" choice (presets from the table plus custom), passed as `--view WxH`. Scale now means
  the window integer scale for that view. The window should fit the desktop (pick the largest integer
  scale that fits).

## Subtasks

Model: H = Haiku, S = Sonnet, O = Opus. Size: S < 1 h of agent work, M = one session, L = one long session
(split it if the context gets large).

### Phase E0 — Repository
| Id | Task | Model | Size / Status |
|---|---|---|---|
| E0.1 | jetstrike-sdl3 history merged; binaries renamed (game `jsenh`, launcher `JetStrikeEnhanced`, own settings folder; CMake targets keep their names for clean upstream merges) | O | done 2026-10-06 |
| E0.2 | Reference snapshots at 320×240: headless (`SDL_VIDEO_DRIVER=dummy`), scripted keys, frame dumps for front end + one mission of each type, plus a compare script (`tools/snapcheck`) that prints only pass/fail counts | S | done 2026-10-06 (`python tools/snapcheck/snapcheck.py`, about 45 s; `--update` only from a known-good build) |

### Phase E1 — Runtime view size (§1–2; gate: E0.2 snapshots match byte for byte at 320×240)
| Id | Task | Model | Size |
|---|---|---|---|
| E1.1 | Video model: runtime `view_w × view_h`, stride, page layout and split, `--view` parsing and validation | O | done 2026-10-06 (see E1 notes) |
| E1.2 | Presentation and switching between the 320×240 front end and the mission view (pillarbox / integer scale) | S | done 2026-10-06 (see E1 notes) |
| E1.3 | Run the snapshot check at 320×240, fix-or-report diffs (escalate real diffs to Opus) | H | done 2026-10-06 (76/76, no diffs) |

E1 notes (from E1.1):
- Playfield = `view_h − 65` (the HUD shows 65 of its 66 rows), so 640×360 has a 295-row playfield.
- The layout lives in `vl` (`video.h`, computed by `layout_for` in `video.c`): stride `view_w + 64`, play page
  = tile rows covering 16 + 15 + playfield rows, + 48 rows for sprites; page A after the 66 HUD rows, page B
  after A, the HUD save area (radar/altimeter, was row 0x246) 4 rows after B; VRAM a power of two ≥ the
  original 0x40000. The whole game (front end too) runs on the view's stride; only the intro keeps the
  320×240 layout. Front-end frames hash identically at every view size (checked at 336×241, 480×270,
  640×360, 960×540).
- The screen is the view from `Pic_LoadHudPanel` until the next `Video_SetSplitLine` (front end), else
  320×240. `host_present` recreates the texture and letterboxed logical presentation when the size changes.
  Left for E1.2: window size (now view × `--scale`, no desktop fit), integer scaling, how the 320×240 screens
  sit in the window, and the screens shown in mission view that are really front-end style
  (`Mission_CompleteScreen` overview, `Mission_Debrief` text over the last frame).
- E1.2: the presentation is `SDL_LOGICAL_PRESENTATION_INTEGER_SCALE` (largest whole factor, borders around it);
  the window is view × `--scale`, lowered until it fits the desktop's usable area. `Video_ClassicScreen(true)`
  shows a 320×240 screen (HUD split at 175) while in a mission view: used by the `Mission_CompleteScreen`
  overview. `Mission_Debrief` text is centred on `vl.view_w / 2` over the last mission frame.
- Default view is still 320×240 (`VIEW_DEFAULT_W/H` in `main.c`); switch it to 640×360 when E2 lands
  (decision 1). `snapcheck.py` runs without `--view`, so it then needs `--view 320x240` by default.

### Phase E2 — Wide renderer (§3)
| Id | Task | Model | Size |
|---|---|---|---|
| E2.1 | Tile window, draw loops, parallax wrap and extension (sprite wrap `x + stride` already done in E1.1) | O | done 2026-10-06 (see E2 notes) |
| E2.2 | ~~Map bottom fill~~ (not needed: the camera stops at the original bottom edge), blitter clipping, camera box / lead margin (vertical done, horizontal left) | S | M |
| E2.3 | Snapshot check at 320×240 + headless dumps at 480 and 640 wide | H | S |
| U-wide | Look at the 640×360 dumps / play one mission: tiles, parallax, sprites at the edges | user | 10 min |

E2 notes (from E2.1):
- `g_TileWindow` (0x831D8) is out of the image: `tile_window` in `video.c`, `vl.tile_rows × vl.tile_cols`
  (`tile_cols` = stride / 16, `tile_rows` = (16 + 15 + playfield + 15) / 16; 24 × 13 at 320×240, 44 × 21 at
  640×360). Nothing else read the old address (0x831D8..0x83357; `g_Zones` starts at 0x83358).
  `Tiles_DrawColumns` draws the whole window, the parallax loop `tile_cols − 2` columns, as the original.
- Backdrop (`par_px`): at 320 wide it stays linear (Q4); wider views wrap x at 320 on the same line. At 240 high
  out-of-buffer reads stay 0; taller views repeat lines 0 and 511 (the bottom line fills in below the art at
  960×540 and low camera positions; the camera clamp in E2.2 decides how much of it shows).
- Camera (user, 2026-10-06: the view must not go below the original's bottom edge): `VIEW_EXTRA_ROWS`
  (= playfield − 175) and `CAM_Y_MAX` (= 0x340 − extra) in `video.h` replace every camera-max 0x340
  (`frame.c` flight move, step 6 clamp, eject camera; `player.c` eject and follow views). Screen-Y values tied
  to the lowest camera move down by the extra rows: runway start `0x9f`, the loop condition `0x9d`, the wreck
  tumble `0x9c`, the eject ground line `0x9f`. The player box is `0x50 + extra/2 .. 0xa0 + extra` (the plane
  keeps its place in the height), follow views centre on `0x58 + extra/2`. Mission start: `g_CamY =
  g_StallTopY − extra` (`g_StallTopY` keeps its world meaning). Map rows past 63 never show now.
- `IsOnScreen`: the bottom edge is `camY + 200 + extra` (else the plane and ground units at the bottom of a tall
  view are culled). The horizontal window is still the original one: E3.1.
- The backdrop line at the bottom of the view is the original's at the same bottom edge (`py` from
  `camY + extra`, minus extra); the extra rows show lines above it, line 0 repeated above the top of the art
  (plain sky at 960×540 high up).
- HUD (E4.1): `compose()` shows the 320-wide panel at `(view_w − 320) / 2` with colour 0 at the sides; all
  HUD drawing keeps its 320-wide coordinates.
- Build from Git Bash needs `PATH=/c/msys64/mingw64/bin:$PATH` (gcc fails silently without it, and
  `cmake --build` output then hides the failure behind the old exe).

### Phase E3 — Gameplay edges (§4)
| Id | Task | Model | Size |
|---|---|---|---|
| E3.1 | Tag every screen-value site (`/* ENH: view */`), confirm each against the spec; change `IsOnScreen`, culling and spawn edges | S | M |
| E3.2 | Ground collision: tile lookup instead of the page read; verify it matches the page read at 320×240 over whole missions | O | M |
| U-play | Play-test every mission type (combat, city, jungle, bonus, training) at 480 and 640 wide; note difficulty | user | 45 min |

### Phase E4 — HUD and launcher (§5–6)
| Id | Task | Model | Size |
|---|---|---|---|
| E4.1 | HUD panel centred with side fill | S | done 2026-10-06 (colour 0 at the sides, in `compose()`) |
| E4.2 | Launcher "View size" (presets + custom) → `--view WxH`; desktop-fit integer scale | S | M |

### Phase E5 — Fixes
| Id | Task | Model | Size |
|---|---|---|---|
| E5.n | One session per 3–5 related items from `port/QUIRKS.md` "For JetStrike Enhanced", each behind the Enhanced build only; snapshot check after each | S | M each |

### Phase E6 — Release
| Id | Task | Model | Size |
|---|---|---|---|
| E6.1 | Zip, launcher, `RELEASE_NOTES.md`, `SHA256SUMS.txt`, same as the sdl3 release | H | S |
| U-release | Install from the zip on a clean folder, play one mission, approve publishing | user | 15 min |

### Later, optional
| Id | Task | Model | Size |
|---|---|---|---|
| L1 | 60 Hz render interpolation (logic stays 19.98 Hz; smooths the 3-frame scroll steps) | O | L |
| L2 | Wider HUD art panel (art by the user or sourced; code to load it) | S | M |
| L3 | Remastered sprites (art pipeline + loader) | S | M |

## Risks
- Hidden 320/384 assumptions in the asm-derived blitters. The E1 snapshot match catches these at the
  original size; sprite clipping catches them at wider sizes.
- Difficulty changes from the wider activation area (see the §4 fallback).
- Parallax art is only 512 rows. Very tall views show the filler.
- Data-segment layout: arrays moved out of `dseg` must not leave readers of the old address behind
  (grep `0x831D8`, sprite queue next to `g_Palette`).
