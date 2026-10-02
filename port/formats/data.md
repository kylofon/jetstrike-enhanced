# Game/DATA/ formats (plus JS.CFG and save games)

Source: `work/JS.bin` (JS_CDROM.EXE flat image), decompile in `port/decomp/js.c`. The dumper is
`tools/jsdata.py all` (writes `work/data/`). Confidence: **H** = read directly from the loader and
consumer, **M** = consumer seen and the meaning inferred from the code and the data, **L** = guess.

General rules
- None of the DATA files is LZW-packed. `jsunpack` is not needed here.
- Text files are opened `"r"` (Watcom text mode: CR/LF becomes LF) and read with `fscanf("%d")`, `fscanf("%d\n")` (this also eats
  the trailing whitespace) and `fgets(buf,n)`. After most `fgets` calls the game does `buf[strlen(buf)-1]=0` (written "chop" below).
  The binary files are opened `"rb"`.
- Amiga-origin binary data (M0..M3 params, GENDATA/GENDAT2, BERTHA*, MISC.Z) is **big-endian 16-bit**.
  JS.CFG and the save files are **little-endian** (the raw DOS memory image).
- File names: DOS4GW truncates names to 8.3 (`Trainingicons.tlx` opens `TRAINING.TLX`, `Cityicons` opens `CITYICON`). The port
  must emulate this: truncate the base name to 8 chars and match case-insensitively.

## Load order

| When | Function | Files |
|---|---|---|
| startup | main 0x146f4 | `js.cfg` (rb, fread 0x48) |
| Game_Run 0x1ba0c init | | `data/gendat3.dax` (text), `data/gendat2.dax` (File_LoadWhole), `data/misc`, `data/misc.z` (File_LoadWhole, kept in memory: ptrs 0x90324 / 0x9032c) |
| every MainMenu 0x469e7 | | `data/jets.n`, `data/weapons.dat`, `data/hudtext.dat`; when DAT_0009021c==0: `data/l1l2`, `data/gendatad.dax`, `data/gendata.dax` |
| Mission_LoadBriefing 0x2299a | | `data/M<mode>` record, `data/enemies` (only if the set changed), `data/<asc>` (Story_ShowAsc 0x23f6b), `data/bertha<X>` (Map_StampBertha 0x243d6) |
| first Mission_Debrief 0x2c9cb | Sarcasm_Load 0x2d2b8 | `data/sarcasm` |
| PlaneSelect | PilotRecord_Load 0x25add / Plane_SetupSprites 0x26256 | read MISC / MISC.Z from memory; `plane/<abk>.hd` |
| briefing F1..F10 / menu "load" | SaveGame_Write 0x3c367 / SaveGame_LoadMenu 0x3c0fb | `js_save.00N` |

---------------------------------------------------------------------------------------------------

## DATA/M0, M1, M2, M3: mission records (H)

`g_GameMode` picks the file: `"data/M" + ('0'+mode)`. 0 = campaign (150 records), 1 = training (10), 2 = practice (10),
3 = Aerolimits (20). Record `g_Mission` is at `g_Mission*0x1c2`. A record is 450 bytes:

| Off | Size | Field | Loaded into / use |
|---|---|---|---|
| 0x000 | 320 | briefing text, space padded | `g_BriefingText` 0x92992. Trimmed (`Str_TrimRight`), word-wrapped to 0x130 px by `Text_FitWidth`, drawn with Text_DrawSmall at x=8, y=row*8+0x19 starting at row 2 |
| 0x140 | 20 | map name (e.g. `Jetmap1`), space padded; **empty = keep the current map** | `g_MapName` 0x85148. A trailing digit is stripped into `g_MapVariant` 0x8fffc (checked twice, so the last digit wins). Files: `map/<name>1.val` (into g_MapVal, 0x13b8 B), `map/<name[:6]><NN>.mp2` and `.mxp` with NN = 2-digit variant. Reloaded only when the name differs from the loaded map |
| 0x154 | 20 | tileset name `xxxicons.abk`; empty = keep | copied to 0x84e48. The last 4 chars are overwritten with `.tlx` (`Jeticons.tlx`). Mission_Setup loads it when it differs from g_TilesetName 0x84b48 |
| 0x168 | 60 | 30 x BE16 params | `g_MissionParams` 0x91648 (byte-swapped in place: `p[i] = (p>>8)+(p<<8)`) |
| 0x1a4 | 20 | story .asc name, empty = none | Story_ShowAsc (skipped for the 2nd Aerolimits player, DAT_00090254!=0) |
| 0x1b8 | 8 | 4 x BE16 weapon indices | `g_MissionDefWeapons` 0x8e378[4]: [0]/[1] default load of rack 1/rack 2 (set into 0x8fbc8/0x8fbcc when a new mission is entered in modes 0-2). [2]/[3] fallbacks used when that rack ends up with 0 rounds. In Aerolimits [3] is the forced weapon and the values index weapon icons on the plane-select screen |
| 0x1c0 | 2 | padding (`"  "`) | unused |

