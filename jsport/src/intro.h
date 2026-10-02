#pragma once
/* The intro (INTRO.EXE, port/spec/intro.md): the separate program JS.BAT runs before JS_CDROM.EXE
 * (`cd intro / intro / cd .. / js_cdrom`), folded into the port (PLAN.md decision 4). Its data object is
 * read from Game/INTRO/INTRO.EXE (dseg.h, Iseg_Load); its files from Game/INTRO/. */
#include "types.h"

/* PORT: what running INTRO.EXE from JS.BAT amounts to: loads the intro's data image, runs main
 * (0x107bb -> Intro_Main 0x107c8) until Esc or D is released, then does what the program's exit did to the
 * machine (sound device and CD stopped, keyboard hook removed, memory freed). The window stays open for
 * the game. */
void Intro_Run(void);
