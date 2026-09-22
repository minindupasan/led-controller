/*
 * LedController.h - owns the FastLED strip, composes every segment each
 * frame and applies the debug/test overlay on top.
 */
#pragma once

#include "Config.h"
#include "SegmentManager.h"

class LedController {
public:
    void begin();
    void loop();                       // call as often as possible

    /* debug / test overlay */
    void testOff();
    void testIndex(uint16_t idx, CRGB c = CRGB::White);
    void testRange(uint16_t a, uint16_t b, CRGB c = CRGB::White);
    void testAll(CRGB c = CRGB::White);
    void testWalk(uint16_t delayMs);
    void identify(int segIndex);       // blink one segment white, 3 s
    const TestState &test() const { return _test; }

    /* stats for the debug panel */
    float    fps() const         { return _fps; }
    uint32_t frames() const      { return _frames; }
    uint16_t estimatedMilliamps() const;
    CRGB    *raw()               { return _leds; }

    void restartStrip();               // re-init after ledCount change

private:
    void renderFrame(uint32_t now);
    void applyTest(uint32_t now);

    CRGB      _leds[MAX_LEDS];
    CRGB      _scratch[MAX_LEDS];
    TestState _test;
    uint32_t  _identifyUntil = 0;
    uint32_t  _lastFrame = 0;
    uint32_t  _frames = 0;
    uint32_t  _fpsWindow = 0;
    uint32_t  _fpsCount = 0;
    float     _fps = 0;
    uint16_t  _activeCount = 0;
    uint8_t   _tintHue = 0;
    bool      _started = false;
};

extern LedController Leds;