Derived at load time: `g_MissionBonus` 0x90408 = (mission+1)*1000 (32000 in Aerolimits), shown as "MISSION BONUS n POINTS".

### Mission completion (Mission_CheckObjectives 0x44a5d, H)

The record holds no script. **Objective params are counters or positions that the game zeroes when the objective is done.**
A mission is complete (`g_MissionResult = DAT_0009025c+1`, HUD "MISSION COMPLETED") when every param `i < 22` with
`GENDATA.param_not_objective[i][aero]==0` (the table at 0x8f088, see GENDATA) is zero. With the shipped GENDATA the
objective params are **p00 p01 p02 p03 p04 p05 p06 p07 p09 p14**. All the others are configuration.

### The 30 parameters

`pNN` = word NN = address 0x91648+2*NN. Modes 0-2 unless noted. The M3 overrides follow the table.

| # | Addr | Name | Meaning | Conf |
|---|---|---|---|---|
| 0 | 0x91648 | p00_land | nonzero = "land" objective: FUN_00015957 (landing check) zeroes it when the plane lands (DAT_000902b0==1). `p00/100` goes to DAT_00090400; ==1 together with success means the campaign end. `p00==2` sets g_Lives=0 | M |
| 1 | 0x9164a | p01_recon_col | recon objective: the player tile column is within p01±1 **and** the player y is >= p02*16. Then p01=p02=0 and "RECON PHOTOS TAKEN" (Aero: "Mind Your Head!"). Radar marker at column p01 (Hud_DrawPanel) | H |
| 2 | 0x9164c | p02_recon_row | see p01 | H |
| 3 | 0x9164e | p03_target_x0 | Target spec, three forms. **(a) 1..2000**: destroy the targets in columns p03..p04. Each column counts when Map_FindTile finds tile 2 or 0xa0 (destroyed) below the surface, or when the column's ground height (val+0x400) is above the target row (val+0xbd0). Done when the count >= p07, then p03=p04=p07=0. **(b) 2001..4999**: steal-aircraft mission: column = p03-2000 (p04 is clamped), DAT_00090280=1. Landing at column+4 (FUN_00017be5) clears the flag. Not counted as destruction. **(c) >= 5000**: destroy tile ids (p03-5000)..(p03-5000+p04-1) anywhere on the map. Done when the number of remaining tiles <= the initial count - p07. Radar range marker p03..p04 (form a) | H |
| 4 | 0x91650 | p04_target_x1 | end column (a) / number of tile ids (c) | H |
| 5 | 0x91652 | p05_air_kills | enemy aircraft to shoot down. Decremented per kill in EnemyAir_Update | H |
| 6 | 0x91654 | p06_convoy_kills | convoy vehicles to destroy (Convoy_Update decrements). **> 1000**: escort mode (DAT_000905c0=1): `min(p06%1000, p09)` vehicles must survive (kept in 0x909d8) | M |
| 7 | 0x91656 | p07_targets_req | number of target columns/tiles needed for p03. Also the Bertha threshold (FUN_0001af0f): the Bertha keeps shelling while fewer than p07 of its tiles are destroyed. Aero: row of the 0x3e pad tiles | H |
| 8 | 0x91658 | p08_enemy_air | `p08/100+1` = ENEMIES record. `p08%100` = number of enemy aircraft still to spawn (re-spawned as they die; EnemyAir_Update / EnemyBomber_Spawn decrement it). Initial wave = min(p08%100, 2) (EnemyBomber_Spawn at load). Aero: row of the 0x3f pad tiles | H |
| 9 | 0x9165a | p09_convoy_count | number of convoy vehicles (Enemy_SetupSpriteIds: DAT_000905ac). **>= 1000**: flag DAT_0008ffe0 (convoy turns round and keeps going), count = p09%1000. Objective cleared by Convoy_Update/Waypoint_Update when the convoy is done | M |
| 10 | 0x9165c | p10_convoy_col | convoy start column (x = col*16+0x60+i*p18). **>= 1000**: `p10/1000` ground units (EnemyGround, 0x8d9d8..) placed at p03*8+i*64, then p10%=1000. Radar marker | H |
| 11 | 0x9165e | p11_enemy_spawn_col | enemy aircraft spawn x = p11*16 ± rand(600), clamped to [200, (w-1)*16-200], y = -800 | H |
| 12 | 0x91660 | p12_enemy_wake_col | the enemy stays passive until the player is past column p12 or within 2000 px, then p12=0 | H |
| 13 | 0x91662 | p13_convoy_kind | hi byte: vehicle bank `plane/truck<'a'+hi>.spx` (Truck_LoadSpx 0x3ff4d) and behaviour (1 = shuttles back and forth, 2 = water vehicles: no ground following, start state 0). lo byte: hit-point multiplier, hp = lo * GENDAT2.vehicle[type].hp | H/M |
| 14 | 0x91664 | p14_pickup_col | pickup/agent column. Object x = p14*16. y = dropped onto the ground (first row with attr<0x7f). Radar marker when p24<500. Cleared on pickup (Player_DeathAndLanding) | H |
| 15 | 0x91666 | p15_convoy_types_a | 4 nibbles, LSB first: GENDAT2 vehicle type of convoy vehicles 0..3 (`(p[15+i/4] >> (i%4*4)) & 15`) | H |
| 16 | 0x91668 | p16_convoy_types_b | vehicle types 4..7 | H |
| 17 | 0x9166a | p17_convoy_row | convoy ground y. Values < 0x46 are a row (y=row*16-1), otherwise pixels. Convoy_Update keeps the vehicles within p17..p17+4 | H |
| 18 | 0x9166c | p18_convoy_spacing | pixels between convoy vehicles | H |
| 19 | 0x9166e | p19_bertha | `p19/1000` = letter (file `data/bertha<'A'+n>`), `p19%1000` = column. Stamped at load. Bertha_Update FUN_0001af0f runs 1/101 per frame and clears p19 once destroyed | H |
| 20 | 0x91670 | p20_ceiling_row | while the player's tile row < p20 the bomber-raid counter rises faster (Game_Run). Also cleared when tile 2/0xa0 is found in column p20 (Mission_CheckObjectives). Values are 40..62 | M |
| 21 | 0x91672 | p21_target_marker | nonzero: the target arrow / guided weapons aim at the middle of p03..p04 (y 0x3e0) | M |
| 22 | 0x91674 | p22_target_flag | ==1 with p03 (form a): special handling in FUN_00018bc8 / FUN_000438f2 (projectile kind 7) | L |
| 23 | 0x91676 | p23_convoy_flag | Convoy_Update terrain-row offset. ==1: the convoy turns at attr<0x82 | L |
| 24 | 0x91678 | p24_pickup_sprite | pickup sprite = p24%500. 500..999: carried by convoy vehicle `p14-1` (agent rides the vehicle). >1000: appears when the convoy is stopped (p06==0). 0xca/0xac = agent (animated, DAT_00090a38=1). Becomes 0xcd once picked up | M |
| 25 | 0x9167a | p25_weather | 0: 1-in-20 random fog. 1: **night** (DAT_0009035c: no parallax, dark sky 0x00a, night flash palette, Runway_Update lights). 2: fog always ("FOG WARNING !") | H |
| 26 | 0x9167c | p26_enemy_base_col | enemy airfield column (< 2000): bombers and the scramble spawn at x=p26*16-0x40. 0 = none (lets the "scramble" path run in Game_Run) | H |
| 27 | 0x9167e | p27_enemy_base_row | its row (y=row*16-3). Values > 63 are pixels (>>4) | H |
| 28 | 0x91680 | p28_bertha_row | top row of the Bertha stamp | H |
| 29 | 0x91682 | p29_misc | `p29/1000` -> DAT_00090148 (random event in FUN_00014923, overwritten by plane stat 64 on plane select). `p29%1000` -> 0x8deb0/0x8deb4 when nonzero | L |

