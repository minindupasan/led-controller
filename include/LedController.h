/*
 * LedController.h - owns the LED strip, composes every segment each
 * frame and applies the debug/test overlay on top.
 */
#pragma once

#include "Config.h"
#include "SegmentManager.h"
#include <NeoPixelBus.h>

/* Output driver: NeoPixelBus on RMT channel 0 - hardware-timed, and the same
   path WLED uses on this board. Its Show() is asynchronous, so a 750-LED
   frame (~22 ms on the wire) does not stall the render loop.
   Colour maths lives in Color.h - nothing here depends on FastLED. */
typedef NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> LedStrip;

class LedController {
public:
    void begin();
    void loop();                       // call as often as possible

    /* debug / test overlay */
    void testOff();
    void testIndex(uint16_t idx, RGB c = RGB_WHITE);
    void testRange(uint16_t a, uint16_t b, RGB c = RGB_WHITE);
    void testAll(RGB c = RGB_WHITE);
    void testWalk(uint16_t delayMs);
    void identify(int segIndex);       // blink one segment white, 3 s
    const TestState &test() const { return _test; }

    /* stats for the debug panel */
    float    fps() const         { return _fps; }
    uint32_t frames() const      { return _frames; }
    uint16_t estimatedMilliamps() const;
    RGB     *raw()               { return _leds; }

    void restartStrip();               // re-init after ledCount change
    uint8_t activePin() const { return _activePin; }   // pin actually driving now

private:
    void renderFrame(uint32_t now);
    void applyTest(uint32_t now);
    void pushToStrip();                // RGB buffer -> driver, with
                                       // brightness and the power cap applied

    LedStrip *_strip = nullptr;        // rebuilt when pin or count changes
    RGB       _leds[MAX_LEDS];
    RGB       _scratch[MAX_LEDS];
    TestState _test;
    uint32_t  _identifyUntil = 0;
    uint32_t  _testUntil = 0;
    uint32_t  _lastFrame = 0;
    uint32_t  _frames = 0;
    uint32_t  _fpsWindow = 0;
    uint32_t  _fpsCount = 0;
    float     _fps = 0;
    uint16_t  _activeCount = 0;
    uint8_t   _tintHue = 0;
    uint8_t   _activePin = DEFAULT_LED_PIN;
    uint8_t   _appliedBrightness = 0;
    uint16_t  _frameBudgetMs = 1000 / TARGET_FPS;
    bool      _started = false;
};

extern LedController Leds;
