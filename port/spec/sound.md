# Sound spec: Sound Blaster mixer, GUS path, sound effects, engine sound, CD audio, intro sound

Scope: everything between `MainMenu`'s `CD_Check`/`Sound_Init` and the last `Sound_Shutdown`, plus the
intro program's sample playback and CD track 2. Addresses are JS.bin (base 0x10000) unless prefixed `INTRO:`.
Names follow `port/js_symbols.csv`; new names are in `port/spec/sound_symbols.csv`.

Confidence tags: **[V]** verified against disassembly, **[L]** likely (decompile + partial check),
**[G]** guess.

Sample data: `Game/MISC/JETSOUND.AAF` (format in `port/formats/sound_intro.md` §1, decoder `tools/jssound.py`).
Music: `Game/MUSIC/TRACKNN.WAV` (NN = 02..15, ripped CD tracks; track 01 is data).

---

## 0. Summary of the important findings

1. **The SB sample rate actually programmed is not 19920 Hz.** `Sound_Init` calls `SB_SpeakerOn` (which sends
   DSP 0x40 + `g_SBTimeConst`) *before* `SB_SetRate` computes the time constant, and nothing sends it again.
   `g_SBTimeConst` (0x803b0) is 0 in the image, so the DSP is set to TC 0 = **3906.25 Hz** [V]. In addition
   `SB_SetRate` computes the TC with a broken 64-bit divide (would have been TC 142 = 8772 Hz, still not
   19920) [V]. The same two bugs are in INTRO.EXE. See §2.4 and open question 1. The port must pick a rate;
   recommended: make it a setting, default 19920 Hz ("as designed"), and offer 3906.25 Hz ("as coded").
2. **`SB_Detect` stores the DSP *minor* version** in `g_SBDspVer` (two `SB_ReadDSP` results written to the
   same variable) [V]. All "SB Pro / stereo" decisions use that value (> 2 or >= 3). SB 2.01 -> 1 (mono,
   single-cycle 0x14), SB Pro 3.02 -> 2 (mono, high-speed auto-init), **SB16 4.05 (DOSBox default) -> 5 ->
   stereo path** with a mixer bug on the right channel (§2.6).
3. Mixer: 4 channels, output byte = `0x80 + sum(sample_u8 >> (8 - shift))` mod 256 (no clipping, silence of
   an active channel still adds a DC offset) [V]. Channel 0 = engine (loop), channels 1..3 round-robin for
   `Sfx_Play` starting at 2 [V].