**Aerolimits (M3) overrides** (Mission_LoadBriefing): p15..p22 are 4 (x,y) gate tile pairs (copied <<4 into 0x91484/0x91498, then
zeroed). The gates are flown in order (Waypoint_Update, "GATE PASSED !", "COURSE COMPLETE"). If gate 0 exists, p09=1 (objective).
p04/p05 = crate x/y tiles (p05=0: y=-0x400, falls in), p06 -> 0x9091c, p24 -> crate sprite (then p24=0). Crate_Update
("TARGET NABBED !") clears p04..p06. p03 = landing pad column: rows p07/p08 get tiles 0x3e (±3) and 0x3f (±5), then the range
becomes p03-5..p03+5. The pad is scored (4-|i-5|)*2500 per column. 0x8f088 column [1] is used for the objective mask.

---------------------------------------------------------------------------------------------------

## DATA/ENEMIES (H/M)

Binary, 7-byte records (10 in the file). Record `p08/100 + 1` is loaded when it differs from the last loaded one (0x90700):

| Byte | Global | Meaning |
|---|---|---|
| 0 | 0x906e0 g_EnemySkill | 0..6: fire probability/accuracy (Rand(4) < skill+1, Rand(6) > 4-skill, ...) |
| 1 | 0x906d8 g_EnemyGunDamage | systems damaged per gun hit: DAT_000907f8 = b1+1 goes to Player_DamageSystems |
| 2 | 0x906f8 g_EnemyMissiles | air-to-air missiles per enemy aircraft (0x8fc00[i]) |
| 3 | 0x906b8 g_EnemyBombs | bombs per enemy aircraft (0x8e3d8[i]), dropped over the player's base (FUN_00017944) |
| 4 | 0x906f0 g_EnemyBombWeapon | weapons.dat index used for those bombs |
| 5 | 0x906cc g_EnemyBombRef | *16: bomber reference coordinate (bombing y / x threshold) (M/L) |
| 6 | 0x906e4 g_EnemySpxLetter | enemy sprite bank `plane/enemy<'a'+n>.spx` (Enemy_LoadSpx 0x3f991, slots 0x5a..0x6a, 0xdb, 0xc3..0xc8) |

