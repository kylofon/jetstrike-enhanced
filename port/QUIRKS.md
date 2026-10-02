# Original bugs and quirks: port policy

Each spec ends with its own numbered list (about 170 items in all); this page sets the policy and lists
the exceptions. Spec sections: platform §10, video §11, sound §11, game_flow §12, intro §6, level §10,
player §11, weapons §10, enemies §16.

## Policy (proposed, awaiting the user's decision)

**Keep by default.** Everything that changes what the player sees, hears or how the game plays stays as
the original does it, including the RNG side effects (extra `Rand()` calls from re-evaluated clamps,
empty stubs that still consume `Rand()`), the stale-global leaks, the partial array swaps on removal,
the out-of-bounds table reads (emulated by laying the tables out contiguously as in the exe), sticky
fog, the "PLAYER 1" twice, the wrong bonus message, the 1-kill tally, the 71 weapon records, the intro's
Esc/D-only exit.

**Fix, with a `/* PORT: */` note, only where the original would crash, corrupt files or depend on the
host machine:**

| Item | Spec | Fix |
|---|---|---|
| Frame rate set by the CPU benchmark | platform Q1 | fixed at W=2: 59.94 Hz retrace, logic and display at 19.98 Hz |
| SB output rate bug (3906 Hz programmed) | sound | 19920 Hz default, 3906 Hz as a setting (user decision, 2026-10-02) |
| Mixer divide by zero above frequency 20000 | sound | guard (cannot happen with shipped data; keep the octave jump) |
| `Rand(-1)` divides by zero | platform | guard |
| F10 save file `js_save.00:` | game_flow | write `js_save.009`? (needs a decision: it changes which slot F10 loads) |
| Missing `fclose` in the load menu | game_flow Q5 | close the file |
| `File_LoadWhole` / `Str_TrimRight` out-of-bounds | platform Q10 | bounds checks |
| Truck sprite name "11" for type >= 10 (fatal load error) | enemies §16 | keep the name; load failure non-fatal? (decision) |
| Sprite queue has no limit (entry 256 overwrites the palette) | video | cap at the array size |
| Intro reads FRAME7 40 bytes past its buffer | intro | pad the buffer (same output) |
| INT 8 never restored, hardware leftovers | platform | not applicable (SDL) |

**Dead code** (no callers: 0x18237–0x19c6f, 0x332fb/0x33308, 0x3e705, 0x40f5a, `CheatKeys`) is not
ported; the addresses are listed in the specs.

A later Enhanced repo can offer the gameplay fixes (e.g. fog flag, "PLAYER 1", kill tally, cluster
bomblet vx) as options.
