#include "AnimationEngine.h"

/* Per-strip-index scratch state, shared by the stateful animations. */
static uint8_t g_heat[MAX_LEDS];
static uint8_t g_spark[MAX_LEDS];

static inline uint8_t *heatBuf(uint16_t absStart)  { return &g_heat[min<uint16_t>(absStart, MAX_LEDS - 1)]; }
static inline uint8_t *sparkBuf(uint16_t absStart) { return &g_spark[min<uint16_t>(absStart, MAX_LEDS - 1)]; }

/* --------------------------------------------------------------- utilities */

/* 8-bit phase: speed 255 ~= 4 cycles/s, speed 1 ~= 1 cycle/64 s */
static inline uint8_t phase8(uint32_t ms, uint8_t speed) {
    return (uint8_t)(((ms * (uint32_t)speed) / 250UL) & 0xFF);
}
/* 16-bit phase for smooth position sweeps */
static inline uint16_t phase16(uint32_t ms, uint8_t speed) {
    return (uint16_t)(((ms * (uint32_t)speed) / 1UL) & 0xFFFF);
}

static inline RGB scaled(const RGB &c, uint8_t v) { return scaleColorVideo(c, v); }

/* ---- sub-pixel motion ----------------------------------------------------
 * Positions are carried in 8.8 fixed point instead of whole LEDs, and band
 * edges fall off with a cosine. A band can then sit "between" two pixels and
 * cross-fade between them, so movement reads as smooth even at 43 fps where
 * integer stepping would visibly stutter.
 */
#define Q8(x) ((int32_t)(x) << 8)

/* Brightness of pixel `i` for a band centred on posQ8 with half-width halfQ8. */
static inline uint8_t bandLevel(uint16_t i, int32_t posQ8, int32_t halfQ8) {
    int32_t d = Q8(i) + 128 - posQ8;          // +128 = centre of the pixel
    if (d < 0) d = -d;
    if (d >= halfQ8 || halfQ8 <= 0) return 0;
    uint8_t x = (uint8_t)((127L * d) / halfQ8);   // 0 at centre .. 127 at edge
    return cos8(x);                                // 255 .. ~0, smooth both ways
}

/* Pixels per second for the travelling effects. */
static inline uint32_t pixelRate(uint8_t speed) { return 30UL + (uint32_t)speed * 4UL; }

static RGB tinted(const RGB &c, const RenderCtx &ctx, uint8_t localHue = 0) {
    if (!ctx.useTint) return c;
    uint8_t sat = rgb2sat(c);
    if (sat < 60) sat = 200;              // keep white-ish colours visible
    uint8_t val = max(c.r, max(c.g, c.b));
    return hsv2rgb(ctx.tintHue + localHue, sat, val);
}

/* ------------------------------------------------------------- animations */