---------------------------------------------------------------------------------------------------

## DATA/WEAPONS.DAT (H loader, M meanings)

Text. `fscanf("%d\n")` count (69). The loop runs **count+2 = 71** records (`i <= count+1`). Per record:

| Read | Into (index w) | Field |
|---|---|---|
| fgets(0x28), chop | g_WeaponNames 0x8c8d8 + w*0x28 | name |
| 6 x `%d` | g_WeaponType 0x8f168 + w*0x18 (6 ints) | k0 fire_kind, k1 arm_delay, k2 motor, k3 flight_kind, k4 detonate_eol, k5 w5_flag |
| `%d` | 0x921f4[w] | blast_a: Explosion_Damage arg 5 (damage/radius) -> projectile 0x92720 |
| `%d` | 0x91cf4[w] | blast_b: Explosion_Damage arg 6 -> 0x927c8 |
| `%d` | 0x920d8[w] | thrust/burn: projectile life counter 0x926b8 (missiles 12..200). For bombs it is the forward impulse (-4 = retarded "Drag/Snakeye") |
| `%d` | 0x92310[w] | icon sprite id (HUD/WeaponSelect) |
| `%d` | 0x91fbc[w] | rounds per rack load (0x8fcb0/0x8fcb4 = count on rack 1/2) |
| `%d` | 0x9242c[w] | weight (lb), subtracted from the load 0x8feb8 per shot |
| `%d\n` | 0x91e10[w] | rack multiplier: max = rack_mult * plane racks (0x8fc10/0x8fc14) |
| fgets(0xa0), chop | 0x877c8 + w*0xa0 | description |
| `%d` | g_WeaponStock 0x91b84[w] | campaign stock (999 = unlimited). Saved |
| `%d\n` | g_WeaponResupply 0x91a68[w] | resupply in tenths per mission: before every briefing `frac+=r; stock+=frac/10; frac%=10` (frac 0x9194c), skipped when stock>=999 |

