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

static inline CRGB scaled(const CRGB &c, uint8_t v) {
    CRGB o = c;
    o.nscale8_video(v);
    return o;
}

static CRGB tinted(const CRGB &c, const RenderCtx &ctx, uint8_t localHue = 0) {
    if (!ctx.useTint) return c;
    CHSV hsv = rgb2hsv_approximate(c);
    hsv.h = ctx.tintHue + localHue;
    if (hsv.s < 60) hsv.s = 200;          // keep white-ish colours visible
    CRGB o;
    hsv2rgb_rainbow(hsv, o);
    return o;
}

/* ------------------------------------------------------------- animations */

static void animSolid(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    CRGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

static void animBreathe(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    uint8_t b = cubicwave8(phase8(c.phaseMs, c.speed));
    uint8_t floorB = 255 - c.intensity;                 // intensity = depth
    b = qadd8(scale8(b, c.intensity), scale8(floorB, 40));
    CRGB col = scaled(tinted(s.color, c), b);
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

static void animPulse(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    /* double heartbeat inside one cycle */
    uint8_t p = phase8(c.phaseMs, c.speed);
    uint8_t b = 0;
    if (p < 64)       b = ease8InOutQuad(p * 4);
    else if (p < 128) b = ease8InOutQuad(255 - (p - 64) * 4);
    else if (p < 176) b = scale8(ease8InOutQuad((p - 128) * 5), 170);
    else if (p < 224) b = scale8(ease8InOutQuad(255 - (p - 176) * 5), 170);
    b = qadd8(scale8(b, c.intensity), 4);
    CRGB col = scaled(tinted(s.color, c), b);
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

/* moving band that runs along the segment */
static void animTraverse(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    if (!len) return;
    uint16_t width = max<uint16_t>(1, (uint16_t)(len * (c.intensity + 16) / 512));
    uint32_t span  = (uint32_t)len + width;
    uint32_t pos   = ((c.phaseMs * (uint32_t)c.speed) / 400UL) % span;
    CRGB col = tinted(s.color, c);
    CRGB bg  = scaled(s.color2, 255);
    for (uint16_t i = 0; i < len; i++) {
        int32_t d = (int32_t)i - (int32_t)pos + width;
        if (d >= 0 && d < (int32_t)width) {
            uint8_t edge = sin8((uint8_t)(255UL * d / width));
            out[i] = scaled(col, qadd8(edge, 40));
        } else {
            out[i] = bg;
        }
    }
}

/* head + exponential tail */
static void animComet(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    if (!len) return;
    uint16_t tail = max<uint16_t>(2, (uint16_t)(len * (c.intensity + 32) / 384));
    uint32_t pos  = ((c.phaseMs * (uint32_t)c.speed) / 400UL) % ((uint32_t)len + tail);
    CRGB head = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) {
        int32_t d = (int32_t)pos - (int32_t)i;
        if (d >= 0 && d < (int32_t)tail) {
            uint8_t b = 255 - (uint8_t)(255UL * d / tail);
            b = scale8(b, b);                       // squared falloff
            out[i] = scaled(head, b);
        } else {
            out[i] = s.color2;
        }
    }
}

/* fill up, then empty */
static void animWipe(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    if (!len) return;
    uint8_t p = phase8(c.phaseMs, c.speed);
    bool filling = p < 128;
    uint8_t t = filling ? p * 2 : (255 - (p - 128) * 2);
    uint16_t lit = (uint32_t)len * t / 255;
    CRGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) out[i] = (i < lit) ? col : s.color2;
}

static void animTheater(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    uint8_t  gap  = 3 + (c.intensity >> 6);                    // 3..6
    uint32_t step = ((c.phaseMs * (uint32_t)c.speed) / 800UL) % gap;
    CRGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++)
        out[i] = ((i + step) % gap == 0) ? col : s.color2;
}

static void animSparkle(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    uint8_t *st = sparkBuf(c.absStart);
    CRGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) {
        uint8_t v = st[i];
        v = qsub8(v, max<uint8_t>(1, scale8(c.speed, 24)));
        if (random8() < scale8(c.intensity, 30)) v = 255;
        st[i]  = v;
        out[i] = blend(s.color2, col, v);
    }
}

static void animTwinkle(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    CRGB col = tinted(s.color, c);
    for (uint16_t i = 0; i < len; i++) {
        uint8_t seed = (uint8_t)(i * 37 + c.segIndex * 11);
        uint8_t w = sin8(phase8(c.phaseMs, c.speed) + seed * 3);
        w = scale8(w, w);
        out[i] = scaled(col, qadd8(scale8(w, c.intensity), 8));
    }
}

static void animRainbow(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    uint8_t spread = 1 + scale8(c.intensity, 24);
    uint8_t h = phase8(c.phaseMs, c.speed);
    for (uint16_t i = 0; i < len; i++)
        out[i] = CHSV(h + i * spread, 240, 255);
}

static void animGradient(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    if (!len) return;
    uint8_t shift = phase8(c.phaseMs, c.speed);
    CRGB a = tinted(s.color, c);
    CRGB b = s.color2 == CRGB(0, 0, 0) ? CRGB(a).nscale8(40) : s.color2;
    for (uint16_t i = 0; i < len; i++) {
        uint8_t t = sin8((uint16_t)(i * 255UL / len) + shift);
        out[i] = blend(a, b, t);
    }
}

static void animStrobe(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
    uint8_t p  = phase8(c.phaseMs, c.speed);
    uint8_t on = 8 + (c.intensity >> 3);                 // duty cycle
    CRGB col = (p < on) ? tinted(s.color, c) : s.color2;
    for (uint16_t i = 0; i < len; i++) out[i] = col;
}

static void animFire(const Segment &s, const RenderCtx &c, CRGB *out, uint16_t len) {
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
        out[i] = HeatColor(heat[i]);
}

/* ------------------------------------------------------------------ engine */

void AnimationEngine::begin() { resetState(); }

void AnimationEngine::resetState() {
    ::memset(g_heat, 0, sizeof(g_heat));
    ::memset(g_spark, 0, sizeof(g_spark));
}

void AnimationEngine::render(uint8_t anim, const Segment &seg, const RenderCtx &ctx,
                             CRGB *out, uint16_t len) {
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
        case ANIM_OFF:
        default:            fill_solid(out, len, CRGB::Black); break;
    }
}
