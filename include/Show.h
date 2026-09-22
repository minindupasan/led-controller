/*
 * Show.h - the opening sequence.
 *
 *   WAIT    dark, holding for the oil lamp (a separate ESP32) to finish
 *   OPENER  TRAVERSE sweeps INNOV IOT - the epic opener
 *   CALM    settles into BREATHE and stays there
 *   MANUAL  you picked something; the show stops driving
 *
 * The lamp board can later trigger this by sending `show start` over serial
 * or pulling SHOW_TRIGGER_PIN low - see armFromExternal().
 */
#pragma once

#include <Arduino.h>

enum ShowStage : uint8_t { SHOW_MANUAL = 0, SHOW_WAIT, SHOW_OPENER, SHOW_CALM };

class Show {
public:
    void begin();
    void loop();

    void start();                      // run it from the top
    void stop();                       // hand control back to the words
    void armFromExternal();            // hook for the lamp board

    bool        running() const { return _stage != SHOW_MANUAL; }
    ShowStage   stage() const   { return _stage; }
    const char *stageName() const;

    /* What the renderer should use right now. Returns false when the show is
       not driving, in which case each word's own settings apply. */
    bool override(uint8_t wordIndex, uint8_t &anim, uint8_t &fade) const;

private:
    ShowStage _stage = SHOW_MANUAL;
    uint32_t  _since = 0;
    uint8_t   _sweeps = 0;

    void enter(ShowStage s);
};

extern Show TheShow;

/* Timings */
#define SHOW_WAIT_MS       1200        // dark beat before the opener
#define SHOW_SWEEP_COUNT   2           // traverse passes in the opener
#define SHOW_FADE_MS       900         // cross-fade from opener into calm
