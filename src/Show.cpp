#include "Show.h"
#include "Sign.h"
#include "Config.h"
#include "Logger.h"

Show TheShow;

void Show::begin() {
    if (TheSign.settings().autoShow) start();
    else                             _stage = SHOW_MANUAL;
}

void Show::start() {
    _sweeps = 0;
    enter(SHOW_WAIT);
    Log.log("[show] armed - waiting, then opener");
}

void Show::stop() {
    if (_stage != SHOW_MANUAL) Log.log("[show] manual control");
    _stage = SHOW_MANUAL;
}

void Show::armFromExternal() { start(); }   // lamp board trigger lands here

void Show::enter(ShowStage s) {
    _stage = s;
    _since = millis();
}

const char *Show::stageName() const {
    switch (_stage) {
        case SHOW_WAIT:   return "wait";
        case SHOW_OPENER: return "opener";
        case SHOW_CALM:   return "calm";
        default:          return "manual";
    }
}

/* How long one traverse pass takes, so the opener can count sweeps. The
   formula mirrors fxTraverse(): span pixels at pixelRate(speed). */
static uint32_t sweepMs() {
    uint16_t len = max<uint16_t>(TheSign.wordLength(0) + TheSign.wordLength(1), 1);
    uint32_t rate = 30UL + (uint32_t)TheSign.settings().speed * 4UL;
    return (uint32_t)(len + len / 6 + 2) * 1000UL / rate;
}

void Show::loop() {
    if (_stage == SHOW_MANUAL) return;
    uint32_t elapsed = millis() - _since;

    switch (_stage) {
        case SHOW_WAIT:
            if (elapsed >= SHOW_WAIT_MS) {
                enter(SHOW_OPENER);
                Log.log("[show] opener");
            }
            break;

        case SHOW_OPENER:
            if (elapsed >= sweepMs() * SHOW_SWEEP_COUNT) {
                enter(SHOW_CALM);
                Log.log("[show] calm");
            }
            break;

        default:
            break;
    }
}

bool Show::override(uint8_t wordIndex, uint8_t &anim, uint8_t &fade) const {
    uint32_t elapsed = millis() - _since;

    switch (_stage) {
        case SHOW_WAIT:
            anim = ANIM_OFF;
            fade = 0;
            return true;

        case SHOW_OPENER:
            anim = ANIM_TRAVERSE;
            /* ease in over the first fifth of a sweep so it arrives, rather
               than snapping on */
            fade = (elapsed < 300) ? (uint8_t)((elapsed * 255UL) / 300UL) : 255;
            return true;

        case SHOW_CALM:
            anim = ANIM_BREATHE;
            fade = (elapsed < SHOW_FADE_MS)
                     ? (uint8_t)(180 + (elapsed * 75UL) / SHOW_FADE_MS)
                     : 255;
            return true;

        default:
            return false;
    }
}
