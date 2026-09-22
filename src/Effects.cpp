#include "Effects.h"

/* ------------------------------------------------------------ sub-pixel ---
 * Positions are 8.8 fixed point rather than whole LEDs, and band edges fall
 * off with a cosine. A band can then sit between two pixels and cross-fade,
 * so motion is smooth at the ~40 fps the wire allows instead of stepping.
 */
#define Q8(x) ((int32_t)(x) << 8)

static inline uint8_t bandLevel(uint16_t i, int32_t posQ8, int32_t halfQ8) {
    int32_t d = Q8(i) + 128 - posQ8;          // +128 = centre of the pixel
    if (d < 0) d = -d;
    if (d >= halfQ8 || halfQ8 <= 0) return 0;
    uint8_t x = (uint8_t)((127L * d) / halfQ8);
    return cos8(x);                            // 255 at centre, ~0 at the edge
}

/* pixels per second */
static inline uint32_t pixelRate(uint8_t speed) { return 30UL + (uint32_t)speed * 4UL; }

/* 8-bit phase; speed 255 is roughly 4 cycles a second */
static inline uint8_t phase8(uint32_t ms, uint8_t speed) {
    return (uint8_t)(((ms * (uint32_t)speed) / 250UL) & 0xFF);
}

/* --------------------------------------------------------------- effects */

static void fxSolid(const EffectCtx &c, RGB *out, uint16_t len) {
    fillSolid(out, len, c.color);
}

/* Calm pulse: eased cubic so the turnaround at each end is soft, and a floor
   so the sign never goes fully dark mid-breath. */
static void fxBreathe(const EffectCtx &c, RGB *out, uint16_t len) {
    uint8_t b = cubicwave8(phase8(c.now, max<uint8_t>(c.speed / 3, 1)));
    b = qadd8(scale8(b, 205), 50);
    fillSolid(out, len, scaleColorVideo(c.color, b));
}

/* One band sweeps the whole word, letter to letter, without pausing. The
   trailing edge leaves a faint tint of the same hue rather than black, which
   reads as a glow following the band instead of a hard cut. */
static void fxTraverse(const EffectCtx &c, RGB *out, uint16_t len) {
    if (!len) return;
    const uint16_t total = c.chainTotal ? c.chainTotal : len;
    const uint32_t rate  = pixelRate(c.speed);
    const int32_t  halfQ = Q8(max<uint16_t>(6, total / 6)) / 2;
    const uint32_t span  = total + (halfQ >> 7) + 2;
    const uint32_t cycle = span * 1000UL / rate;
    if (!cycle) return;

    int32_t posQ8 = (int32_t)((uint64_t)(c.now % cycle) * rate * 256ULL / 1000ULL) - halfQ;
    RGB glow = shadeOf(c.color, 205);          // same hue, much darker
    RGB head = tintOf(c.color, 60);            // same hue, slightly lighter

    for (uint16_t i = 0; i < len; i++) {
        uint8_t lv = bandLevel(c.chainOffset + i, posQ8, halfQ);
        out[i] = lv ? blendColor(glow, head, lv) : glow;
    }
}

/* Three non-harmonic waves summed along the word. Hue swings only +/-20
   around the word's colour, so it flows and shimmers without ever becoming a
   rainbow - the "matching colours" rule. */
static void fxAurora(const EffectCtx &c, RGB *out, uint16_t len) {
    if (!len) return;
    const uint8_t baseHue = rgb2hue(c.color);
    const uint8_t baseSat = max<uint8_t>(rgb2sat(c.color), 170);
    const uint32_t t = c.now;

    const uint8_t p1 = (uint8_t)((t * (3 + c.speed / 12)) / 100);
    const uint8_t p2 = (uint8_t)((t * (2 + c.speed / 20)) / 100);
    const uint8_t p3 = (uint8_t)((t * (5 + c.speed / 9))  / 100);

    const uint16_t k1 = max<uint16_t>(1, 512 / len);
    const uint16_t k2 = max<uint16_t>(1, 1024 / len);

    for (uint16_t i = 0; i < len; i++) {
        uint8_t w1 = sin8((uint8_t)(i * k1 + p1));
        uint8_t w2 = sin8((uint8_t)(i * k2 - p2));
        uint8_t w3 = sin8((uint8_t)(i * ((k1 + k2) / 2) + p3));

        uint8_t v = qadd8(scale8(w1, 150), scale8(w2, 105));
        v = scale8(v, qadd8(scale8(w3, 150), 105));
        v = qadd8(scale8(v, 225), 30);                 // faint floor, never black

        int16_t swing = ((int16_t)w3 - 128) / 6;       // about +/-20 of hue
        out[i] = hsv2rgb((uint8_t)(baseHue + swing), baseSat, v);
    }
}

/* Bright head, smooth decaying tail, squared falloff. */
static void fxComet(const EffectCtx &c, RGB *out, uint16_t len) {
    if (!len) return;
    uint16_t tail = max<uint16_t>(6, len / 4);
    uint32_t rate = pixelRate(c.speed);
    uint32_t cycleMs = (uint32_t)(len + tail) * 1000UL / rate;
    if (!cycleMs) return;

    int32_t posQ8  = (int32_t)((uint64_t)(c.now % cycleMs) * rate * 256ULL / 1000ULL);
    int32_t tailQ8 = Q8(tail);
    RGB head = tintOf(c.color, 90);
    RGB rest = shadeOf(c.color, 225);

    for (uint16_t i = 0; i < len; i++) {
        int32_t d = posQ8 - (Q8(i) + 128);
        if (d >= 0 && d < tailQ8) {
            uint8_t b = 255 - (uint8_t)((255L * d) / tailQ8);
            b = scale8(b, b);
            out[i] = blendColor(rest, head, b);
        } else {
            out[i] = rest;
        }
    }
}

/* ------------------------------------------------------------------ entry */
void Effects::render(uint8_t anim, const EffectCtx &ctx, RGB *out, uint16_t len) {
    if (!len) return;
    switch (anim) {
        case ANIM_SOLID:    fxSolid(ctx, out, len);    break;
        case ANIM_BREATHE:  fxBreathe(ctx, out, len);  break;
        case ANIM_TRAVERSE: fxTraverse(ctx, out, len); break;
        case ANIM_AURORA:   fxAurora(ctx, out, len);   break;
        case ANIM_COMET:    fxComet(ctx, out, len);    break;
        case ANIM_OFF:
        default:            fillSolid(out, len, RGB_BLACK); break;
    }

    /* the show fades words in and out without touching their animation */
    if (ctx.fade < 255)
        for (uint16_t i = 0; i < len; i++) out[i] = scaleColorVideo(out[i], ctx.fade);
}
