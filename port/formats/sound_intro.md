# Sound, intro and CD audio

Tools: `tools/jssound.py` (JETSOUND.AAF), `tools/jsintro.py` (INTRO/ data), `tools/cdrip.py` (CD audio).
Intro symbols: `port/symbols_game_intro.csv`.

## 1. MISC/JETSOUND.AAF (in-game sound effects)

### File layout
297594 bytes (0x48a7a). It is an **AMOS Professional sample bank** followed by headerless data:

| offset | content |
|---|---|
| 0x00 | `"AmBk"`, BE16 bank 5, BE16 flags 0, BE32 0x8002481e (low 28 bits = length after this field + 8 → bank ends at 0x24826) |
| 0x0c | `"Samples "` |
| 0x14 | BE16 count = 28, then 28 x BE32 offsets (relative to 0x14) |
| each | 8-byte name (`"New Samp"`), BE16 rate (10000, ignored), BE32 length, signed 8-bit data |
| 0x24826.. | raw signed 8-bit data, no headers (game slices 29..35) |

**The DOS game ignores all of this.** `Sound_Init` reads the whole file (0x48a7a bytes; 0x32dea for a 256 KB GUS),
for SB subtracts 0x80 from every byte (signed → unsigned), and plays slices from a hard-coded table of 35 lengths
at JS.bin `0x80434`:

```
5054 4186 834 3696 8020 18990 2216 2160 3580 13866 7942 8256 5850 4390 5324 46 6398
16394 4976 4850 46 5904 3174 46 46 4984 2688 5240 15904 10880 14070 6292 11660 48766 40466
```
Slice k (0-based) starts at `sum(len[0..k-1])` and plays `len[k]-1` bytes. `len[k]` = AMOS sample length + 14
(the per-sample header size), but the slices start at file offset 0, so on SB **each slice begins 148 bytes before
the real sample data** (the tail of the previous sample + its 14-byte header) and stops 134 bytes early. That is
what the original sounds like (a ~7 ms click at the start). GUS uploads the file to DRAM 0 and plays
`[start+0x50, start+len-1]` (Sfx_PlayChannel) or `[start+0x186, ...]` (Sfx_PlayVoice, loop start +2), so GUS offsets
differ again. For a port, either replicate the SB slicing (`jssound.py all`, faithful) or use the clean AMOS
samples (`jssound.py amos`, 28 samples) + the raw tail.

The 46-byte slices (ids 16, 21, 23, 24) are 32-byte AMOS dummies: effectively a click/silence.

### Mixer (Sound Blaster)
- DSP rate 19920 Hz (`SB_Detect(...,0x4dd0)`), 8-bit unsigned mono, auto-init DMA ring 0x4000 bytes.
- INT 8 at 50 Hz (`Timer_ISR_SBMixer` 0x310ba) fills 398 bytes per tick: buffer set to 0x80, then each of 4
  channels does `buf[i] += (u8)sample >> (8-vol)` (8-bit wraparound, no clipping), `src += step`.
- `vol` (shift 0..6) from the API volume v (0..0x3f): 0:v<=1, 1:v=2, 2:v<=4, 3:v<=8, 4:v<=16, 5:v<=32, 6:v>32.
  Each active channel therefore also adds a DC offset of `128>>(8-vol)`.
- `step = 40000 / ((20000 / max(f,8000)) * 20000) & 15` → 1 for f <= 10000, 2 for 10000 < f <= 20000.
  `Sfx_Play(id,freq,vol)` passes `f = freq/2`, so every in-game call plays at **step 1 = 19920 samples/s**
  (only freq > 20000 would give step 2; 25000 is used once, sfx 4 with DAT_8fff8==4). Pitch changes
  (`Sound_SetFreq` for the engine) are therefore inaudible on SB; on GUS they work (`fc = freq/19`).
- Loop flag per channel: at end, loop → restart, else channel cleared.
- Channels: 0 = engine (Sfx_PlayVoice(0,..., loop=1)); `Sfx_Play` round-robins channels 1,2,3 (no priorities,
  oldest is overwritten). `Sfx_PlayVoice(1, 1, DAT_909c0*15, vol, loop)` also uses channel 1 (looping cannon
  sound while DAT_909c0 > 0). `Sound_StopAll` resets all 4. GUS: 14 voices, Sfx_PlayChannel voices 1..13
  round robin; volume via table 0x804c0.
- `Sfx_Play`'s 4th argument (x position) is ignored (no panning).