Weapon_Fire 0x341e9 dispatches on fire_kind: 0/3/6/7 FireMissile, 1 FireCamera, 2 FireGuided, 4 DropMarker,
5 FireRepeat (gun pod), 8 ArmB, 9 FireFromStock, 10 FireBomb, 11 ArmA, 12 Flares_Add. On launch k0..k5 are copied to the projectile
(0x91384, 0x912b0, 0x914ac, 0x91330, 0x9125c, 0x913d8). Projectiles_Update: k1 = frames before the flight logic starts.
k2 = launch mode (0: FUN_00019574 drop, 1: FUN_00019669 powered). k3 = flight routine 0..13 (switch). k4==1: explode when the life
counter ends. k5==2: special handling below y 0x3d4 (water). The projectile sprite = GENDATA.proj_sprite_base[k0] + frame
(k0==10 uses the per-projectile sprite).

## DATA/JETS.N (H)

Text: `%d\n` count (59), then count+1 = **60** lines `fgets(0x50)` + chop into g_PlaneNames 0x86cb0 + i*0x28. **No stats.**
A name equal to "Alien Superfighter" sets g_PlaneUsed[i]=9999 (never selectable) and 0x90a04=i.

## DATA/MISC (H): 60 x 0xdc pilot/plane text records

Kept in memory (0x90324). Record = plane index-1 (PilotRecord_Load uses DAT_00090a58*0xdc-0xdc).

| Off | Size | Field |
|---|---|---|
| 0x00 | 20 | display name -> 0x8c3d8 |
| 0x14 | 20 | sprite file `xxx.abk` -> 0x8c458. Plane_SetupSprites replaces the extension: `plane/<name>.spx` (+ `.hd` for helicopters) |
| 0x28 | 120 | description -> 0x8c4d8 (PlaneSelect text) |
| 0xa0 | 20 | obfuscated string: `chr(((b-1)&0xff) ^ "Mixamatosis is Fun !"[i])` gives e.g. `200/1`, `8/10` -> 0x8c558. No reader found (L: availability info) |
| 0xb4 | 40 | spaces |

## DATA/MISC.Z (H layout, M/L meanings): 60 x 300-byte plane stats

Kept in memory (0x9032c). Plane_SetupSprites 0x26256 copies 121 BE16 words of record `plane-1` into int array
`g_PlaneStats` 0x91684[121] (words > 0x4f are sign-extended). Bytes 242..299 are unused (spaces).
- words 0..61: per-direction sprite frame map (1-based frame of the plane bank, bit 15 = mirrored), 0 = unused.
- words 62..120 are copied into gameplay globals by PlaneSelect_Screen 0x245a9 (Mission_Setup/LoadBriefing/Airbase_Update restore
  84,85,88,89,90,108 per mission):

