/*
 * LedController.h - owns the strip, renders each word, applies the test
 * overlay, and pushes frames out.
 */
#pragma once

#include "Config.h"
#include "Sign.h"
#include <NeoPixelBus.h>

/* NeoPixelBus on RMT channel 0 - hardware timed, and the driver WLED uses on
   this board. Show() is asynchronous, so a 591-LED frame (~18 ms on the wire)
   does not stall the render loop. */
typedef NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> LedStrip;

class LedController {
public:
    void begin();
    void loop();
    void restartStrip();               // re-init after a pin or count change

    /* mapping / debug overlay */
    void testOff();
    void testIndex(uint16_t idx, RGB c = RGB_WHITE);
    void testRange(uint16_t a, uint16_t b, RGB c = RGB_WHITE);
    void testAll(RGB c = RGB_WHITE);
    void testWalk(uint16_t delayMs);
    void identify(int letterIndex);
    const TestState &test() const { return _test; }

    float    fps() const    { return _fps; }
    uint32_t frames() const { return _frames; }
    uint8_t  activePin() const { return _activePin; }
    uint16_t estimatedMilliamps() const;

private:
    void renderFrame(uint32_t now);
    void applyTest(uint32_t now);
    void pushToStrip();

    LedStrip *_strip = nullptr;
    RGB       _leds[MAX_LEDS];
    RGB       _scratch[MAX_LEDS];      // one word at a time
    TestState _test;

    uint32_t _identifyUntil = 0;
    uint32_t _testUntil = 0;
    uint32_t _lastFrame = 0;
    uint32_t _frames = 0;
    uint32_t _fpsWindow = 0;
    uint32_t _fpsCount = 0;
    float    _fps = 0;
    uint16_t _activeCount = 0;
    uint8_t  _activePin = DEFAULT_LED_PIN;
    uint8_t  _appliedBrightness = 0;
    bool     _started = false;
};

extern LedController Leds;