### Effect table (Sfx id = slice+1)
| id | name | call sites (freq, vol) |
|---|---|---|
| 1 | gun_alt_enemyfire | Player_Weapons alt gun (14000,0x10); EnemyGround/Convoy fire (14000,0xc) |
| 2 | cannon | player gun ammo type 0 (20000-1000*(n%10),0x20); Weapon_FireSpecialA/B (20000,0x1e); looping on ch1 in Engine_Sfx |
| 3 | gun_report | paired with 1 (4500, 0x20/0x18) |
| 4 | explosion_a | explosions `Rand(1)*23+4` (4 or 27); Game_Run DAT_8fff8==4 (25000) |
| 5 | weapon_release | Weapon_Fire (6000,0x28) |
| 6 | explosion_rumble | Explosion_Damage big blast (12000-x/10,0x20) |
| 7 | event_81 | Game_Run, DAT_90034==0x81 (12000,0x30) |
| 8 | launch_thump | Weapon_FireCamera (4000), Player_Weapons out-of-ammo (3000), FUN_3978d (15000,0x2b) |
| 9 | player_hit | Game_Run after hit flash (10000) |
| 10 | warning_tone | FUN_3f945, repeats every 0x18 frames (8000) |
| 11-13 | warning_damage / _c / _d | queued via DAT_903fc = 0xb/0xc/0xd, played by Engine_SoundUpdate (8000) |
| 14 | repeat_counter | Game_Run while DAT_902ec counts down (9000) |
| 15 | system_damage | Player_DamageSystems (12000,0x20) |
| 16 | stub16 | 46 B; FUN_389b0 random 1/21 |
| 17 | cannon_heavy | player gun ammo type 1 |
| 18 | random_event | Game_Run random (5500) |
| 19 | gear_toggle | Player_Update, toggles DAT_8ff14 + HUD text (4500,0x1e) |
| 20 | debris | Explosion_Damage random 1/6 (4000,0x20) |
| 21 | stub21 | 46 B; Weapon_FireMissile |
| 22 | mine | Mines_Update random 1/51 |
| 23 | stub23 | 46 B; 40 frames after 34 |
| 24 | stub24 | 46 B; Airbase_Update before WeaponSelect |
| 25 | proximity | Player_Weapons near target (8000) |
| 26 | enemy_air_event | FUN_389b0 random 1/11 |
| 27 | explosion_b | second explosion variant |
| 29, 31, 32 | engine_1/2/3 | Engine_Sfx `Sfx_PlayVoice(0, 0x1c/0x1e/0x1f, 2500, 0x10, loop)` by plane class (1 / 2,12 / 3) |
| 34 | mission_event | FUN_14923 at mission events (4100,0x3a) |
| 28, 30, 33, 35 | unused | |

Names are from call context only (not listened to); ids 7, 11-14, 18, 25, 26, 34 especially are guesses.

## 2. INTRO.EXE

Started by `JS.BAT` (`cd intro / intro / cd .. / js_cdrom`). Separate LE program, own copy of the SB/GUS
drivers and MSCDEX code. Most game functions are a 13-byte stub (`__CHK`) falling into the body at +0xd;
the CSV names the stub address.

### Video
- `Video_SetModeX`: unchained 256-colour **320x240, 60 Hz**, 4 planes, line pitch 0x60 bytes = **384 px virtual
  width**, two pages at 0 and 0x5a00. Each frame: draw everything into the back page, then
  `Video_FlipScroll(scrollX, page)` sets CRTC start + pel panning and waits for vertical retrace (frame = 1/60 s).
- Palette: 192 colours embedded at INTRO.EXE 0x6036c: colours 0-31 stored as 4-bit (x4 → 6-bit), 32-191 as 8-bit
  (>>2 → 6-bit); 192-255 black. Sprites use 0-31, the background 32+.
- `Pal_Fade(0)`: 25 steps `pal*k/25` (k=1..25), one vsync each; `Pal_Fade(1)`: k=25..0.

### Data files
| file | used | format |
|---|---|---|
| INTRO.RAW | yes | 97 sprite blocks; block = w*h bytes, first 4 bytes BE16 w/16, BE16 h; game zeroes the header and draws w*h bytes from block+4 (so the last 4 bytes come from the next zeroed header), pixels masked `&0x1f`, 0 = transparent, chunky |
| INTRO.TIL | yes | 384 tiles of 16x16, 256 B each, planar: `tile[plane*0x40 + row*4 + col]`, x = col*4+plane |
| CITY.PTT | yes | 405 x (int32 x, int32 y, int32 colour): city lights |
| FRAME1-8.RAW | yes | chunky 320 x {21,40,86,151,180,203,217,240}, 0 transparent; drawn at y {0x6f,0x67,0x4c,0x2f,0x18,0xc,6,0} (expanding green explosion ring) |
| BIGFNT2.RAW + BIGFNT1.RAW | yes | concatenated (0xc80 + 0x9d0); glyph k of `ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?` is 16x9 chunky at `k*0x90 + (k<17 ? 0x10 : 0x20)` |
| INTRO.SAM | yes | 8 signed 8-bit slices, lengths at 0x6070d: 48973 48973 29747 40575 41493 20997 12401 15917 (sum = file size) |
| SMALLFNT.RAW, CHAR1.RAW | loaded, never drawn | CHAR1 is fread with 0x33f8 into a buffer whose pointer slot is offs[-1] of the sprite table |
| AOSET.RAW/.PAL | only dead menu code | 320x240 chunky + 6-bit palette |
| INTRO1-5.SPR, INTROFON.SPR | not referenced by any exe | leftovers (INTROFON looks like a 1-bpp font) |