| w | Dest | Note | w | Dest | Note |
|---|---|---|---|---|---|
| 62 | 0x90060 | Player_Update | 92 | 0x90610 | Turrets_Update |
| 63 | 0x90258 | | 93 | 0x903d4 | Player_Update |
| 64 | 0x90148 | random event chance (FUN_00014923) | 94 | 0x90558 = w%10, 0x901f8 = w/10 | gun power / gun type (Player_Weapons) |
| 65 | 0x90838 | Mission_ResetState | 95 | 0x90a34 | |
| 66 | 0x90358 | | 96 | 0x90958 | throttle/afterburner mode (Player_Throttle*) |
| 67 | 0x9008c | Player_Update (338 on one plane) | 97 | 0x9090c | |
| 68-72 | 0x90508, 0x905a4, 0x90560, 0x90564, 0x90850 | gun parameters (Player_Weapons/Weapon_FireFromStock). 0x90850 = gun power bonus | 98, 99 | 0x90a48, 0x90a4c | |
| 73 | 0x90188 | Player_ThrottleDown | 100 | 0x8ff0c | Mission_Setup |
| 74, 75 | 0x90450, 0x8ff9c | | 101, 102 | 0x8fc10, 0x8fc14 | racks on side 1 / side 2 |
| 76 | 0x8fee0 | landing (Player_DeathAndLanding) | 103 | 0x90500 | max total load (lb) |
| 77 | 0x9045c | plane class: score multiplier (kill = (c+1)*1000). c!=0 does not cost a life (stolen/special) | 104, 105 | 0x8fbc0, 0x8fbc4 | max load of rack 1 / 2 |
| 78 | 0x908d0 | Player_RotateA/B | 106 | 0x90388 | fuel/gun-pod capacity (FireRepeat: 0x905b8 - this) |
| 79 | 0x906b0 | built-in gun weapon index, 0xff = none | 107 | 0x90788 | pod weight/capacity (Weapon_Fire) |
| 80, 81 | 0x90090, 0x90780 | float = w/100 (flight model) | 108 | 0x91834 | armour; current armour 0x909d0 = w + armour bonus 0x909e0 |
| 82 | 0x903b0 | | 109, 110 | 0x901dc, 0x901e0 | ejection seat dx, dy (signed) |
| 83 | 0x90674 | float = w/100; 0 sets 0x90594 (hover-capable) | 111 | 0x906d0 | engine sound/kind (Engine_*, w%10==3 special) |
| 84, 85 | 0x907d8, 0x907dc | airbase/landing parameters | 112 | 0x903c0 | ammo (0x909c4 = w + ammo pods*100) |
| 86 | 0x907a0 | float = w/100 | 113 | 0x8ff20 | landing pitch (FUN_00015957: dir == w*4) |
| 87 | 0x90954 | >10 means -1 | 114-120 | 0x90644, 0x9006c, 0x8ffe4, 0x8ffe8, 0x90130, 0x90844, 0x90070 | |
| 88 | 0x90168 | rotation rate (Player_Rotate*) | | | |
| 89, 90 | 0x905dc, 0x8ff14 | Player_Update/Collide | 91 | 0x904cc | helicopter flag (70 uses; selects .hd collision) |

## DATA/L1L2 (H)

60 bytes, `rb`, one byte per plane -> g_PlaneLimit 0x9152c[60]: the number of airframes available in the campaign.
`>= 200` means unlimited. Availability = limit - g_PlaneUsed 0x90ecc[p]. PlaneSelect shows "TRASHED" (<=0), the number (<200)
or "LOTS !". After a successful campaign mission each plane flagged in 0x91140 adds +1 to used. Bonus crates can also add +1.

## DATA/HUDTEXT.DAT (H)

Text, `fgets(0x50)` + chop until EOF -> g_HudText 0x8a428 + i*0x50 (101 lines). Code refers to entries by address. Examples:
18 MISSION COMPLETED, 44..53 bonus names (= bonus type+43), 72 TRASHED, 79 "MISSION BRIEFING: MISSION", 80/81 MISSION BONUS/POINTS,
82 FOG WARNING !, 87 LOTS !. The full list is in `work/data/hudtext.txt`.

## DATA/GENDATAD.DAX (H)

Text, 16 x `fgets(0x28)` (**newline kept**) -> g_DamageMsgs 0x85c48 + i*0x50. Messages "--- ENGINE FIRE ---" etc., used by Player_DamageSystems.

## DATA/SARCASM (H)

Text, 28 x `fgets(0x50)` (newline kept) -> 0x863f0 + i*0x50. Loaded on the first debrief. Mission_Debrief picks lines:
0-3 generic crash, 4 "pull up" (random), 5-7 crashed into tile 0x82 (water), 8-11 tile 0x81, 12/13 (DAT_0009099c>99),
14 / 15-16 / 17 / 18 (ejected, 99), 19-20 (stalled on the ground), 21 (no lives left), 22-27 success (3 per DAT_00090a10 outcome).

## DATA/GENDATA.DAX (H layout)

Binary, 202 bytes. MainMenu byte-swaps 0x66 words in place (one past the file, harmless), then:
1. Colour rows until 0xFFFF -> 0x90fe8: rows of 10 ints. Word 0x0023 (`#`) ends a row (rest of the row = 0). These are Amiga 12-bit
   RGB flash sequences (0x423, 0xfff, 0xc68, ...). Pal_CycleEffects plays them on colour 0x40 for explosions in **night** missions
   (Explosion_Damage picks row = dmg/1000).
