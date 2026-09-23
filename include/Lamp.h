/*
 * Logo.h - the oil lamp: a second WS2812B run on its own GPIO, driven from
 * this board on RMT channel 1.
 *
 * The lamp is split into segments (like the letters) and lit level by level:
 * `logo level 7` lights the first seven. An external Arduino raises the level
 * over the AP as the lamp fills - see /api/cmd in Net.cpp:
 *
 *     GET http://192.168.4.1/api/cmd?c=logo+level+7
 *
 * When the level reaches the last segment the lamp is "full", which is the
 * cue for the sign's opener.
 */
#pragma once

#include "Config.h"
#include "Sign.h"
#include <NeoPixelBus.h>

typedef NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt1Ws2812xMethod> LogoStrip;

class LogoController {
public:
    void begin();
    void loop();
    void restartStrip();

    /* control ---------------------------------------------------------
     * Cards light arbitrary segments, not sequential ones, so what is lit is
     * a bitmask rather than a level. `level()` is still reported as the
     * number lit, which is what the UI and telemetry show. */
    void    lightSegment(int i);       // light one, with its fade-in
    void    clearSegment(int i);
    bool    isLit(int i) const;
    void    setLevel(uint8_t n);       // light the first n (manual / testing)
    void    step(int8_t delta);
    uint8_t level() const;             // how many are lit
    bool    full() const;              // every enabled segment is lit
    void    allOn();
    void    allOff();

    /* RFID ------------------------------------------------------------- */
    int     tapCard(const String &uid);   // -1 unknown, -2 consumed by learn
    void    learn(int segIndex);          // next tap assigns to this segment
    int     learning() const { return _learn; }
    const String &lastCard() const { return _lastCard; }
    uint32_t litMask() const { return _lit; }
    void    setGlow(bool on) { _glow = on; }
    bool    glow() const { return _glow; }

    /* mapping -------------------------------------------------------- */
    void testOff();
    void testIndex(uint16_t idx, RGB c = RGB_WHITE);
    void testRange(uint16_t a, uint16_t b, RGB c = RGB_WHITE);
    void testWalk(uint16_t delayMs);
    void identify(int segIndex);
    const TestState &test() const { return _test; }

    uint8_t activePin() const { return _activePin; }
    uint16_t ledCount() const { return _activeCount; }

private:
    void render(uint32_t now);
    void applyTest(uint32_t now);

    LogoStrip *_strip = nullptr;
    RGB        _leds[MAX_LOGO_LEDS];
    TestState  _test;

    uint32_t  _lit = 0;                 // one bit per segment
    uint32_t  _litAt[MAX_LOGO_SEGS] = {0};
    bool      _glow = false;
    int8_t    _learn = -1;              // segment waiting for a card
    String    _lastCard;
    uint32_t  _identifyUntil = 0;
    uint32_t  _testUntil = 0;
    uint32_t  _lastFrame = 0;
    uint16_t  _activeCount = 0;
    uint8_t   _activePin = DEFAULT_LOGO_PIN;
};

extern LogoController Logo;
