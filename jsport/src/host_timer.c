#include "host_int.h"

/* Retrace k starts at host_start_ns + k * HOST_RETRACE_NUM / HOST_RETRACE_DEN ns. */
#define VBLANK_NS 1430000ull             /* 45 blank lines x 31.78 us (vertical total 525, 480 shown) */

static Uint64 retrace_at(Uint64 k) { return host_start_ns + k * HOST_RETRACE_NUM / HOST_RETRACE_DEN; }

uint64_t host_retrace_count(void)
{
    return (SDL_GetTicksNS() - host_start_ns) * HOST_RETRACE_DEN / HOST_RETRACE_NUM;
}

bool host_in_vretrace(void)
{
    Uint64 now = SDL_GetTicksNS();
    return now - retrace_at(host_retrace_count()) < VBLANK_NS;
}

static Uint64 last_presented = ~0ull;

/* PORT (developer aid, JS_VCLOCK=1): a clock that advances by one retrace period per retrace the game consumes,
 * so scripted keys (JS_KEYS) and JS_QUIT_AFTER follow game time even when the host runs slower than real time. */
static double vclock;
#define RETRACE_S ((double)HOST_RETRACE_NUM / (double)HOST_RETRACE_DEN / 1e9)
double host_vclock(void) { return vclock; }

void host_idle(void)
{
    host_pump();
    Uint64 k = host_retrace_count();
    if (k != last_presented) {
        vclock += (last_presented != ~0ull && k > last_presented) ? (double)(k - last_presented) * RETRACE_S : RETRACE_S;
        last_presented = k;
        host_present();
    }
    SDL_DelayPrecise(SDL_NS_PER_MS / 2);
}

uint64_t host_wait_vretrace(void)
{
    last_presented = host_retrace_count();
    vclock += RETRACE_S;
    host_present();
    Uint64 next = host_retrace_count() + 1;             /* the first boundary after now */
    Uint64 due = retrace_at(next);
    for (;;) {
        host_pump();
        Uint64 now = SDL_GetTicksNS();
        if (now >= due) break;
        SDL_DelayPrecise(SDL_min(due - now, SDL_NS_PER_MS * 2));
    }
    return next;
}