### Sound in the intro
- `Intro_SoundInit`: GUS if present, else SB at **40000 Hz** with the same 4-channel mixer as the game, but
  refilled by polling (`Mixer_Update`) 4x per frame from the main loop (no timer ISR).
- `Intro_PlaySample(n)`: SB rate argument 12000 → step `40000/12000 = 3` → effective **13333 Hz**
  (GUS fc 0x12e ≈ 13 kHz, consistent). n=0..4: one-shot, volume shift 6, channels 1/2 alternating;
  n=6: channel 0 **looped**, volume 4 (ambient bed, 12401 bytes; GUS loops from +0x251c). n=5 and 7 are never
  played (no case) even though scene 5 calls `Intro_PlaySample(5)`.
- **CD audio: track 2** (`CD_InitAndPlayTrack2` right before the main loop; plays once, start..start of track 3).
  `CD_Stop` on exit. So the intro music is CD track 2 (5:52), and track 2 is also in the mission pool.

### Timeline (frame = 1/60 s)
Init: load all files, build sprite table, hook INT 9, mode X, palette black, draw the tile background
(24x16 map, identity) + city lights, `Pal_Fade(0)` (25 frames), `Intro_PlaySample(6)` (loop), start CD track 2.

Every frame of the main loop:
1. Flip/scroll. `scroll` (0..15) +1 every 3 frames; at 16 → `Tile_ScrollMapLeft` (each row rotates left one
   tile, panorama wraps every 384 px = 1152 frames) and scroll = 0. Everything is drawn at `x + scroll`.
2. Every 10 frames all city points x -= 1 (x<0 → 0x17f). Points drawn at `(scroll+x, 158+y-2)` only over
   colours 0/0x20-0x24 (`Video_PlotUnder`), if x < 0x170.
3. Redraw all 8 tile bands.
4. Logo (sprite 66, 256x59) centred at `((320-w)/2, (240-h)/2-12)` when `g_LogoShown` and no overlay.
5. FRAME overlay (only after scene 6→7): a counter steps every 4 frames: steps 1-5 draw FRAME1-5; 6-8 draw the
   logo + FRAME6-8; step 9 draws the logo, sets `g_LogoShown`, and calls `Intro_SoundShutdown` (all intro
   samples stop for good; later `Intro_PlaySample` calls are silent). After step 9 the overlay ends.
