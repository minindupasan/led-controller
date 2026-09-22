/*
 * Color.h - 8-bit colour type and fixed-point maths.
 *
 * Replaces the parts of FastLED this project actually used (CRGB, scale8,
 * sin8, easing, blending, HSV, the fire palette) so the firmware depends on
 * nothing but the output driver.
 *
 * All maths is integer: no floats in the render path.
 */
#pragma once

#include <Arduino.h>

/* ----------------------------------------------------------------- colour */
struct RGB {
    uint8_t r, g, b;

    RGB() : r(0), g(0), b(0) {}
    RGB(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    explicit RGB(uint32_t hex)
        : r((hex >> 16) & 0xFF), g((hex >> 8) & 0xFF), b(hex & 0xFF) {}

    bool     isBlack() const { return !r && !g && !b; }
    uint32_t toHex()   const { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
    bool operator==(const RGB &o) const { return r == o.r && g == o.g && b == o.b; }
};

static const RGB RGB_BLACK(0, 0, 0);
static const RGB RGB_WHITE(255, 255, 255);

/* ------------------------------------------------------------ 8-bit maths */

/* (i * scale) / 256 */
static inline uint8_t scale8(uint8_t i, uint8_t scale) {
    return ((uint16_t)i * (uint16_t)(1 + scale)) >> 8;
}

/* like scale8, but never fades a non-zero value all the way to zero */
static inline uint8_t scale8_video(uint8_t i, uint8_t scale) {
    uint8_t v = (((uint16_t)i * (uint16_t)scale) >> 8) + ((i && scale) ? 1 : 0);
    return v;
}

static inline uint8_t qadd8(uint8_t a, uint8_t b) {
    uint16_t t = (uint16_t)a + (uint16_t)b;
    return t > 255 ? 255 : (uint8_t)t;
}

static inline uint8_t qsub8(uint8_t a, uint8_t b) {
    int16_t t = (int16_t)a - (int16_t)b;
    return t < 0 ? 0 : (uint8_t)t;
}

static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t frac) {
    return (a > b) ? a - scale8(a - b, frac) : a + scale8(b - a, frac);
}

/* ------------------------------------------------------------- waveforms */
extern const uint8_t SIN8_TABLE[256] PROGMEM;

static inline uint8_t sin8(uint8_t theta) { return pgm_read_byte(&SIN8_TABLE[theta]); }
static inline uint8_t cos8(uint8_t theta) { return sin8(theta + 64); }

/* 0..255..0 linear ramp */
static inline uint8_t triwave8(uint8_t in) {
    return (in & 0x80) ? (uint8_t)((255 - in) * 2) : (uint8_t)(in * 2);
}

/* eased ramps - smoother than a triangle, cheaper than a sine */
static inline uint8_t ease8InOutQuad(uint8_t i) {
    uint8_t j = (i & 0x80) ? (uint8_t)(255 - i) : i;
    uint8_t jj2 = scale8(j, j) << 1;
    return (i & 0x80) ? (uint8_t)(255 - jj2) : jj2;
}

/* smoothstep, 3x^2 - 2x^3 */
static inline uint8_t ease8InOutCubic(uint8_t i) {
    uint16_t x  = i;
    uint16_t xx = (x * x) >> 8;
    uint16_t xxx = (xx * x) >> 8;
    int32_t  r  = 3 * (int32_t)xx - 2 * (int32_t)xxx;
    return (uint8_t)constrain(r, 0, 255);
}

static inline uint8_t cubicwave8(uint8_t in) { return ease8InOutCubic(triwave8(in)); }
static inline uint8_t quadwave8(uint8_t in) { return ease8InOutQuad(triwave8(in)); }

/* ---------------------------------------------------------------- random */
static inline uint8_t random8()            { return (uint8_t)(esp_random() & 0xFF); }
static inline uint8_t random8(uint8_t lim) { return lim ? (uint8_t)(random8() % lim) : 0; }
static inline uint8_t random8(uint8_t lo, uint8_t hi) {
    return (hi <= lo) ? lo : (uint8_t)(lo + random8(hi - lo));
}

/* ------------------------------------------------------- colour operations */
static inline RGB scaleColor(const RGB &c, uint8_t v) {
    return RGB(scale8(c.r, v), scale8(c.g, v), scale8(c.b, v));
}

/* keeps dim colours visible instead of snapping them to black */
static inline RGB scaleColorVideo(const RGB &c, uint8_t v) {
    return RGB(scale8_video(c.r, v), scale8_video(c.g, v), scale8_video(c.b, v));
}

static inline RGB blendColor(const RGB &a, const RGB &b, uint8_t frac) {
    return RGB(lerp8(a.r, b.r, frac), lerp8(a.g, b.g, frac), lerp8(a.b, b.b, frac));
}

static inline void fillSolid(RGB *buf, uint16_t n, const RGB &c) {
    for (uint16_t i = 0; i < n; i++) buf[i] = c;
}

RGB     hsv2rgb(uint8_t hue, uint8_t sat, uint8_t val);
uint8_t rgb2hue(const RGB &c);
uint8_t rgb2sat(const RGB &c);
RGB     heatColor(uint8_t heat);          // black -> red -> yellow -> white