static void animSolid(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    RGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

static void animBreathe(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    uint8_t b = cubicwave8(phase8(c.phaseMs, c.speed));
    uint8_t floorB = 255 - c.intensity;                 // intensity = depth
    b = qadd8(scale8(b, c.intensity), scale8(floorB, 40));
    RGB col = scaled(tinted(s.color, c), b);
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

static void animPulse(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    /* double heartbeat inside one cycle */
    uint8_t p = phase8(c.phaseMs, c.speed);
    uint8_t b = 0;
    if (p < 64)       b = ease8InOutQuad(p * 4);
    else if (p < 128) b = ease8InOutQuad(255 - (p - 64) * 4);
    else if (p < 176) b = scale8(ease8InOutQuad((p - 128) * 5), 170);
    else if (p < 224) b = scale8(ease8InOutQuad(255 - (p - 176) * 5), 170);
    b = qadd8(scale8(b, c.intensity), 4);
    RGB col = scaled(tinted(s.color, c), b);
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

/*
 * TRAVERSE runs in two phases and then repeats:
 *
 *   phase A - every letter sweeps a band along itself, in parallel
 *   phase B - one band sweeps the entire sign, letter to letter, as if the
 *             segments were a single continuous strip
 *
 * Phase B uses ctx.chainOffset/chainTotal, so it crosses letter boundaries
 * seamlessly however the segments are mapped.
 */
static void animTraverse(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    if (!len) return;

    const uint32_t rate  = pixelRate(c.speed);           // pixels / second
    const int32_t  halfQ = Q8(max<uint16_t>(2, (uint16_t)(len * (c.intensity + 16) / 640))) / 2;
    const uint16_t total = c.chainTotal ? c.chainTotal : len;
    const uint16_t longest = c.chainMaxLen ? c.chainMaxLen : len;

    /* Travel distance of each phase, in pixels, plus a band-width of run-off
       so the band fully exits before the phase ends. */
    const uint32_t runA = longest + (halfQ >> 7) + 2;
    const uint32_t runB = total   + (halfQ >> 7) + 2;
    const uint32_t msA  = runA * 1000UL / rate;
    const uint32_t msB  = runB * 1000UL / rate;
    const uint32_t cycle = msA + msB;
    if (!cycle) return;

    const uint32_t t = c.phaseMs % cycle;
    RGB col = tinted(s.color, c);
    fillSolid(out, len, s.color2);

    if (t < msA) {
        /* phase A: this letter sweeps itself */
        int32_t posQ8 = (int32_t)((uint64_t)t * rate * 256ULL / 1000ULL) - halfQ;
        for (uint16_t i = 0; i < len; i++) {
            uint8_t lv = bandLevel(i, posQ8, halfQ);
            if (lv) out[i] = scaled(col, lv);
        }
    } else {
        /* phase B: one band crosses the whole sign; convert this segment's
           pixels into chain coordinates so the band flows between letters */
        uint32_t tb = t - msA;
        int32_t posQ8 = (int32_t)((uint64_t)tb * rate * 256ULL / 1000ULL) - halfQ;
        for (uint16_t i = 0; i < len; i++) {
            uint8_t lv = bandLevel(c.chainOffset + i, posQ8, halfQ);
            if (lv) out[i] = scaled(col, lv);
        }
    }
}

/* head + exponential tail, sub-pixel positioned */
static void animComet(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    if (!len) return;
    uint16_t tail = max<uint16_t>(3, (uint16_t)(len * (c.intensity + 32) / 384));
    uint32_t rate = pixelRate(c.speed);
    uint32_t span = len + tail;
    uint32_t cycleMs = span * 1000UL / rate;
    if (!cycleMs) return;

    int32_t posQ8 = (int32_t)((uint64_t)(c.phaseMs % cycleMs) * rate * 256ULL / 1000ULL);
    RGB head = tinted(s.color, c);
    int32_t tailQ8 = Q8(tail);

    for (uint16_t i = 0; i < len; i++) {
        int32_t d = posQ8 - (Q8(i) + 128);
        if (d >= 0 && d < tailQ8) {
            uint8_t b = 255 - (uint8_t)((255L * d) / tailQ8);
            b = scale8(b, b);                        // squared falloff
            out[i] = scaled(head, b);
        } else {
            out[i] = s.color2;
        }
    }
}

/* fill up, then empty - eased so the edge glides instead of stepping */
static void animWipe(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    if (!len) return;
    uint8_t p = phase8(c.phaseMs, c.speed);
    bool filling = p < 128;
    uint8_t t = filling ? (uint8_t)(p * 2) : (uint8_t)(255 - (p - 128) * 2);
    int32_t edgeQ8 = ((int32_t)len * ease8InOutCubic(t) * 256L) / 255L;
    RGB col = tinted(s.color, c);

    for (uint16_t i = 0; i < len; i++) {
        int32_t d = edgeQ8 - Q8(i);
        if (d >= 256)      out[i] = col;                       // fully inside
        else if (d <= 0)   out[i] = s.color2;                  // fully outside
        else               out[i] = blendColor(s.color2, col, (uint8_t)d);  // the edge pixel
    }
}

static void animTheater(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    uint8_t  gap  = 3 + (c.intensity >> 6);                    // 3..6
    uint32_t step = ((c.phaseMs * (uint32_t)c.speed) / 800UL) % gap;
    RGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++)
        out[i] = ((i + step) % gap == 0) ? col : s.color2;
}

static void animSparkle(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    uint8_t *st = sparkBuf(c.absStart);
    RGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) {
        uint8_t v = st[i];
        v = qsub8(v, max<uint8_t>(1, scale8(c.speed, 24)));
        if (random8() < scale8(c.intensity, 30)) v = 255;
        st[i]  = v;
        out[i] = blendColor(s.color2, col, v);
    }
}

static void animTwinkle(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    RGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) {
        uint8_t seed = (uint8_t)(i * 37 + c.segIndex * 11);
        uint8_t w = sin8(phase8(c.phaseMs, c.speed) + seed * 3);
        w = scale8(w, w);
        out[i] = scaled(col, qadd8(scale8(w, c.intensity), 8));
    }
}

static void animRainbow(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    uint8_t spread = 1 + scale8(c.intensity, 24);
    uint8_t h = phase8(c.phaseMs, c.speed);
    for (uint16_t i = 0; i < len; i++)
        out[i] = hsv2rgb(h + i * spread, 240, 255);
}

static void animGradient(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    if (!len) return;
    uint8_t shift = phase8(c.phaseMs, c.speed);
    RGB a = tinted(s.color, c);
    RGB b = s.color2.isBlack() ? scaleColor(a, 40) : s.color2;
    for (uint16_t i = 0; i < len; i++) {
        uint8_t t = sin8((uint16_t)(i * 255UL / len) + shift);
        out[i] = blendColor(a, b, t);
    }
}

