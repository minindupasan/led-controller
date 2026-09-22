#include "Color.h"

const uint8_t SIN8_TABLE[256] PROGMEM = {
    128, 131, 134, 137, 140, 144, 147, 150, 153, 156, 159, 162, 165, 168, 171, 174,
    177, 179, 182, 185, 188, 191, 193, 196, 199, 201, 204, 206, 209, 211, 213, 216,
    218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 239, 240, 241, 243, 244,
    245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255,
    255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246,
    245, 244, 243, 241, 240, 239, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220,
    218, 216, 213, 211, 209, 206, 204, 201, 199, 196, 193, 191, 188, 185, 182, 179,
    177, 174, 171, 168, 165, 162, 159, 156, 153, 150, 147, 144, 140, 137, 134, 131,
    128, 125, 122, 119, 116, 112, 109, 106, 103, 100,  97,  94,  91,  88,  85,  82,
     79,  77,  74,  71,  68,  65,  63,  60,  57,  55,  52,  50,  47,  45,  43,  40,
     38,  36,  34,  32,  30,  28,  26,  24,  22,  21,  19,  17,  16,  15,  13,  12,
     11,  10,   8,   7,   6,   6,   5,   4,   3,   3,   2,   2,   2,   1,   1,   1,
      1,   1,   1,   1,   2,   2,   2,   3,   3,   4,   5,   6,   6,   7,   8,  10,
     11,  12,  13,  15,  16,  17,  19,  21,  22,  24,  26,  28,  30,  32,  34,  36,
     38,  40,  43,  45,  47,  50,  52,  55,  57,  60,  63,  65,  68,  71,  74,  77,
     79,  82,  85,  88,  91,  94,  97, 100, 103, 106, 109, 112, 116, 119, 122, 125,
};

/* Six-sextant HSV -> RGB. Hue 0-255 wraps the wheel. */
RGB hsv2rgb(uint8_t hue, uint8_t sat, uint8_t val) {
    if (!sat) return RGB(val, val, val);

    uint8_t sextant = hue / 43;              // 256 / 6
    uint8_t offset  = (uint8_t)((hue - sextant * 43) * 6);   // 0..255 inside it

    uint8_t p = scale8(val, 255 - sat);
    uint8_t q = scale8(val, 255 - scale8(sat, offset));
    uint8_t t = scale8(val, 255 - scale8(sat, 255 - offset));

    switch (sextant) {
        case 0:  return RGB(val, t, p);
        case 1:  return RGB(q, val, p);
        case 2:  return RGB(p, val, t);
        case 3:  return RGB(p, q, val);
        case 4:  return RGB(t, p, val);
        default: return RGB(val, p, q);
    }
}

uint8_t rgb2sat(const RGB &c) {
    uint8_t mx = max(c.r, max(c.g, c.b));
    uint8_t mn = min(c.r, min(c.g, c.b));
    return mx ? (uint8_t)(((uint16_t)(mx - mn) * 255) / mx) : 0;
}

uint8_t rgb2hue(const RGB &c) {
    uint8_t mx = max(c.r, max(c.g, c.b));
    uint8_t mn = min(c.r, min(c.g, c.b));
    uint8_t d  = mx - mn;
    if (!d) return 0;

    int16_t h;
    if (mx == c.r)      h = 0   + (43 * ((int16_t)c.g - (int16_t)c.b)) / d;
    else if (mx == c.g) h = 85  + (43 * ((int16_t)c.b - (int16_t)c.r)) / d;
    else                h = 171 + (43 * ((int16_t)c.r - (int16_t)c.g)) / d;
    return (uint8_t)(h & 0xFF);
}

/* Classic fire ramp: the bottom third heats red, the middle adds green to
   reach yellow, the top adds blue to reach white. */
RGB heatColor(uint8_t heat) {
    uint8_t t192 = scale8(heat, 191);
    uint8_t band = t192 & 0x3F;              // 0..63 inside the band
    band <<= 2;                              // scale to 0..252

    if (t192 & 0x80)      return RGB(255, 255, band);    // hottest: white
    else if (t192 & 0x40) return RGB(255, band, 0);      // middle:  yellow
    else                  return RGB(band, 0, 0);        // coolest: red
}