4. Rate step per channel is 1 or 2 only: `step = (40000 / ((20000 / max(f,8000)) * 20000)) & 15`, i.e. step 1
   for f <= 10000, step 2 for 10000 < f <= 20000, **divide-by-zero (CPU #DE) for f > 20000** [V].
   `Sfx_Play` halves freq first, so only `Sfx_Play(4, 25000)` gives step 2. The engine pitch functions call
   `Sound_SetFreq` with unhalved values, so **the SB engine sound jumps one octave when the computed
   frequency crosses 10000** (not "inaudible" as `FORMATS.md` says) [V].
5. Sfx id 33 (not 34) is the "mission event" sample (`FUN_00014923` calls `Sfx_Play(0x21,...)`) [V];
   `tools/jssound.py` and `port/formats/sound_intro.md` label slice 34 instead. Corrected table in §4.
6. CD music: one random track from {2,3,4,5,6,14,15} per mission, played **once** (no repeat, no status
   polling); end-game track 7 + endgame index (0..6). The RNG is consumed for the track pick even when CD
   music is off [V].

---

## 1. Data structures and globals

### 1.1 SB driver globals (obj1, 0x80394..0x803d3)

| addr | type | name | init | meaning |
|---|---|---|---|---|
| 0x80394 | u32 | g_SBBase | 0 | I/O base; 0 = autodetect 0x210..0x280 |
| 0x80398 | u32 | g_SBIrqNum | 5 | IRQ (Sound_Init passes JS.CFG +0x3a) |
| 0x8039c | u32 | g_SBDmaChan | 1 | DMA channel (never changed; page port 0x83 hard-coded) |
| 0x803a0 | u32 | g_SBDspVer | 0 | DSP **minor** version after detect (bug, §0.2) |
| 0x803a4 | u32 | g_SBRate | 20000 | requested rate (Sound_Init: 0x4dd0 = 19920) |
| 0x803a8 | u32 | (unnamed) | 0 | SB_SetRate: (rate/50)*125, never read |
| 0x803ac | u16 | (unnamed) | 0 | SB_SetRate: rate/50 = 398, never read |
| 0x803ae | u16 | g_MixChunk | **0x78 = 120** | bytes mixed per ISR call (constant, never written) |
| 0x803b0 | u8 | g_SBTimeConst | 0 | DSP time constant (sent once by SB_SpeakerOn, before it is computed) |
| 0x803b1 | u32 | (unnamed) | 0 | SB_SetRate: 0x369e940000/rate, never read |
| 0x803b5 / 0x803b7 | u16 | (unnamed) | 0x400 / 0 | b7 = b5*2-1, never read |
| 0x803b9 | u16 | g_DmaBufSize | 0x4000 | ring size |
| 0x803bb | u16 | g_DmaBufSeg | | real-mode segment of the ring (linear = seg*16) |
| 0x803bd | u16 | g_DmaAddrLo | | DMA offset within 64K page |
| 0x803bf | u16 | g_DmaPage | | DMA page (seg >> 12) |
| 0x803c1 | u16 | g_DmaCount | | size-1 = 0x3fff |
| 0x803c5 | u16 | g_MixWritePos | 0x14 | next byte offset to mix into |
| 0x803c7 | u16 | g_MixNextPos | 0 | write pos after this chunk |
| 0x803c9 | u16 | g_DmaBufLen | | = 0x4000 |
| 0x803cd | u16 | g_MixWrapFlag | 0 | chunk wraps around the ring end |
| 0x803cf | u16 | g_SBStopFlag | 0 | set by SB_SetStopFlag; AckIRQ then stops re-arming |
| 0x803d1 | u16 | g_SBStopped | 0 | set by AckIRQ when stop flag seen |
| 0x803d3 | u8 | g_SBOldPicMask | | port 0x21 before unmask |

### 1.2 Mixer channels (0x803d4, 4 x 21 bytes, packed) [V]

```c
#pragma pack(1)
struct MixChan {            // ch0 0x803d4, ch1 0x803e9, ch2 0x803fe, ch3 0x80413
    uint8_t *data;          // +0x00 sample start (unsigned 8-bit); NULL = idle
    uint32_t len;           // +0x04 bytes
    uint32_t pos;           // +0x08 byte offset of next sample (saved at end of each ISR call)
    uint32_t loop;          // +0x0c 1 = loop (restart at offset 0), anything else = one-shot
    uint8_t  shift;         // +0x10 volume shift 0..6 (output adds (s << shift) >> 8)
    uint32_t step;          // +0x11 source bytes advanced per output byte (1 or 2 in practice)
};
```
Initial image values: all zero except `shift` = 6 for each channel.

### 1.3 Sound globals (game side)

| addr | type | name | meaning |
|---|---|---|---|
| 0x80430 | u32 | g_SoundDevice | 0 none, 1 SB, 2 GUS |
| 0x80434 | u32[35] | g_SfxLens | slice lengths (see formats doc) |
| 0x804c0 | u16[64] | g_GusVolTable | GUS volume register per API volume 0..63 (0, 0xa000 .. 0xff80) |
| 0x80542 | u32 | g_GusVoiceRR | next GUS voice for Sfx_PlayChannel, init 2, range 2..13 |
| 0x80546 | u32 | g_SfxChanRR | last SB channel used by Sfx_PlayChannel, init 1 (first sound -> ch 2) |
| 0x8078c | u16 | g_SoundInitDone | MainMenu: Sound_Init done once |
| 0x8078e | u16 | g_CDCheckDone | MainMenu: CD_Check done once |
| 0x93384 | u32 | g_GusSliceLimit | GUS: number of playable slices (0x21 for 256K, 0x23 for 512K+); 0 on SB |
| 0x93388 | u8* | g_SampleData | SB: whole AAF, converted to unsigned |
| 0x9337c | u32 | g_DosBufSeg | DPMI 0x100 result (segment); also selector for free |
| 0x93380 | u32 | g_SBVector | IRQ+8 (also used as a loop counter in Sound_Init) |
| 0x9338c / 0x93390 | u32/u16 | g_OldSBVecOff/Sel | saved vector |
| 0x936b4 | u16 | g_Config+0 | JS.CFG word 0 = CD music on |
| 0x936b6 | u16 | (JS.CFG +2) | sfx on |
| 0x936ee | u16 | g_SBIrq | JS.CFG +0x3a |
| 0x936f8 | u32 | g_CDMusicOn | = zero-extended JS.CFG +0 (MainMenu, every call) |
| 0x936fc | u16 | g_SfxOn | = JS.CFG +2 (MainMenu, every call) |
| 0x936f4 | u32 | g_CDEndgameLatch | set to 1 when the end-game track starts; never cleared |

### 1.4 CD globals

| addr | type | name | meaning |
|---|---|---|---|
| 0x93344 | u32 | g_CDDrive | MSCDEX CX after 1500h = first CD drive letter (0=A) |
| 0x93348 | 0x32 B | g_DpmiRmRegs | DPMI real-mode call struct (EDI+0 ESI+4 EBP+8 EBX+0x10 EDX+0x14 ECX+0x18 EAX+0x1c ES+0x22) |
| 0x9322c | u32[] | g_CDTrackStart | [0] = lead-out (from disc info), [t] = start of track t in frames (M*4500+S*75+F, no 150 offset), [last+1] = lead-out |
| 0x9337a | u8 | g_CDLastTrack | |
| 0x9337b | u8 | g_CDFirstTrack | |
| 0x92b04 | u32 | g_CDMissionTrack | track picked for the mission |
| 0x907f4 | u32 | (endgame index) | 0..6, set by Mission_Debrief 0x2c9cb (see that spec) |

### 1.5 Engine / sound-trigger globals (names proposed here)

| addr | name | meaning |
|---|---|---|
| 0x90698 | g_EngineRev | smoothed engine value (drives pitch) |
| 0x9066c | g_EngineRevTarget | target computed each frame |
| 0x909c0 | g_CrashTimer | >0: falling/crash sequence counter (FUN_000164d2 increments to 100); drives looping sfx on ch1 |
| 0x903fc | g_PendingWarnSfx | queued warning sample id (0xb/0xc/0xd), 0 = none |
| 0x90404 | g_WarnSfxDelay | frames until the queued warning sample plays |
| 0x900f0 | g_SfxBusyTimer | small cooldown; >=5 suppresses explosion sounds |
| 0x900a4 | g_EngineSfxRequest | 1 = (re)start engine loop at next Engine_SoundUpdate |
| 0x907c8 | g_EngineSfxLastState | last value of 0x907fc seen by Engine_SoundUpdate (init -1 per mission) |

Other globals referenced (owned by other specs): 0x906d0 plane class, 0x907fc player-down/ejected flag,
0x8fef4 post-death counter, 0x90594, 0x905a0 (frame parity toggle), 0x90720 speed, 0x8ffd4 throttle,
0x907a8, 0x8ff2c, 0x901f0, 0x8e210 table, 0x90094 (float), 0x8ff40 (written by the pitch functions),
0x903a8 = g_CamX, 0x90214 = g_PlayerScrX.

---

## 2. Sound Blaster driver (obj1, hand-written asm, register calling convention unless noted)

### 2.1 SB_ResetDSP @ 0x305da [V]
`CF SB_ResetDSP(void)`: out(base+6,1); 0x28 x in(base+6); out(base+6,0); up to 100 tries: if
in(base+0xe)&0x80 and in(base+0xa)==0xAA -> CF=0 return; else CF=1.

### 2.2 SB_WriteDSP 0x3060f / SB_ReadDSP 0x30624 / SB_MixerWrite 0x30639 / SB_MixerRead 0x3065a [V]
Write: wait until in(base+0xc)&0x80==0, out(base+0xc, AL). Read: wait in(base+0xe)&0x80, return in(base+0xa).
MixerWrite: out(base+4, AL), 6 dummy reads, out(base+5, AH), 0x23 dummy reads. MixerRead (unused): reg AL,
returns in(base+5).

### 2.3 SB_Detect @ 0x30670 [V]
`int SB_Detect(dspver, base, unused, irq, rate)` (stack args). Each `mov [g], arg` is guarded by `je` on the
**caller's flags** (no instruction sets ZF first). The game's caller leaves ZF=0 (last flag op is
`cmp eax,0x200` with a `jl` taken), so all are stored: g_SBDspVer=0, g_SBBase=0, g_SBIrqNum=irq,
g_SBRate=rate. Then:
```
if (g_SBBase == 0) { for (g_SBBase = 0x210;; g_SBBase += 0x10) { if (reset ok) break;
                     if (g_SBBase >= 0x280) return 0; } }        // tries 0x210..0x280
else if (reset fails) return 0;
if (g_SBDspVer == 0) { WriteDSP(0xE1); g_SBDspVer = ReadDSP(); g_SBDspVer = ReadDSP(); } // BUG: minor
return 1;
```

### 2.4 SB_SetRate @ 0x30712 [V]
```
0x803ac = (u16)(g_SBRate / 50);                    // 398, unused
0x803a8 = 398 * 125;                               // unused
if (g_SBDspVer <= 2) q = (0x0000000F_00004240) / g_SBRate;       // 32-bit div of EDX:EAX
else                  q = (0x0000000F_00004240) / (g_SBRate*2);
g_SBTimeConst = (u8)(-(u8)q);
0x803b1 = 0x369e940000 / g_SBRate; 0x803b7 = 0x803b5*2-1;       // unused
```
Intended: `256 - 1000000/rate` (16-bit `DX:AX = 0x000F4240`), but `mov dx,0xf` + 32-bit `div` makes the
dividend 0xF00004240. For 19920 mono q=3234162 -> TC 142 (8772 Hz); stereo q=1617081 -> TC 71.
**Irrelevant in practice: the TC is never sent after this** (SB_SpeakerOn ran earlier). Port: do not emulate.

### 2.5 SB_SpeakerOn @ 0x3057c [V]
WriteDSP(0xD1); MixerWrite(reg 0x0e, value 0x31 if g_SBDspVer <= 2 else 0x33) (0x33 = SB Pro stereo bit);
MixerWrite(0x22, 0xff); MixerWrite(0x04, 0xff) (master and voice volume max); WriteDSP(0x40);
WriteDSP(g_SBTimeConst) (= 0 at this point -> 1000000/256 = **3906.25 Hz**); g_SBStopFlag = g_SBStopped = 0.

### 2.6 SB_SetupDmaBuffer @ 0x307c5 (stack arg seg) [V]
`g_DmaBufSeg = seg; if ((seg & 0xfff) && 0x400 <= 0x1000-(seg&0xfff)) { page = seg>>12; addrLo = (seg&0xfff)<<4; }
else { if (seg&0xfff) g_DmaBufSeg = (seg & 0xf000) + 0x1000; page = g_DmaBufSeg>>12; addrLo = 0; }`
g_DmaCount = 0x3fff, g_DmaBufLen = 0x4000. Quirk: `Sound_Init` fills 0x80 at the *original* seg, so when the
buffer is moved to the next 64K page part of it starts as uninitialised memory (a click at startup).

### 2.7 SB_StartDMA @ 0x30906 [V]
DMA: mask ch (out 0xa, ch|4), clear flip-flop, mode 0x59 (single, auto-init, read, ch1), address, page port
0x83, count 0x3fff, unmask. DSP: `g_SBDspVer <= 1`: 0x14, count lo, hi (8-bit single-cycle; AckIRQ re-arms it
on every IRQ). `== 2` or `>= 3`: 0x48, count lo, hi, 0x90 (8-bit high-speed auto-init). IRQ every 16 KB.

### 2.8 SB_AckIRQ @ 0x30875, SB_IRQ_ISR @ 0x31a31 [V]
ISR: `__GETDS`, call AckIRQ, iretd. AckIRQ: cli; in(base+0xe) (ack); if !g_SBStopFlag and g_SBDspVer <= 1:
re-send 0x14, count lo, hi; if stop flag: g_SBStopped = 1; out(0x20,0x20) (master EOI only); sti.
SB_UnmaskIRQ 0x308de: save in(0x21), clear bit irq. SB_RestoreIRQMask 0x308fa. SB_SetStopFlag 0x308c3.
SB_IsStopped 0x308d1 (returns g_SBStopped; unused in JS). SB_Stop 0x307ab: if ver<=1 WriteDSP(0xD3); ResetDSP.
Quirks (no port impact): vector = irq+8 and master PIC only, so IRQ >= 8 cannot work.

### 2.9 DMA_WaitPos @ 0x30ac7 [V]
`u16 DMA_WaitPos(void)` returns DX: clear flip-flop; loop { read count(port 3) twice as 16-bit a, b;
until |a-b| <= 4 and b < g_DmaBufLen }; return g_DmaBufLen - b (play position, 1..0x4000).

### 2.10 Mixer_SetChannel @ 0x309ab [V]
`void Mixer_SetChannel(int ch, u8 *data, u32 len, u32 loop, u8 shift, u32 rateParam)` (stack).
ch 0/1/2, anything else -> channel 3. Sets data, len, pos = 0, loop, shift,
`step = (u16)(40000 / (u16)rateParam) & 0xf` (16-bit `div cx`: rateParam & 0xffff == 0 -> #DE crash).
Not atomic w.r.t. the timer ISR (port: apply under the audio lock).

### 2.11 Mixer_SetRate @ 0x30ffa [V]
`void Mixer_SetRate(int ch, u32 rateParam)`: only `step` of channel ch (same mapping, else ch3), same formula.

### 2.12 Mixer_IsActive @ 0x3103b (unused) [V]
Returns data != NULL but with the channel mapping reversed (0 -> ch3 ... 3 -> ch0). Getters 0x31092
(base), 0x3109c (irq), 0x310a6 (dma), 0x310b0 (dsp ver) are unused. 0x30b06..0x30ff9 is an unused near-copy
of the timer ISR body (polled variant, like INTRO's Mixer_Update); never called.

### 2.13 Timer_ISR_SBMixer @ 0x310ba (INT 8, 50 Hz) [V]

Installed by `Timer_Install` 0x10abb (DPMI 0x204/0x205 on vector 8, PIT ch0 mode 3 divisor 0x5d37 =
23863 -> 50.0006 Hz). It does **not** chain to the old INT 8. Exact algorithm (all positions u16):

```
newPos = g_MixWritePos + g_MixChunk;                    // chunk = 120
if (newPos > g_DmaBufLen) {                             // note: '>' (jbe skips)
    g_MixWrapFlag = 1;
    newPos -= g_DmaBufLen; if (g_SBDspVer >= 3) newPos--; newPos--;
}
g_MixNextPos = newPos;
play = DMA_WaitPos();                                  // bytes consumed so far in the ring
lim  = play + 0x78;                                    // u16
if (!g_MixWrapFlag) { if (!(g_MixWritePos < lim)) goto eoi; }
else                { if (g_MixNextPos > lim || g_MixWritePos < lim) goto eoi; }
// clear the chunk to 0x80
if (!wrap) memset(ring + g_MixWritePos, 0x80, 120);
else { memset(ring + wp, 0x80, 0x4000 - wp); memset(ring, 0x80, wp + 120 - 0x4000); }
for ch in 0,1,2,3 (in this order):
    dst = ring + g_MixWritePos;
    src = ch.data + ch.pos;                            // NULL+0 if idle
    n   = (g_SBDspVer >= 3) ? 60 : 120;
    do {
        if (src != NULL) {
            v = (u8)(((u16)*src << ch.shift) >> 8);   // = *src >> (8 - shift)
            *dst += v;                                 // u8 wraparound, no clipping
            if (g_SBDspVer >= 3) { dst++; if (ch <= 1) *dst += v; else *dst = v; }  // stereo path
            src += ch.step;
        }
        if (ch.data + ch.len <= src) {                 // also true for an idle channel (0 <= 0)
            if (ch.loop == 1) { ch.pos = 0; src = ch.data; }
            else { ch.data = NULL; ch.pos = 0; ch.len = 0; src = NULL; }
        }
        if (dst - ring < 0x3fff) dst++; else dst = ring;
    } while (--n);
    ch.pos = src - ch.data;
g_MixWritePos = g_MixNextPos; g_MixWrapFlag = 0;
eoi: out(0x20, 0x20); iretd
```
Notes:
- The volume shift `cl` is swapped into `cl` only (loop counter in `cx`; `ch` byte untouched): correct as
  long as count < 256 (120/60).
- One-shot end test runs after advancing: the last sample mixed is at offset < len. Slices are played with
  len = table length - 1 (see formats doc), so the final byte of each slice is never played.
- Loop restart goes to offset 0 of the slice (no separate loop point on SB).
- On wrap the next chunk starts one byte (stereo: two bytes) before where this one ended; that byte is
  cleared and remixed (tiny quirk).
- **Throttle**: a chunk is mixed only while the write cursor is less than 120 bytes ahead of the play cursor.
  The ISR supplies at most 120 x 50 = 6000 B/s, so it only keeps up if the DSP consumes <= 6000 B/s. With the
  actual TC 0 (3906 B/s) it keeps up and output is continuous; at 19920 B/s it would underrun and replay stale
  ring contents. (Another argument that the 3906 Hz behaviour is what really shipped; see open question 1.)
- **Stereo path** (g_SBDspVer >= 3, i.e. SB16 / DOSBox `sbtype=sb16`): 60 frames x 2 bytes per call;
  channels 0 and 1 add to both bytes, channels 2 and 3 **overwrite** the right byte with their own `v`
  (losing the 0x80 bias and channels 0/1). So R = 0x80+c0+c1 when ch2/3 idle, = c2 when only ch2 active,
  = c3 when ch3 active. With hardware stereo the sound is L-correct / R-garbage; if the card ignores
  mixer reg 0x0e (real SB16), the two bytes are played sequentially as mono at 3906 Hz.

### 2.14 Exact per-sample output (port reference)
For each output frame (mono path):
```
u8 out = 0x80;
for ch in 0..3: if (ch.data) { out += ch.data[ch.pos] >> (8 - ch.shift); ch.pos += ch.step;
                               if (ch.pos >= ch.len) { if (ch.loop==1) ch.pos=0; else ch.data=NULL; } }
pcm_s16 = ((int)out - 128) << 8;
```
(`ch.data` holds unsigned bytes = AAF byte + 0x80, i.e. `(int8)aaf ^ 0x80`.) Changes from the game thread
take effect at the next 120-byte chunk in the original (port may apply them at any sample boundary; the
difference is < 31 ms at 3906 Hz, < 6 ms at 19920 Hz).

---

## 3. Game-side sound API

### 3.1 Sound_Init @ 0x31e24 [V]
```
r = GUS_Detect(0);                                    // (ramUnits << 16) | port, 0 if none
if ((r & 0xffff) < 0x200) {                           // no GUS -> SB
    if (!SB_Detect(0, 0, 0, g_SBIrq, 0x4dd0)) return; // g_SoundDevice stays 0
    g_SoundDevice = 1;
    g_SampleData = malloc(0x48a7a); fread whole "misc\jetsound.aaf" (0x48a7a bytes);
    for i in 0..0x48a79: g_SampleData[i] += 0x80;     // signed -> unsigned
    SB_SpeakerOn();                                   // sends TC 0 (see 2.5)
    DPMI 0x100, BX=0x800 (32 KB DOS memory) -> g_DosBufSeg = AX (segment);
    _fmemset(selector:0, 0x80, 0x4000);
    SB_SetRate(); SB_SetupDmaBuffer(g_DosBufSeg);
    g_SBVector = g_SBIrq + 8; save old vector; _dos_setvect(g_SBVector, SB_IRQ_ISR);
    SB_UnmaskIRQ(); SB_StartDMA(); Timer_Install();
} else if ((r >> 16) < 0x200) {                       // GUS with 256 KB
    g_GusSliceLimit = 0x21; GUS_Reset(); buf = malloc(0x32dea); fread 0x32dea bytes;
    GUS_Upload(0x32dea, buf, 0); free(buf); GUS_Start(); g_SoundDevice = 2;
} else {                                              // GUS with >= 512 KB
    g_GusSliceLimit = 0x23; same with 0x48a7a bytes; g_SoundDevice = 2;
}
```
Called from MainMenu 0x469e7 only when JS.CFG sfx word != 0 and not yet done; if afterwards
`g_SoundDevice == 0` -> `FatalError("No supported soundcard found ...", ..., 3)` [V].

### 3.2 Sound_Shutdown @ 0x32119 [V]
SB (device 1, g_SampleData != NULL): SB_Stop, SB_SetStopFlag, SB_RestoreIRQMask, restore SB vector,
free samples, g_SampleData = NULL, DPMI 0x101 free DOS block. **Does not restore INT 8 / PIT** (Timer_Restore
0x10ba0 is never called; `main` reprograms the PIT to 18.2 Hz afterwards). GUS: GUS_Shutdown.
Callers: main 0x146f4 at exit, FatalError 0x10c1d (only if g_SfxOn). Port: close the SDL audio device.

### 3.3 Sound_StopAll @ 0x321e5 [V]
SB: for ch 0..3: `Mixer_SetChannel(ch, g_SampleData, 0, 0, 1, 1000)` (len 0, step 8). Quirk: since data !=
NULL, the next ISR mixes one byte `g_SampleData[0] >> 7` (= 0xC1>>7 = 1) per channel, then idles it. Port: just
clear the channels. GUS: for voice 0..13: GUS_PlayVoice({voice, mode 0, pan 7, freq 0x3c, vol 0, start 0,
loop 0, end 0}). Uses 0x90470 as loop counter (shared global; harmless).
Callers: Game_Run end of mission (after the last frame), Mission_Setup 0x212ac (first thing),
WeaponSelect_Screen 0x29d90.

### 3.4 Sfx_Play @ 0x315bc [V]
`void Sfx_Play(int id, int freq, int vol, int x)`: `if (g_SfxOn) Sfx_PlayChannel(id - 1, freq / 2, vol, 0);`
`x` is ignored (no panning). `freq/2` is signed `idiv`.

### 3.5 Sfx_PlayChannel @ 0x31a53 [V]
`void Sfx_PlayChannel(int slice, int f, int vol, int loop)`:
```
if (g_SoundDevice == 0) return;
if (g_SoundDevice == 1) {
    off = sum(g_SfxLens[0..slice-1]);
    if (++g_SfxChanRR > 3) g_SfxChanRR = 1;           // 2,3,1,2,3,1,...
    shift = VolToShift(vol);
    if (f < 8000) f = 8000;
    Mixer_SetChannel(g_SfxChanRR, g_SampleData + off, g_SfxLens[slice] - 1, 0, shift,
                     (20000 / f) * 20000);             // loop arg is 0 regardless of 'loop'
} else if (g_SoundDevice != 2) return;
if (slice < g_GusSliceLimit) {                         // 0 on SB -> skipped
    off = 0x50 + sum(g_SfxLens[0..slice-1]);
    v = g_GusVoiceRR; if (++g_GusVoiceRR > 13) g_GusVoiceRR = 2;   // voices 2..13
    GUS_PlayVoice({v, mode = loop ? (uninitialised stack byte) : 0, pan 7, 0,
                   freq = f / 19, vol = g_GusVolTable[vol],
                   start = off, loopStart = off, end = off + g_SfxLens[slice] - 1});
}
```
`VolToShift(v)`: v<=1 -> 0, 2 -> 1, 3..4 -> 2, 5..8 -> 3, 9..16 -> 4, 17..32 -> 5, >32 -> 6 (signed compares).
Note on SB `f` here is already freq/2, so step = 1 for freq <= 20001, 2 for 20002..40001 (only freq 25000 is
used), #DE for freq/2 > 20000 (never used).
Quirk: `vol` indexes `g_GusVolTable` unchecked (all call sites pass 0..0x3f).

### 3.6 Sfx_PlayVoice @ 0x31c51 [V]
`void Sfx_PlayVoice(int ch, int slice, int f, int vol, int loop)`: as Sfx_PlayChannel but with a fixed
channel/voice, **no halving** of f and no g_SfxOn check (but g_SoundDevice is 0 without Sound_Init):
SB: `Mixer_SetChannel(ch, g_SampleData + off, len-1, loop, VolToShift(vol), (20000/max(f,8000))*20000)`.
GUS: off = 0x186 + sum, mode = loop ? 8 : 0, start = off, loopStart = off + 2, end = off + len - 1,
freq = f/19, vol = table[vol].

### 3.7 Sound_SetFreq @ 0x322c4, Sound_SetVolume @ 0x3235d [V]
SetFreq(ch, f): SB: `Mixer_SetRate(ch, (20000 / max(f,8000)) * 20000)` (step 1 if f<=10000, 2 if
<=20000, **#DE if f > 20000**); GUS: GUS_SelectVoice(ch); GUS_SetFreq(f / 19).
SetVolume(ch, v): **GUS only** (GUS_SelectVoice; GUS_SetVolume(g_GusVolTable[v])); no-op on SB.

---

## 4. Effect table and call sites

Slice k = Sfx id - 1 (`Sfx_Play`) or the slice argument directly (`Sfx_PlayVoice`). Volume column = API volume
(SB shift in brackets). `x` arguments are ignored and omitted. All `Rand(n)` = `rand() % (n+1)`, and
**`Rand(0)` returns 0 without calling rand()** [V]. RNG calls listed are those made *for* the sound
decision, in order; short-circuit evaluation as shown.

| id | slice len | name (by context) | call site | condition | freq (SB step) | vol |
|---|---|---|---|---|---|---|
| 1 | 5054 | gun_light | Player_Weapons 0x32846 | gun fired with 0x901f8 != 0 (alt gun), followed by id 3 | 14000 (1) | 0x10 [4] |
| 1 | | | EnemyGround_Update 0x35e20, FUN_0003e8b3 | enemy shot, followed by id 3 | 14000 (1) | 0x0c [4] |
| 2 | 4186 | cannon | Player_Weapons | gun fired, 0x901f8 == 0, ammo type u = (0x90558+0x90544 > 0) == 0 | 20000/(u+1) - ((0x90558+0x90544)%10)*1000 (1) | 0x20 [5] |
| 2 | | | Weapon_FireSpecialA 0x3494e / B 0x34b94 | fire | 20000 (1) | 0x1e [5] |
| 3 | 834 | gun_report | after every id 1 | | 4500 (1) | 0x20 [5] player / 0x18 [5] enemy |
| 4 | 3696 | explosion_a | Game_Run 0x1d55e | 0x110bc() != 0 && 0x8ff68 == 0; `Rand(0)*23+4` = always 4 | 7500 (1) | 0x3f [6] |
| 4/27 | | | EnemyAir_Update 0x38630 (after Explosion_Damage) | Rand(10) < skill+1; id = Rand(1)*23+4 | 7000 (1) | 0x3f |
| 4/27 | | | Explosion_Damage 0x39cbe | power(0x90210, clamped <=4000) >= 100 && 0x900f0 < 5; id = Rand(1)*23+4 | 8000 - power/200 (1) | 0x20 [5] |
| 4/27 | | | FUN_0003ec5b 0x3ee9d | r = Rand(2000) **then** id = Rand(1)*23+4 | 6000 - 0x9028c/200 + r (1) | 0x3f |
| 4 | | | Game_Run 0x1e5b6 | 0x8fff8 == 4 (counter +4/frame from EnemyAir_Update, -1/frame in Game_Run) | 25000 (**2**) | 0x3f |
| 5 | 8020 | weapon_release | Weapon_Fire 0x341e9 | 0x907c4 == 1; also sets 0x900f0 = 10 | 6000 (1) | 0x28 [6] |
| 6 | 18990 | explosion_rumble | Explosion_Damage | power >= 500 && 0x900f0 < 5 (evaluated after the id 4/27 call); sets 0x900f0 = 2 | 12000 - power/10 (1) | 0x20 |
| 7 | 2216 | event_81 | Game_Run 0x1df5f | 0x90414 == 0 && 0x90034 == 0x81 && 0x904cc == 0 (every frame it holds) | 12000 (1) | 0x30 [6] |
| 8 | 2160 | launch_thump | Player_Weapons 0x328a0 | 0x906b0 != 0 && 0x8fa50 == 0 && 0x905b8 > 0 && 0x90608 == 0 | 3000 (1) | 0x3f |
| 8 | | | Weapon_FireCamera 0x35532, Weapon_FireGuided 0x356fe | fire | 4000 (1) | 0x3f |
| 8 | | | FUN_0003978d | | 15000 (1) | 0x2b [6] |
| 9 | 3580 | player_hit | Game_Run 0x1fe5a | after hit flash, 0x907ec == 0 && 0x90594 == 0 | 10000 (1) | 0x3f |
| 10 | 13866 | warning_tone | FUN_0003f945 (from Engine_SoundUpdate) | see 5.3; sets 0x90404 = 0x900f0 = 0x18 | 8000 (1) | 0x3f |
| 11/12/13 | 7942/8256/5850 | warning voices | Engine_SoundUpdate | 0x18 frames after id 10 (5.3) | 8000 (1) | 0x3f |
| 14 | 4390 | repeat_counter | Game_Run 0x1e590 | 0x902ec != 0 && 0x905a0 == 0: 0x902ec--, play (every other frame) | 9000 (1) | 0x3f |
| 15 | 5324 | system_damage | Player_DamageSystems 0x3f2fe | g_Armour reaches exactly -1 | 12000 (1) | 0x20 |
| 16 | 46 | stub16 | FUN_000389b0 | 0x9018c == 0 && Rand(20) == 1; sets 0x9018c = 1, 0x900f0 = 8 | 4000 | 0x3f |
| 17 | 6398 | cannon_heavy | Player_Weapons | as id 2 with u == 1 | 10000 - (n%10)*1000 (1) | 0x20 |
| 18 | 16394 | random_event | Game_Run 0x1e220 | Rand(1000) == 1 path (see Game_Run spec); sets 0x8ffb0 = 50 | 5500 (1) | 0x3f |
| 19 | 4976 | gear_toggle | Player_Update 0x2d34e | toggles 0x8ff14, if 0x90060 == 0 | 4500 (1) | 0x1e [5] |
| 20 | 4850 | debris | Explosion_Damage 0x3a983 | 0x90534 > 0 && 0x90738 > 8 && 0x90194 == 0 && Rand(5) == 1; sets 0x90194 = 1, 0x900f0 = 15 | 4000 | 0x20 |
| 21 | 46 | stub21 | Weapon_FireMissile 0x3529c | (timer > 400 && Rand(10) == 1 && 0x90198 == 0) or timer == -40; sets 0x90198 = 1 | 4100 | 0x3f |
| 22 | 5904 | mine | Mines_Update 0x35a33 | 0x9019c == 0 && Rand(50) == 1; sets 0x9019c = 1 | 4000 | 0x3f |
| 23 | 46 | stub23 | Game_Run 0x1ea84 | 0x8ff34 counts down to 0 (40 frames after id 33) && 0x90190 == 1 | 4100 | 0x3f |
| 24 | 46 | stub24 | Airbase_Update 0x2bc0b (call 0x2c462) | landing at base, before WeaponSelect_Screen | 4000 | 0x3f |
| 25 | 4984 | proximity | Player_Weapons (call 0x32a4a) | ammo ran out near target (<300 px, see Player_Weapons spec), once (0x90144) | 8000 | 0x3f |
| 26 | 2688 | enemy_air_event | FUN_000389b0 | 0x9014c == 0 && Rand(10) == 1 (evaluated after the id 16 test) | 4000 | 0x3f |
| 27 | 5240 | explosion_b | see id 4/27 | | | |
| 33 | 11660 | mission_event | FUN_00014923 | `(0x90148 < 1 \|\| Rand(40) != 1) && (Rand(10) == 1 \|\| g_Mission == 0) && 0x90190 == 0`; sets 0x90190 = 1, 0x8ff34 = 40 | 4100 | 0x3a [6] |
| slice 1 | 4186 | cannon (loop) | Engine_Sfx ch/voice 1 | g_CrashTimer > 0 (5.2) | g_CrashTimer*15 (clamped 8000 -> 1) | min(timer,63) |
| slice 28/30/31 | 15904/14070/6292 | engine_1/2/3 | Engine_Sfx ch/voice 0, loop | 5.1 | 2500 -> 8000 (1) | 0x10 [4] |

Unused slices: 27 (id 28), 29 (id 30), 33 (id 34), 34 (id 35). The 46-byte slices (ids 16, 21, 23, 24) are AMOS
dummy samples: effectively 45 bytes of the previous sample's tail + header bytes, i.e. a click.
Freq values in hex in the binary: 0x1194 = 4500, 0x1004 = 4100, 0x157c = 5500, 0x1d4c = 7500,
0x2328 = 9000, 0x2ee0 = 12000, 0x61a8 = 25000.

Player gun detail [V]: `u = (0x90558 + 0x90544 > 0); 0x90588 = u; Sfx_Play(u*15 + 2, 20000/(u+1) -
((0x90558+0x90544) % 10) * 1000, 0x20)`. The alt gun (0x901f8 != 0) plays id 1 then id 3 instead.
Note that the effects round-robin over only 3 channels, so a burst (gun + report + explosion) evicts the
oldest sound immediately; effects retriggered every frame (id 7, id 14, id 4 at 0x8fff8 == 4 only once since
the counter passes 4 once per decay) restart from the first byte each frame.

---

## 5. Engine and continuous sounds

### 5.1 Engine_Sfx @ 0x391e3 [V]
```
if (0x907fc == 0 && g_CrashTimer < 1) {
    if (0x8fef4 == 0 && 0x90594 == 0) {
        c = 0x906d0; if (c % 10 == 3) c -= 3;           // signed %
        switch ((u32)(c + 1)) {                          // unsigned compares
          case 1:  Sfx_PlayVoice(0, 0x1c, 2500, 0x10, 1); break;   // class 0, 3
          case 2:  Sfx_PlayVoice(0, 0x1e, 2500, 0x10, 1); break;   // class 1
          case 3:  Sfx_PlayVoice(0, 0x1f, 2500, 0x10, 1); break;   // class 2
          case 12: Sfx_PlayVoice(0, 0x1e, 2500, 0x10, 1); break;   // class 11
          default: break;                                // other classes: engine unchanged
        }
    }
    g_EngineSfxRequest = 0;
} else if (0x907fc != 0) Sound_SetVolume(0, 0);        // GUS only: on SB the engine keeps running
if (g_CrashTimer > 0) {
    v = min(g_CrashTimer, 63); if (v < 0) v = 0;
    Sfx_PlayVoice(1, 1, g_CrashTimer * 15, v, 1);      // restarts slice 1 from byte 0 on every call
}
```
On SB the engine therefore starts at step 1 (2500 clamped to 8000) with shift 4.

### 5.2 Engine_SoundUpdate @ 0x39388 (once per game frame, from Game_Run 0x207c7) [V]
```
if (g_SfxBusyTimer > 0) g_SfxBusyTimer--;
g_EngineRevTarget = 0x90720/2 (signed, round to 0) + 0x8ffd4*10 - (0x907a8 > 0)*0x8ff2c*5;
s = 0x90720 + T[0x901f0]   (T = int table 0x8e210);
d = (abs(0x90720) <= 10) ? Sign(0x90720)*abs(0x90720) : Sign(0x90720)*10;
if (T[0x901f0] == 0) s += d;
lo = __FSI4(__FSA(__FSM(f90094, 10.0f), -30.0f));  if (lo < 0) lo = 0;     // float32 ops, f90094 is a float
hi = __FSI4(__FSM(__FSA((float)T[0x901f0], f90094), 10.0f)); if (hi < 0) hi = 0;
0x90720 = Clamp(s, lo, hi);
if (0x904cc == 1) g_EngineRevTarget = 0x8ffd4 * 10;
if (g_EngineRev != g_EngineRevTarget) {
    st = abs(g_EngineRevTarget - g_EngineRev) / 15; if (st < 1) st = 1;
    g_EngineRev += Sign(g_EngineRevTarget - g_EngineRev) * st;
}
if (0x905a0 != 0) {                                     // toggles every frame -> every 2nd frame
    if (0x8fef4 == 0) {
        if (g_CrashTimer < 1) switch (0x906d0 % 10) {
            case 0: case 3: Engine_SetPitch(); break;
            case 1: Engine_Pitch1(); break;           // 0x39991
            case 2: Engine_Pitch2(); break;           // 0x39a94
        } else Engine_Nop();                          // 0x39976, empty
    } else Engine_DeathWobble();                      // 0x397c5: 0x8fef4 = clamp(0x8fef4 + Rand(1), 4, 12)
}
if (g_PendingWarnSfx > 0) {
    if (g_WarnSfxDelay == 0) Sfx_WarningTone();       // 0x3f945
    else if (--g_WarnSfxDelay == 0) { Sfx_Play(g_PendingWarnSfx, 8000, 0x3f, x);
                                      g_SfxBusyTimer = 0; g_PendingWarnSfx = 0; }
}
if (0x907fc > 0) { ... player-down drift: Rand(3), Rand(abs(0x90778)), Rand(1), Rand(1) or Rand(2) ... }
    // (non-sound; exact code in the player spec; RNG order: Rand(3) first, rest only if ==3 && scrY > 0x9c)
if (g_CrashTimer > 0 || g_EngineSfxRequest == 1 || 0x907fc != g_EngineSfxLastState) {
    Engine_Sfx(); g_EngineSfxRequest = 0; g_EngineSfxLastState = 0x907fc;
}
```
`__FSI4` rounding: Watcom chop (truncate) [L]. FSA/FSM argument order as listed (eax op edx) [V].
The non-sound block (0x907fc > 0) is reproduced in the decompile at js.c:20702; port it from there.
Because the last condition includes `g_CrashTimer > 0`, the crash loop on channel 1 is restarted every frame.

### 5.3 Warning sequence [V]
Setters: FUN_0003f5c8 sets g_PendingWarnSfx = 0xb (damage), FUN_0003f645 = 0xc, FUN_0003f8c8 = 0xd (each only
if 0x90440 == 0 && 0x907fc == 0). Cleared at mission start and on death/crash events. Engine_SoundUpdate:
first frame with a pending id and delay 0 -> `Sfx_WarningTone` (0x3f945): `Sfx_Play(10, 8000, 0x3f)`,
g_WarnSfxDelay = 24, g_SfxBusyTimer = 24. The next frames count down; at 0 the voice sample plays and the
queue clears. If a new id is queued during the delay, only the latest is played.

### 5.4 Engine pitch functions [V]
All call `Sound_SetFreq(0, f)` then compute an int written to 0x8ff40 (non-sound; owner: player spec).
`e = g_EngineRev`, `x = max(0, e - 80)`.

| func | class (0x906d0 % 10) | f | 0x8ff40 (double math, __FDI4 truncation) |
|---|---|---|---|
| Engine_SetPitch 0x39817 | 0, 3 | `e*90 + 4000 + x*25` (the `%10 == 4` branch `e*50 + 5000 + x*25` is dead) | `min(16.0 + e*0.4, 32.0)` |
| Engine_Pitch1 0x39991 | 1 | `(x*100 + e*80 + 8000) / 2` (signed, toward 0) | `min(16.0 + e*0.2, 48.0)` |
| Engine_Pitch2 0x39a94 | 2 | `e*40 + 6000` | `min(18.0 + e*0.2, 32.0)` |

The constants are doubles 0.4 = 0x3fd999999999999a, 0.2 = 0x3fc999999999999a; order: `FDA(16.0, FDM((double)e,
0.4))`, compare `> 32.0` (FDC + jle), then FDI4. Pitch1 sets 0x8ff40 *before* SetFreq; the others after.
SB effect: step 1 while f <= 10000, step 2 (octave up) for 10000 < f <= 20000, **crash (#DE) if f > 20000**
(Engine_SetPitch reaches 20000 at e = 136; whether e can get there depends on 0x90720's range: open question 4).
GUS effect: continuous, `FC = f / 19`.

---

## 6. Gravis UltraSound (obj4 @ 0x70000, asm) [V unless noted]

State: 0x70499 g_GusBase (port, e.g. 0x220), 0x7049d g_GusRam (0x100 per 256 KB), 0x704a1 upload address.
Register access: select at base+0x103, data 16-bit at base+0x104, data 8-bit at base+0x105, DRAM at base+0x107.

| func | behaviour |
|---|---|
| GUS_Detect 0x70000 (port) | port==0: probe base 0x210..0x260 step 0x10 (reset, write 0xAA to DRAM 0, read back); then RAM size: test DRAM at 0x40000, 0x80000 (0xC0000 ends) adding 0x100 per good 256 KB bank; returns `(ram << 16) \| base`, or 0 base on failure |
| GUS_Upload 0x7004c (len, src, dramAddr) | pokes len+1 bytes (one past the buffer) via regs 0x43/0x44 + port 0x107 |
| GUS_SelectVoice 0x700a6 (v) | out base+0x102 = v; reg 0x0e (active voices) = 0x0e |
| GUS_Start 0x700d1 | out base = 1 (mix control: line out on) |
| GUS_Shutdown 0x700df | out base = 3; GUS_Reset |
| GUS_SetFreq 0x700f2 (fc) | reg 1 = fc (16-bit) |
| GUS_SetVolume 0x7012b (v) | reg 9 = v (16-bit) |
| GUS_Reset 0x702d6 | out base = 3; reg 0x4c = 7 (run, DAC, IRQ enable); reg 0x0e = 0x0e (15 voices); voices 31..0: reg 0 = 3 (stop), reg 0x0d = 3 (volume ramp stop); out base = 0 |
| GUS_PlayVoice 0x7034d (struct*) | see below |

```c
struct GusVoiceReq {   // built on the stack by the callers
    u8 voice;          // +0
    u8 mode;           // +1 voice control: 0 = one-shot forward, 8 = loop
    u8 pan;            // +2 always 7 (centre)
    u8 pad;            // +3
    u16 fc;            // +4 reg 1
    u16 vol;           // +6 reg 9
    u32 start;         // +8 DRAM byte address -> regs 0x0a/0x0b (current position)
    u32 loopStart;     // +0x0c -> regs 2/3
    u32 end;           // +0x10 -> regs 4/5
};
```
Order: select voice; reg 0 = mode | 3 (stop); reg 0x0c = pan; reg 1 = fc; reg 9 = vol; loop start; end;
current = start; reg 0 = mode (start). Addresses are written as `hi = (a >> 7) & 0x1fff`, `lo = a << 9`
(8-bit samples). Volume changes are instant (no ramp) -> clicks.

Port model of the GUS path (if offered): output rate 617400/15 = 41160 Hz [L] (15 active voices),
voice increment = fc/1024 samples per output sample [L], so Sfx_Play(freq) plays at
`(freq/2/19)/1024 * 41160` ~= 1.058 x freq Hz; volume gain from the 12-bit log value `w = vol >> 4`:
`gain = 2^((w >> 8) - 15) * (1 + (w & 0xff)/256)` relative to full scale [G]. Data = signed 8-bit AAF bytes.
Window per slice: Sfx_Play -> [0x50+off, 0x50+off+len-1]; Sfx_PlayVoice -> [0x186+off, 0x186+off+len-1],
loop start +2. These differ from the SB windows ([off, off+len-1)) by 80 / 390 bytes, so GUS skips most of the
leading click but includes ~80/390 bytes of the *next* sample at the end.
GUS voice usage: 0 engine, 1 crash loop, 2..13 round-robin effects; Sound_StopAll silences 0..13.

---

## 7. CD audio (MSCDEX via DPMI 0x300 -> INT 2Fh)

### 7.1 Low-level [V]
- CD_MSCDEX_Install 0x2f7e6: AX=1500h; returns CX (first CD drive letter, 0 = A:). 0 means "not found"
  (also a CD on drive A: would be "not found").
- CD_MSCDEX_Version 0x2f892: AX=150Ch; returns BX > 0x209 (needs MSCDEX 2.10+).
- CD_GetDiscInfo 0x2fc91 (drive): DOS block 5 paragraphs; IOCTL input (cmd 3, 7-byte buffer, code 0x0A);
  g_CDFirstTrack = buf[1], g_CDLastTrack = buf[2], g_CDTrackStart[0] = lead-out frames
  (F + S*75 + M*4500 from buf[3..5]); returns last - first + 1. Frees the block.
- CD_ReadTrackTable 0x2fe82 (drive): DOS block 0x11 paragraphs; for t = first .. last+1: IOCTL 0x0B track
  info -> g_CDTrackStart[t] = F + S*75 + M*4500; then g_CDTrackStart[last+1] = g_CDTrackStart[0] (lead-out).
  (The query for track last+1 is invalid; its result is overwritten.)
- CD_PlayTrackRaw 0x2f951 (drive, t): request header cmd 0x84, addressing mode 1 (Red Book), start = MSF of
  g_CDTrackStart[t] re-encoded `M<<16 | S<<8 | F`, count = start[t+1] - start[t] frames. Returns status word.
  Plays the track **once**; nothing ever restarts it.
- CD_StopRaw 0x2fb5a (drive): request cmd 0x85 (stop audio).
- CD_Check 0x300b9: 1 no MSCDEX, 2 old version, 3 disc track count != 15, 0 ok (then reads the table).
- CD_PlayTrack 0x30136 (t) / CD_Stop 0x30176: only if g_CDMusicOn.

MainMenu (first call only): CD_Check -> 1: FatalError("MSCDEX Not found ..."), 2: "Old version of MSCDEX ...",
3: "Wrong CD. Consider buying the original!"; this happens **even if CD music is off** in JS.CFG.
Port: skip the check entirely (or verify that Game/MUSIC/TRACK02..15.WAV exist and only disable music).

### 7.2 Track selection and lifetime [V]
| situation | code | track |
|---|---|---|
| mission start (Game_Run, call at 0x1cc2a, before the main loop) | `t = 0; while (t < 2 \|\| (6 < t && t < 14)) t = Rand(13) + 2; CD_PlayTrack(t)` | uniform over {2,3,4,5,6,14,15}; Rand(13) is called a geometric number of times (accept prob 7/14) **regardless of g_CDMusicOn** |
| mission end (Game_Run, after the loop) | `if (g_CDMusicOn) CD_Stop()` then last frame, Sound_StopAll | |
| end-game screen (EndGame_Screen 0x301b2, not the g_Lives == -10 branch) | `if (g_CDMusicOn) { g_CDEndgameLatch = 1; CD_PlayTrack(7 + idx); }` idx = 0x907f4 (0..6) | 7..13 |
| Mission_Setup, Mission_ResetState, PlaneSelect_Screen | `if (g_CDMusicOn) CD_Stop()` | |
| WeaponSelect_Screen | `if (g_CDEndgameLatch) CD_Stop()` (latch never cleared, so from then on every visit) | |
| FatalError | CD_Stop() | |

When a track ends, the drive stops; there is no repeat and no next track: the rest of the mission is
silent music-wise. Menus, briefing, debrief have no music. The end-game track keeps playing through the end
screen until one of the stops above.

### 7.3 Port
`CD_PlayTrack(t)`: stop current, open `Game/MUSIC/TRACK%02d.WAV`, stream once (no loop), mixed with the SFX at
the device rate (CD audio was mixed by the card's analog mixer; SB_SpeakerOn sets master volume 0xff; there is
no CD volume control in the game). `CD_Stop()`: stop. Missing file -> silent. Keep the RNG loop even with
music off.

---

## 8. Configuration (JS.CFG, read by main 0x146f4) [V]
- +0 word: CD music. MainMenu sets `g_CDMusicOn = word` each call.
- +2 word: sound effects. MainMenu: `g_SfxOn = word`; if non-zero and not yet initialised -> Sound_Init, and
  no card -> fatal. If zero, no driver is installed at all (no timer ISR); every Sfx_Play is a no-op;
  Sfx_PlayVoice / Sound_* are no-ops because g_SoundDevice == 0.
- +0x3a word: SB IRQ.
Port: same two switches; "no card" cannot happen (SDL device open failure -> run silent, not fatal).

---

## 9. Intro program sound (INTRO.EXE)

See `port/formats/sound_intro.md` §2 for the timeline; details verified here:

- Intro_SoundInit (INTRO:0x10464, body 0x10471) [V]: sets the "sound alive" flag 0x60364 = 1.
  GUS only if base >= 0x200 **and** RAM >= 512 KB, else SB. SB: `SB_Detect(0,0,0,0,40000)`: IRQ argument
  0 is stored (caller ZF=0), so `SB_GetIrq` returns 0 and the SB "IRQ" handler is installed on **INT 8**
  (timer) [L], replacing the BIOS tick while the intro runs. Same SB_SpeakerOn-before-SetRate order: real TC 0
  (3906.25 Hz). Loads INTRO.SAM (0x3f404 bytes), +0x80 each byte, DOS buffer, `Intro_PlaySample(6)`,
  SB_StartDMA. No timer ISR: `Mixer_Update` (INTRO:0x5058a, same code as 2.13) is polled from the main loop.
  GUS: GUS_Reset, upload 0x3f404 bytes at DRAM 0, GUS_Start.
- Intro_PlaySample(n) (0x101c7 / body 0x101d4) [V]: off = sum(g_SampleLens[0..n-1]).
  SB: n = 0,2,4 -> ch 1; n = 1,3 -> ch 2; one-shot, shift 6, rateParam 12000 -> step `40000/12000 & 15 = 3`;
  n = 6 -> ch 0, **loop**, shift 4, step 3; len = full slice length (not -1). n = 5, 7: nothing.
  GUS: voice n, mode 0 (n=6: 8 = loop), vol 60000 (n=6: 45000), fc 0x12e (n=0,1,4), 0x13b (n=2,6), 0x107 (n=3),
  start = loopStart = off + 2 (n=6: loopStart = off + 0x251c), end = off + len - 2.
- Intro_SoundShutdown (0x106cb / 0x106d8) [V]: once (flag 0x60364): SB_Stop, set stop flag, **busy-wait
  until SB_IsStopped** (needs one more SB IRQ; with the IRQ hooked on INT 8 the next timer tick satisfies it),
  restore mask and vector, free; GUS: GUS_Shutdown. Called at FRAME overlay step 9 and in Intro_Cleanup.
- CD: `CD_InitAndPlayTrack2` (0x1553a / 0x15547): MSCDEX install + version check (printf on failure, no
  fatal, no 15-track check), GetDiscInfo, ReadTrackTable, PlayTrackRaw(drive, 2): track 2 plays once.
  CD_Stop (0x15511) at exit is unconditional.
- Port: intro mixer = same mixer as the game with step 3 (designed output rate 40000 Hz -> effective
  13333 Hz per sample; "as coded" 3906.25 Hz / 3). Music: TRACK02.WAV once.

---

## 10. Port design (SDL3)

```c
typedef struct { const u8 *data; u32 len, pos; int loop; u8 shift; u8 step; } MixChan;
static MixChan g_ch[4];
static SDL_AudioStream *g_sfxStream;     // U8 mono at SB_RATE
static SDL_AudioStream *g_musicStream;   // S16 stereo 44100 from TRACKNN.WAV
```
- Load JETSOUND.AAF into `u8 g_samples[0x48a7a]`, each byte `+0x80`. Precompute `g_sliceOff[35]`.
- `SB_RATE` setting: 19920 (designed, default) or 3906.25 ("as coded", TC 0); optional stereo-bug mode
  emulating g_SBDspVer >= 3 (2.13) for DOSBox-sb16 parity. Use SDL resampling to the device rate.
- Mixer thread = SDL stream callback: for each output frame apply 2.14 exactly (unsigned 8-bit adds with
  wraparound; do not clip, do not convert per channel). Optionally quantise command application to 120-frame
  chunks for bit-exact timing; not needed for audible fidelity.
- Game-thread API with identical signatures/semantics: `Sfx_Play`, `Sfx_PlayChannel`, `Sfx_PlayVoice`,
  `Sound_SetFreq`, `Sound_SetVolume` (no-op in SB mode), `Sound_StopAll`, `Sound_Init/Shutdown`; protect channel
  writes with `SDL_LockAudioStream`. Keep the round-robin counter (init 1) and the step formula; replace the
  #DE cases with a clamp (`rateParam == 0` -> step 2) and log once (PORT note).
- Music: `CD_PlayTrack(t)` opens TRACK%02d.WAV (any format SDL_LoadWAV handles), plays once; `CD_Stop`
  clears the stream. Both gated on g_CDMusicOn exactly like the original (including the latch in
  WeaponSelect_Screen).
- GUS mode is optional (not needed for SB fidelity); if implemented use §6 offsets, 41160 Hz, fc/1024.
- Intro: same mixer with the INTRO.SAM slices, step 3, chunk refill irrelevant.

---

## 11. Original bugs and quirks

| # | quirk | recommendation |
|---|---|---|
| Q1 | TC sent before it is computed (TC 0 = 3906 Hz), and SetRate's divide is wrong anyway | user decision (open question 1); default 19920 with a setting |
| Q2 | DSP minor version stored as "version" -> SB16 uses the SB Pro stereo path | port: mono path by default; optional emulation |
| Q3 | Stereo path: ch2/ch3 overwrite the right byte | only in the optional emulation |
| Q4 | No clipping, 8-bit wraparound of the mix | keep (it is the sound) |
| Q5 | Active channels add DC (`0x80 >> (8-shift)`), DC steps at start/end of every sound | keep |
| Q6 | Step formula: only 2 pitches; f > 20000 divides by zero | keep steps; PORT: clamp instead of crashing |
| Q7 | SB engine octave jump at f = 10000 | keep |
| Q8 | Sound_SetVolume is GUS-only: engine not silenced on SB when the player goes down | keep |
| Q9 | Crash loop (slice 1 on ch1) restarted every frame; ch1 shared with Sfx_Play round-robin | keep |
| Q10 | SB slices start 148 bytes early and omit the last byte (formats doc) | keep |
| Q11 | Sound_StopAll leaves one byte of `sample[0]>>7` per channel | drop (inaudible) |
| Q12 | Sound_Shutdown never restores INT 8 | n/a |
| Q13 | CD_Check is fatal without the CD even when music is off; drive A: reads as "no MSCDEX" | drop: PORT note |
| Q14 | Music plays once, then silence | keep |
| Q15 | GUS_Upload copies len+1 bytes; Sfx_PlayChannel GUS `mode` uninitialised when loop != 0 (never happens) | n/a |
| Q16 | Mixer_IsActive mapping reversed (unused) | n/a |

---

## 12. Open questions

1. **Real SB sample rate.** Statically the DSP gets TC 0 (3906.25 Hz) in both JS and INTRO; the intended
   design rate is 19920 (JS) / 40000 (INTRO). A DOSBox capture settles it: play a mission start (sfx 33 =
   11660 bytes: 0.59 s at 19920, 2.98 s at 3906, 5.97 s at 1953) with `sbtype=sbpro2` and `sbtype=sb16`.
   Also check whether DOSBox halves the rate for SB-Pro stereo on an SB16 (mixer reg 0x0e) [G].
2. Does real SB16 hardware honour mixer reg 0x0e stereo? If not, the stereo-path bytes play as mono pairs.
3. GUS output rate / FC scale and the log-volume formula are from GUS documentation, not from the binary [L/G].
4. Range of 0x90720 / g_EngineRev: can Engine_SetPitch exceed f = 20000 (SB crash) on any plane?
5. Exact meaning of the conditions around ids 7, 14, 18, 25 (globals owned by Game_Run / Player_Weapons specs).
6. INTRO: confirm the SB handler really sits on INT 8 (IRQ argument 0) and whether that disturbs the
   intro's frame timing (it is vsync-paced, so probably not).