static void animStrobe(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    uint8_t p  = phase8(c.phaseMs, c.speed);
    uint8_t on = 8 + (c.intensity >> 3);                 // duty cycle
    RGB col = (p < on) ? tinted(s.color, c) : s.color2;
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

static void animFire(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    uint8_t *heat = heatBuf(c.absStart);
    uint8_t cooling  = 100 - scale8(c.intensity, 60);
    uint8_t sparking = 60 + scale8(c.intensity, 120);

    for (uint16_t i = 0; i < len; i++)
        heat[i] = qsub8(heat[i], random8(0, ((cooling * 10) / max<uint16_t>(len, 1)) + 2));
    for (uint16_t k = len - 1; k >= 2 && k < len; k--)
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    if (random8() < sparking && len > 7)
        heat[random8(7)] = qadd8(heat[random8(7)], random8(160, 255));
    for (uint16_t i = 0; i < len; i++)
        out[i] = heatColor(heat[i]);
}

/* ---------------------------------------------------------- sub-section FX */
/*
 * BUILD / STEPS walk the segment's sub-sections (the logo's pieces) one slot
 * at a time and finish on an all-glow step that lasts two slots:
 *
 *   slot:   0   1   2  ...  n-1   n   n+1
 *   piece:  0   1   2  ...  n-1   <---- all glow ---->
 *
 * BUILD keeps the previous pieces lit, STEPS shows one at a time.
 */
static void renderSections(const Segment &s, const RenderCtx &c,
                           RGB *out, uint16_t len, bool cumulative) {
    const uint8_t n = divisionCount(s);
    if (n <= 1) { animBreathe(s, c, out, len); return; }

    const uint16_t stepMs = max<uint16_t>(60, 25500 / max<uint8_t>(c.speed, 1));
    const uint8_t  slots  = n + 2;                    // + 2 slots of all-glow
    const uint32_t tick   = c.phaseMs / stepMs;
    const uint8_t  slot   = tick % slots;
    const uint16_t within = c.phaseMs % stepMs;       // progress inside the slot

    RGB col = tinted(s.color, c);
    fillSolid(out, len, s.color2);

    if (slot >= n) {
        /* final step: everything glows together, breathing across both slots */
        uint32_t glowMs = (uint32_t)(tick - (tick % slots) + n) * stepMs;
        uint8_t  ph     = (uint8_t)(((c.phaseMs - glowMs) * 255UL) / (2UL * stepMs));
        uint8_t  b      = qadd8(scale8(cubicwave8(ph), 150), 105);
        fillSolid(out, len, scaled(col, b));
        return;
    }

    for (uint8_t k = 0; k <= slot; k++) {
        if (!cumulative && k != slot) continue;
        uint16_t a, b;
        if (!divisionBounds(s, k, a, b)) continue;

        /* the newest piece eases in over the first third of its slot */
        uint8_t level = 255;
        if (k == slot) {
            uint16_t fade = stepMs / 3;
            level = (within >= fade) ? 255 : ease8InOutQuad((uint8_t)(255UL * within / max<uint16_t>(fade, 1)));
            level = qadd8(level, 8);
        }
        RGB piece = scaled(col, level);

        int32_t la = (int32_t)a - (int32_t)s.start;
        int32_t lb = (int32_t)b - (int32_t)s.start;
        for (int32_t i = max<int32_t>(la, 0); i <= lb && i < (int32_t)len; i++)
            out[i] = piece;
    }
}

static void animBuild(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    renderSections(s, c, out, len, true);
}

static void animSteps(const Segment &s, const RenderCtx &c, RGB *out, uint16_t len) {
    renderSections(s, c, out, len, false);
}

/* ------------------------------------------------------------------ engine */

void AnimationEngine::begin() { resetState(); }

void AnimationEngine::resetState() {
    ::memset(g_heat, 0, sizeof(g_heat));
    ::memset(g_spark, 0, sizeof(g_spark));
}

void AnimationEngine::render(uint8_t anim, const Segment &seg, const RenderCtx &ctx,
                             RGB *out, uint16_t len) {
    if (!len) return;
    switch (anim) {
        case ANIM_SOLID:    animSolid(seg, ctx, out, len);    break;
        case ANIM_BREATHE:  animBreathe(seg, ctx, out, len);  break;
        case ANIM_PULSE:    animPulse(seg, ctx, out, len);    break;
        case ANIM_TRAVERSE: animTraverse(seg, ctx, out, len); break;
        case ANIM_COMET:    animComet(seg, ctx, out, len);    break;
        case ANIM_WIPE:     animWipe(seg, ctx, out, len);     break;
        case ANIM_THEATER:  animTheater(seg, ctx, out, len);  break;
        case ANIM_SPARKLE:  animSparkle(seg, ctx, out, len);  break;
        case ANIM_RAINBOW:  animRainbow(seg, ctx, out, len);  break;
        case ANIM_GRADIENT: animGradient(seg, ctx, out, len); break;
        case ANIM_STROBE:   animStrobe(seg, ctx, out, len);   break;
        case ANIM_FIRE:     animFire(seg, ctx, out, len);     break;
        case ANIM_TWINKLE:  animTwinkle(seg, ctx, out, len);  break;
        case ANIM_BUILD:    animBuild(seg, ctx, out, len);    break;
        case ANIM_STEPS:    animSteps(seg, ctx, out, len);    break;
        case ANIM_OFF:
        default:            fillSolid(out, len, RGB_BLACK); break;
    }
}
