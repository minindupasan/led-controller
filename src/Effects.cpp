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
    /* Full swing: the trough reaches black, so the sign really breathes in
       and out rather than sitting lit and merely dimming. */
    uint8_t b = cubicwave8(phase8(c.now, max<uint8_t>(c.speed / 3, 1)));
    fillSolid(out, len, scaleColorVideo(c.color, b));
}

/*
 * TRAVERSE - a comet-like sweep across the whole sign, INNOV straight into
 * IOT, built from three parts so it reads as light rather than a lit strip:
 *
 *   core   a short, near-white crest (tintOf, so it stays the same hue)
 *   body   the word's own colour either side of the crest
 *   wake   a long squared decay behind it, fading to black
 *
 * Between passes there is a short dark beat, which makes each sweep feel
 * deliberate instead of a conveyor belt. Everything not in the band is off.
 */
static void fxTraverse(const EffectCtx &c, RGB *out, uint16_t len) {
    if (!len) return;

    const uint16_t total = c.chainTotal ? c.chainTotal : len;
    const uint32_t rate  = pixelRate(c.speed);
    const uint16_t band  = max<uint16_t>(8, total / 8);       // crest width
    const uint16_t wake  = max<uint16_t>(band * 2, total / 4);// trailing glow
    const int32_t  halfQ = Q8(band) / 2;
    const int32_t  wakeQ = Q8(wake);

    const uint32_t travelMs = (uint32_t)(total + band + wake) * 1000UL / rate;
    const uint32_t holdMs   = travelMs / 7;                   // dark beat
    const uint32_t cycle    = travelMs + holdMs;
    if (!cycle) return;

    uint32_t t = c.now % cycle;
    fillSolid(out, len, RGB_BLACK);
    if (t >= travelMs) return;                                // between passes

    /* head position, started far enough back that the wake enters smoothly */
    int32_t posQ8 = (int32_t)((uint64_t)t * rate * 256ULL / 1000ULL) - wakeQ;

    const RGB base = c.color;
    const RGB core = tintOf(base, 170);                       // same hue, near white

    for (uint16_t i = 0; i < len; i++) {
        int32_t d = posQ8 - (Q8(c.chainOffset + i) + 128);    // >0 = behind the head

        uint8_t lv = 0;
        if (d < 0) {                                          // ahead of the head
            if (-d < halfQ) lv = cos8((uint8_t)((127L * (-d)) / halfQ));
        } else if (d < halfQ) {                               // the crest
            lv = cos8((uint8_t)((127L * d) / halfQ));
        } else if (d < wakeQ) {                               // the wake
            uint8_t b = 255 - (uint8_t)((255L * (d - halfQ)) / (wakeQ - halfQ));
            lv = scale8(scale8(b, b), 165);                   // squared decay
        }
        if (!lv) continue;

        /* gradient inside one hue: crest whitens, body is the pure colour,
           and the falloff carries it down to black */
        RGB col = (lv >= 170) ? blendColor(base, core, (uint8_t)((lv - 170) * 3)) : base;
        out[i] = scaleColorVideo(col, lv);
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
        /* no floor: troughs fall to black, so the light visibly moves rather
           than the whole word sitting lit and pulsing */
        v = scale8(v, 255);

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
    RGB head = tintOf(c.color, 70);

    for (uint16_t i = 0; i < len; i++) {
        int32_t d = posQ8 - (Q8(i) + 128);
        if (d >= 0 && d < tailQ8) {
            uint8_t b = 255 - (uint8_t)((255L * d) / tailQ8);
            b = scale8(b, b);                        // squared falloff
            out[i] = scaleColorVideo(head, b);       // tail fades out to black
        } else {
            out[i] = RGB_BLACK;                      // everything else is off
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
