/*
 * AnimationEngine.h - pure renderers. Each animation fills a local buffer
 * of `len` pixels for one segment; the caller maps local -> strip index
 * (handling `reversed`), so animations never care about wiring.
 */
#pragma once

#include "Config.h"

struct RenderCtx {
    uint32_t now;          // millis()
    uint32_t phaseMs;      // now + per-segment stagger offset
    uint8_t  speed;        // 1-255
    uint8_t  intensity;    // 0-255
    uint8_t  tintHue;      // global rainbow tint (0 = off / unused)
    bool     useTint;
    uint16_t segIndex;     // segment ordinal, for per-segment variation
    uint16_t absStart;     // strip index of local pixel 0 (for stateful FX)

    /* The enabled segments laid end to end, so an animation can sweep the
       whole sign as one continuous run rather than per letter. */
    uint16_t chainOffset;  // pixels before this segment in that chain
    uint16_t chainTotal;   // total lit pixels across every enabled segment
    uint16_t chainMaxLen;  // longest single segment, for phase timing
};

class AnimationEngine {
public:
    static void begin();
    /* Render one animation into `out[0..len-1]`. */
    static void render(uint8_t anim, const Segment &seg, const RenderCtx &ctx,
                       RGB *out, uint16_t len);
    /* Scratch state (fire heat, sparkle decay) is keyed on strip index. */
    static void resetState();
};