2. 9 words -> 0x92980[i + j*3] (3x3, transposed) (L: dir table).
3. 26 words -> 0x8f088[i*2] = **param_not_objective (campaign)**. 26 words -> 0x8f08a[i*2] = **(Aerolimits)**. Used for i<22.
4. 16 sin/cos values computed, no data consumed.
5. 16 words -> 0x92674 = projectile sprite base per fire_kind.
6. 6 words -> 0x8fc18, immediately overwritten with constants 0x36, 0x37, 0xaf, 0xb0, 0xb1, 0x45.
7. 1 trailing word (5), unused.

## DATA/GENDAT2.DAX (H layout)

Binary BE16, 180 bytes (File_LoadWhole into 0x849f0, Game_Run):
1. 16 x (sprite_a, sprite_b, hp) -> 0x8de38[i*2], 0x8de3c[i*2], 0x8e1d0[i]: **vehicle types** (convoy, p15/p16 nibbles):
   two sprite ids (facing left/right) and base hit points (x p13 lo byte).
2. 1 skipped word, 8 words -> 0x90e8c (NameEntry_Screen column table).
3. 10 words -> 0x9255c bonus crate sprites. Game_Run then **overwrites 0x92560..0x9258c** (indices 1..12) with constants
   0xd0, 0xc8, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xe0, 0xdc, 0x4e, 0x1fc. Only index 0 comes from the file.
4. 16 words -> 0x8fa78 (L: direction remap). 7 words -> 0x90eb0 radar zoom scales (1, 5, 10, 25, 50, 75, 100; Hud_UpdateRadar).

## DATA/GENDAT3.DAX (H layout, L meanings)

Text read by Game_Run with `fscanf("%d\n")` / `fgets(0x50)`. "a/b" = an interleaved pair per index:

| Count | Destination | Data |
|---|---|---|
| 5 | 0x8d8c0 / 0x8d8d4 | sprite id pairs (523/527 ...) |
| 18 | 0x8d818 / 0x8d860 | (dx,dy) offsets (Player_Weapons gun muzzle positions) |
| 6 | 0x8d8a8 | 255 127 32 0 0 0 |
| 4 lines | discarded | `MISSIONS1/`, `BASE/` x3 (Amiga paths) |
| 4 | 0x8d9f8 | Aerolimits rounds options 3/5/10/15 (AeroOptions_Menu) |
| 27 | 0x90b48[1..27] | |
| 4 | 0x8da88 | 0 6 12 18 (Enemy_SetupSpriteIds) |
| 3 | 0x90cd8 / 0x90be4 | parachute/ejection sprite offsets |
| 6 | 0x90cf8[1..6] | sprite ids 396.. (then [7..100] = min(i+0x2e, 0x4a)) |
| 5 | 0x90ce4 | mine sprite ids |
| 5 | 0x90cc4 / 0x90c84 | (FUN_0003afc0) |
| 8 | 0x8deb8 / 0x8e310 | pitch/collision table (Player_PitchUp/Down, Player_Collide) |
| 7 | 0x8ded8 / 0x8df00 | debris offsets (FUN_0003b18d) |
| 7 x (fgets title, fgets artist, `%d\n` track) | 0x86148+i*0x50, 0x86170+i*0x50, 0x90b18[i] | CD music credits. Entry 7 is hard-coded "Negative G"/"Adam F", track = entry 6 + 1 (15) |

## DATA/BERTHA? (H)

Binary: BE16 width, BE16 height, then w*h tile bytes, row-major. Map_StampBertha 0x243d6: letter = 'A'+p19/1000
(A..G, Z), placed at column p19%1000, row p28 with Map_SetTile. For each column, the ground-height byte (val+0x400) is lowered to
p28 when it is higher.

## DATA/*.ASC story screens (H)

