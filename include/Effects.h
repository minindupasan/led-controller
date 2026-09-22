/*
 * Effects.h - the four animations, rendered per word.
 *
 * Each effect fills a word-local buffer (0..len-1, the word's letters laid
 * end to end). LedController maps those back to strip indices, so effects
 * never deal with letter boundaries, gaps or reversed wiring - which is what
 * lets a band cross letters with no pause.
 */
#pragma once

#include "Config.h"

struct EffectCtx {
    uint32_t now;          // millis()
    uint8_t  speed;        // 1-255
    RGB      color;        // the word's colour
    uint8_t  fade;         // 0-255 master fade, used by the show for entries

    /* Normally a word animates within itself (offset 0, total == len). The
       show's opener sets these so INNOV and IOT form one continuous run and
       the band flows from one word straight into the next. */
    uint16_t chainOffset;
    uint16_t chainTotal;
};

namespace Effects {
    void render(uint8_t anim, const EffectCtx &ctx, RGB *out, uint16_t len);
}