6. Scene state machine `g_Scene` (below).
7. Credits: message `g_CreditIndex` (0..10, starts at 0) shown for 200 frames, then 41 blank frames, next
   message (wraps 10 → 0). Big font, centred per line with the width table 0x6075d, `\` = newline (+12 px),
   advance 11 px (`I` 7, `W` 12, space 11). y = 100, or 150 while the logo/overlay is up or in scene 7.
   Messages: PC PROGRAMMING/TEAM HOI GAMES; PUBLISHED BY/RASPUTIN SOFTWARE/COPYRIGHT 1994; ORIGINAL DESIGN/
   SHADOW SOFTWARE; DESIGN/AARON FOTHERGILL; CODE/MARTIJN PIETERSE; ADDITIONAL CODE/PETER SCHAAP; GRAPHICS SOUND
   AND MUSIC/ADAM FOTHERGILL; INTRO BACKGROUND GRAPHICS/METIN SEVEN; ADDITIONAL SOUND EFFECTS/DIGITAL DOMAIN;
   MUSIC RECORDED AT/THE CUPBOARD BARNSTAPLE; PRESS ANY KEY/TO PLAY JETSTRIKE.
8. Key: ESC or scancode 0x20 ('D') *released* → leave loop. No other key exits ("PRESS ANY KEY" notwithstanding).

Scenes (sprite numbers = INTRO.RAW 1-based; x,y = top-left before adding scroll):
1. Two jets (sprite 1, 128x38) fly in from the left: A from x=-96,y=100 at +2/frame until x >= 100+jitter;
   B from x=-120,y=144 until x >= 86+jitter (jitter = `(rand%10)*(rand%22)/10`). y of each bobs between 97..100 /
   141..144 (random 1/22 start). When A stops: `PlaySample(0)` (once). After both stopped, 150 frames → 2.
2. `PlaySample(1)`. Every 3rd frame: A sprite 1→21 (sprite stops at 21), x -= vx, y -= vy; B starts animating once
   A's sprite > 4; vx grows by 1 per step after sprite 4, vy grows to 5 → both jets pull up and away and shrink.
   When A's sprite >= 20 they disappear; 50 steps later → 3.
3. `PlaySample(2)`. One jet, sprites 23..34 (96 px wide, turning towards the viewer), x from -96 at +3/frame,
   y=100; sprite +1 whenever x%5==1; y -= 1 per frame once sprite > 30 and x > 80. At x > 300 `PlaySample(3)`.
   Off-screen at x >= 321, then 80 frames → 4.
4. Sprite 1 from x=-96, y=100 at +4/frame; at x > 200 `PlaySample(4)`; off-screen, 30 frames → 5.
5. `PlaySample(5)` (silent). Sprite 51 (32x10) from x=0, y=100 at +4/frame; after x > 100 it emits 4 smoke
   puffs per frame (sprites 68-70, max 60 live) that drift up-left, age, and are recycled. Until x >= 401,
   then 50 frames → 6.
6. Every 2nd frame: sprite 35→49 at x from 0 with vx 16 decreasing to 0, y from 191 with vy decreasing by 2
   (by 1 once above y 150): a jet climbs out of the city; smoke trail interpolated 4 puffs per step
   (sprite 68 + min(3,(n-34)/3)). When the sprite index reaches 50 → 7 (starts the FRAME overlay, clears
   logo flag, restarts the credit timer).
7. Sprite 50..65 (random advance 1/3 per step), x from -64 at +3 every 2nd frame, y = 80. Off-screen
   (x > 320) + 50 frames → back to scene 1 (loop forever; the logo now stays on screen).

Exit: after the loop `Intro_SoundShutdown`, `CD_Stop`, `Pal_Fade(1)`; then `Intro_Cleanup` (another fade,
text mode, shutdown/stop again, restore INT 9, free).
Quirk: if ESC comes during the 41-frame gap between credits, the code falls into a leftover menu loop
(PLAYERS %d / GAMES %d / ABORT, mouse, MAINMENU/BETWEEN/AOSET/OPTIONS/PLANESEL screens) whose only reachable
action is another ESC/'D' → cleanup. A port can just exit.

## 3. CD audio

`tools/cdrip.py` reads `Jetstrik/CD/Jetstrike.cue/.img` (raw 2352-byte sectors; `.sub` is separate) and writes
`Game/MUSIC/TRACKNN.WAV` (44.1 kHz, 16-bit, stereo). Byte order checked: little-endian gives sample-to-sample
RMS differences ~2-3k vs ~27k byte-swapped (white noise), so the img data is plain CD-DA LE. Lengths are
INDEX 01 to next INDEX 01 (they include the ~2 s gap before the next track).

| track | length | use |
|---|---|---|
| 1 | 1602 sectors | data (ISO 9660) |
| 2 | 5:52.27 | **intro** (INTRO.EXE) and mission pool |
| 3 | 4:53.40 | mission pool |
| 4 | 3:10.19 | mission pool |
| 5 | 2:47.15 | mission pool |
| 6 | 5:42.08 | mission pool |
| 7-13 | 0:30.19 each | end game jingle: `CD_PlayTrack(endgameIndex + 7)`, endgameIndex 0..6 (ENDGAME0-6.PAX) |
| 14 | 4:28.27 | mission pool |
| 15 | 5:31.28 | mission pool |

Mission: `t = Rand(13)+2` (Rand(n) = rand()%(n+1), so 2..15) re-rolled while `t<2 || 7<=t<=13` → uniform over
{2,3,4,5,6,14,15}; started at every mission start, stopped at the end. Verified in Game_Run (js.c ~7545). No CD
music in menus/briefings. Plays once (no repeat).

Data track contents: DATA.JS, GFX.JS, INTRO.JS, MAINCD.JS, MAP.JS, MISC.JS, PLANE.JS (ARJ archives),
INSTALL.EXE, DOS4GW.EXE, README.TXT (Rasputin/Powerlabel, Dec 1994), DISK.ID. All 364 files in the ARJ
archives match the files in Game/ (same size and CRC32); nothing on the CD differs from Game/.
