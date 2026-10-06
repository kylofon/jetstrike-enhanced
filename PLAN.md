# JetStrike Enhanced — plan

Built on the faithful port in [jetstrike-sdl3](https://github.com/kylofon/jetstrike-sdl3) (phases 0-5 done).
**Main goal: a bigger playfield.** The game still draws its original pixels at 1:1, but the view is larger
than the original 320×175 window, so you see more of the map around the plane. Second goal: apply the
bug-fix list from `port/QUIRKS.md` ("For JetStrike Enhanced").

The original: VGA mode X 320×240, with a **320×175 playfield** above a 65-line HUD (`display.pax`, 320×66).
Tiles are 16×16. The map is W×64 tiles (1024 px tall) and wraps horizontally. The parallax backdrop is
320×512. Game logic runs at 19.98 Hz.

## Ground rules
- **Same game, more view.** The rules, physics and AI distances measured from the player stay as they are.
  The only things that change are values that mean "the screen edge".
- **Original mode still works.** `--view 320x240` must stay pixel-identical to jetstrike-sdl3. This is the
  regression test for every phase.
- One setting, `view_w × view_h` (the whole window including the HUD), checked at startup. Width must be a
  multiple of 16, between 320 and about 960. Height must be between 240 and about 540.
- Front end (menus, briefings, pictures, intro): drawn at 320×240 and pillarboxed or integer-scaled. Only
  the in-mission view gets larger.

## Resolution options (decide in phase E0)
| View | Playfield | Visible vs original | Notes |
|---|---|---|---|
| 320×240 | 320×175 | 1× | original, regression mode |
| 480×270 (16:9) | 480×204 | 1.5× wide, 1.17× tall | keeps the game tight; 4× = 1920×1080 |
| **640×360 (16:9)** | 640×294 | **2× wide, 1.7× tall** | recommended; 3× = 1920×1080 |
| 640×480 (4:3) | 640×414 | 2×, 2.4× | shows a lot of sky; the backdrop runs out (512 rows) |

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

## Phases
- **E0 Repo setup.** Import the jetstrike-sdl3 history into this repo (`git remote add upstream`, merge
  master) so later fixes to the faithful port can be merged in. Rename the binaries to JetStrikeEnhanced.
  Decide the resolution presets. Make a set of reference snapshots at 320×240 (headless
  `SDL_VIDEO_DRIVER=dummy`, scripted keys) for regression tests.
- **E1 Runtime view size.** Video model, stride, pages, split, presentation, switching between front end
  and mission. At 320×240 the output must match the snapshots byte for byte.
- **E2 Wide renderer.** Tile window, draw loops, parallax wrap and extension, map bottom fill, sprite
  clipping, camera box. Visible milestone: wide missions draw correctly, with the logic unchanged.
- **E3 Gameplay edges.** Tag all ~70 sites, change the "on screen" and spawn values, and replace the
  page-read collision with tile lookups. Play-test every mission type (combat, city, jungle, bonus,
  training) at 480 and 640 wide.
- **E4 HUD and launcher.** Centred HUD, view setting, desktop-fit scaling.
- **E5 Fixes.** Bug-fix list from QUIRKS.md, each behind the Enhanced build only.
- **E6 Release.** Zip, launcher, `RELEASE_NOTES.md`, `SHA256SUMS.txt`, same as the sdl3 release.
- Later, optional: 60 Hz render interpolation (logic stays 19.98 Hz; smooths the 3-frame scroll steps),
  a wider HUD art panel, remastered sprites.

## Risks
- Hidden 320/384 assumptions in the asm-derived blitters. The E1 snapshot match catches these at the
  original size; sprite clipping catches them at wider sizes.
- Difficulty changes from the wider activation area (see the §4 fallback).
- Parallax art is only 512 rows. Very tall views show the filler.
- Data-segment layout: arrays moved out of `dseg` must not leave readers of the old address behind
  (grep `0x831D8`, sprite queue next to `g_Palette`).