Text: `%s` picture name (`COMBATCOMBI.ABK`; the last 4 chars are replaced by `.pax`, loaded from GFX to page 1), `%d` text x
(0x8fedc), `%d` text y (0x8fea0), `%d` (into scratch 0x90540, unused by the screen). Then all remaining lines are concatenated
(`fgets(0x50)` appended) and drawn once with Text_DrawSmall at (x, y+8+0xf0) (+0x28 in Aerolimits). The text has embedded
newlines. Colour 0xff = white (black in Aerolimits). Waits for a key.

## Game/JS.CFG (H)

`fread(g_Config 0x936b4, 0x48)`; the file is only 60 bytes (30 LE words), the rest stays 0. Written by CONFIG.EXE.

| Off | Word | Off | Word |
|---|---|---|---|
| +0x00 | CD music on (g_CDMusicOn) | +0x24 | joystick enabled (g_JoyEnabled) |
| +0x02 | sfx on (g_SfxOn) | +0x26..+0x2c | joystick calibration -> 0x84530, 0x84532, 0x84534, 0x8452c |
| +0x04..+0x22 | 16 scancodes copied by Kbd_ISR into word slots: E->0x84548, Enter->0x84556, A->0x84546, U->0x84558, L->0x8455e, D->0x84554, P->0x8454a, Tab->0x8454c, B->0x84544, Esc->0x84542, Up->0x84560, Down->0x8454e, Left->0x84552, Right->0x84564, LShift->0x84550, RShift->0x84562 | +0x2e | detail/parallax (g_DetailParallax) |
| | | +0x30 | fire (Space) -> 0x8455a |
| | | +0x32 / +0x34 | Alt / Ctrl (edge-triggered -> 0x8453c / 0x84540) |
| | | +0x36 | KP* -> 0x8453e; +0x38 Backspace -> g_KeySlots 0x8453a; +0x3a SB IRQ (g_SBIrq) |

## Save games `js_save.000`..`js_save.009` (H)

In the current directory. The name is `"js_save.000"` with the last char + key (F1..F10 = 1..10, so F10 gives `js_save.00:`).
Written when F1..F10 is held at the campaign briefing (mission>0, mode 0, after a story screen). Loaded from the main menu zone 5.
Little-endian, 311 bytes. **Single bytes are written from int globals (the low byte); on load only the low byte is replaced.**

| Off | Size | Global | Meaning |
|---|---|---|---|
| 0 | 1 | g_Lives 0x902f0 | lives |
| 1 | 1 | g_Mission 0x903e0 | mission index (0..149) |
| 2 | 1 | 0x904a8 | enemy aircraft kills |
| 3 | 1 | 0x90314 | auto-eject charges (bonus 4, +4) |
| 4 | 1 | 0x906a8 | fire extinguishers (bonus 0) |
| 5 | 1 | 0x909e4 | ammo pods (bonus 1, max 4; ammo +100 each) |
| 6 | 1 | 0x90078 | flag set in Game_Run (L) |
| 7 | 1 | 0x90710 | ECM pod (bonus 5) |
| 8 | 1 | 0x909e0 | armour bonus (bonus 6, max 4) |
| 9 | 1 | 0x90544 | fire-power bonus (bonus 8, max 3) |
| 10 | 1 | 0x90944 | bonus 9 flag (gun) |
| 11 | 4 | 0x90304 | next extra-aircraft score (250000, doubles) |
| 15 | 4 | 0x902d8 | next bonus-crate score (10000) |
| 19 | 4 | 0x902e4 | bonus score step (12000, grows by 1000) |
| 23 | 4 | 0x8e330 | score (player 1) |
| 27 | 71 x 2 | g_PlaneUsed 0x90ecc[71] | low 16 bits of each int |
| 169 | 71 x 2 | g_WeaponStock 0x91b84[71] | low 16 bits |

After loading: DAT_000903a4=g_Mission, 0x9023c=1, 0x9030c=1, 0x90184=0xb. The weapon resupply fraction 0x9194c is not saved.

**High scores:** no high-score file exists. Aerolimits scores (AeroScores_Screen, aoscores.pax) and the campaign score are only
kept in memory.

## PLANE/*.HD (out of scope, noted)

Plane_SetupSprites reads `plane/<abk base>.hd` if it exists (only the 8 helicopters/Harrier): 0x640 bytes, 400 BE32 (byte-swapped
per dword) into 0x84a34. Used by Player_Collide.
