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

void host_idle(void)
{
    host_pump();
    Uint64 k = host_retrace_count();
    if (k != last_presented) { last_presented = k; host_present(); }
    SDL_DelayPrecise(SDL_NS_PER_MS / 2);
}

uint64_t host_wait_vretrace(void)
{
    last_presented = host_retrace_count();
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
