# Original bugs and quirks: port policy

Each spec ends with its own numbered list (about 170 items in all). Spec sections: platform §10,
video §11, sound §11, game_flow §12, intro §6, level §10, player §11, weapons §10, enemies §16.

## Policy (user decision, 2026-10-02)

**The SDL3 port keeps every original bug as is.** Fixes belong to the later JetStrike Enhanced repo.
This includes the crash and file bugs: F10 saves to `js_save.00:`, the truck sprite name "11" fatal
load error, `Rand(-1)` and mixer divide by zero, the missing `fclose`, the unbounded sprite queue, the
intro's FRAME7 over-read (emulate the adjacent memory where the result depends on it), RNG side
effects, stale globals, partial swaps, out-of-bounds table reads (tables laid out contiguously as in
the exe), sticky fog, "PLAYER 1" twice, the 71 weapon records, the intro's Esc/D-only exit.

Not bugs, so handled by the port's platform layer:
- frame rate: the CPU benchmark is replaced by W=2 (59.94 Hz retrace, logic at 19.98 Hz);
- Sound Blaster rate: 19920 Hz default, 3906 Hz (the original's programmed rate) as a setting;
- DOS hardware leftovers (INT 8 not restored, DPMI, ports): not applicable under SDL.

Where an original bug would crash the port process (not just the game logic, e.g. a host divide by
zero or a write outside our memory), the port reproduces the original's visible outcome (fatal error
message / exit) instead of crashing, with a `/* PORT: */` note.

**Dead code** (no callers: 0x18237–0x19c6f, 0x332fb/0x33308, 0x3e705, 0x40f5a, `CheatKeys`) is not
ported; the addresses are listed in the specs.

## For JetStrike Enhanced
Fix list candidates: all items above, starting with the fog flag, "PLAYER 1", the kill tally, the
cluster bomblet vx, the F10 save name, the truck sprite name, the Sound Blaster slice offsets.
