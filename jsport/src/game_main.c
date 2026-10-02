/* Placeholder game (phase 4): shows GFX/JETLOGO.PAX through the LZW unpacker, Pic_LoadPax and the mode X
 * model, plays the intro CD track (MUSIC/TRACK02.WAV) and a sound effect on Space. Esc quits.
 * Replaced by Game_Run 0x1ba0c in phase 5. */
#include "game_main.h"

#include <stdio.h>

#include "pic.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

int game_main(void)
{
    /* main 0x146f4, steps 2, 3, 5, 8 (the sprite bank and the benchmark come with the game). */
    Platform_AllocBuffers();
    Kbd_Install();
    Cfg_Load();
    Video_SetModeX();
    for (int i = 0; i < 0x300; i++) g_Palette[i] = 0;
    Pal_Upload(0, 0x100);

    /* MainMenu 0x469e7: config switches, sound start-up. */
    g_SfxOn = g_Config.sfx;
    if (g_SfxOn) Sound_Init();
    g_CDMusicOn = g_Config.cd_music;

    g_PicFullLoad = 1;
    Pic_LoadPax("jetlogo.pax", 0, 1);
    Video_ShowPage(0);
    CD_PlayTrack(2);

    bool space_was = false;
    while (!g_KeyDown[0x01]) {                      /* Esc */
        Video_WaitVSync();
        bool space = g_KeyDown[0x39];
        if (space && !space_was) Sfx_Play(6, 12000, 0x20, 0);   /* explosion_rumble, as Explosion_Damage */
        space_was = space;
    }

    CD_Stop();
    Sound_Shutdown();
    Kbd_Restore();
    Video_SetTextMode();
    return 0;
}
