#include "LedController.h"
#include "AnimationEngine.h"

LedController Leds;

void LedController::begin() {
    ::memset(_leds, 0, sizeof(_leds));
    ::memset(_scratch, 0, sizeof(_scratch));
    AnimationEngine::begin();
    restartStrip();
}

void LedController::restartStrip() {
    GlobalSettings &g = Segments.globals();
    _activeCount = constrain(g.ledCount, (uint16_t)1, (uint16_t)MAX_LEDS);

    if (!_started) {
        FastLED.addLeds<WS2812B, LED_DATA_PIN, LED_COLOR_ORDER>(_leds, MAX_LEDS)
               .setCorrection(TypicalLEDStrip);
        _started = true;
    }
    FastLED.setMaxPowerInVoltsAndMilliamps(PSU_VOLTS, g.maxMilliamps);
    FastLED.setBrightness(g.brightness);
    FastLED.clear(true);
    AnimationEngine::resetState();
    Serial.printf("[led] strip ready: %u LEDs on GPIO%d\n", _activeCount, LED_DATA_PIN);
}

/* ----------------------------------------------------------------- overlay */
void LedController::testOff()  { _test = TestState(); _identifyUntil = 0; }

void LedController::testIndex(uint16_t idx, CRGB c) {
    _test.mode  = TEST_INDEX;
    _test.index = min<uint16_t>(idx, _activeCount - 1);
    _test.color = c;
}

void LedController::testRange(uint16_t a, uint16_t b, CRGB c) {
    if (b < a) { uint16_t t = a; a = b; b = t; }
    _test.mode     = TEST_RANGE;
    _test.index    = min<uint16_t>(a, _activeCount - 1);
    _test.rangeEnd = min<uint16_t>(b, _activeCount - 1);
    _test.color    = c;
}

void LedController::testAll(CRGB c) {
    _test.mode  = TEST_ALL;
    _test.color = c;
}

void LedController::testWalk(uint16_t delayMs) {
    _test.mode      = TEST_WALK;
    _test.index     = 0;
    _test.walkDelay = max<uint16_t>(30, delayMs);
    _test.lastStep  = millis();
    _test.color     = CRGB::White;
}

void LedController::identify(int segIndex) {
    const Segment *s = Segments.get(segIndex);
    if (!s) return;
    _test.mode    = TEST_IDENTIFY;
    _test.segment = segIndex;
    _identifyUntil = millis() + 3000;
}

/* ------------------------------------------------------------------- frame */
void LedController::loop() {
    uint32_t now = millis();
    if (now - _lastFrame < (1000 / TARGET_FPS)) return;
    _lastFrame = now;

    renderFrame(now);
    applyTest(now);

    /* testing always lights the strip, even with master power off */
    GlobalSettings &g = Segments.globals();
    bool lit = g.power || _test.mode != TEST_NONE;
    FastLED.setBrightness(lit ? g.brightness : 0);
    FastLED.show();

    _frames++;
    _fpsCount++;
    if (now - _fpsWindow >= 1000) {
        _fps = _fpsCount * 1000.0f / (now - _fpsWindow);
        _fpsWindow = now;
        _fpsCount = 0;
    }
}

void LedController::renderFrame(uint32_t now) {
    GlobalSettings &g = Segments.globals();
    fill_solid(_leds, MAX_LEDS, CRGB::Black);
    if (!g.power) return;

    if (g.rainbowTint) _tintHue = (uint8_t)((now / 24) & 0xFF);

    const uint8_t n = Segments.count();
    for (uint8_t i = 0; i < n; i++) {
        Segment *s = Segments.get(i);
        if (!s || !s->enabled) continue;

        uint16_t len = s->length();
        if (!len || s->start >= _activeCount) continue;
        if (s->start + len > _activeCount) len = _activeCount - s->start;

        uint8_t anim = (s->animation == ANIM_INHERIT) ? g.animation : s->animation;

        /* play-mode handling -------------------------------------------- */
        uint32_t phase = now;
        if (g.playMode == PLAY_STAGGER) {
            phase = now + (uint32_t)i * g.stagger;
        } else if (g.playMode == PLAY_SEQUENCE && n > 0) {
            uint32_t slot = max<uint16_t>(120, g.stagger * 4);
            uint8_t  active = (now / slot) % n;
            if (active != i) continue;              // only one segment lit
        }

        RenderCtx ctx {
            .now       = now,
            .phaseMs   = phase,
            .speed     = g.speed,
            .intensity = g.intensity,
            .tintHue   = (uint8_t)(_tintHue + i * 16),
            .useTint   = g.rainbowTint,
            .segIndex  = i,
            .absStart  = s->start
        };

        AnimationEngine::render(anim, *s, ctx, _scratch, len);

        for (uint16_t k = 0; k < len; k++) {
            uint16_t dst = s->reversed ? (s->start + len - 1 - k) : (s->start + k);
            CRGB px = _scratch[k];
            if (s->brightness < 255) px.nscale8_video(s->brightness);
            _leds[dst] = px;
        }
    }
}

void LedController::applyTest(uint32_t now) {
    switch (_test.mode) {
        case TEST_NONE:
            return;

        case TEST_INDEX:
            fill_solid(_leds, _activeCount, CRGB::Black);
            if (_test.index < _activeCount) _leds[_test.index] = _test.color;
            break;

        case TEST_RANGE:
            fill_solid(_leds, _activeCount, CRGB::Black);
            for (uint16_t i = _test.index; i <= _test.rangeEnd && i < _activeCount; i++)
                _leds[i] = _test.color;
            break;

        case TEST_ALL:
            fill_solid(_leds, _activeCount, _test.color);
            break;

        case TEST_WALK:
            if (now - _test.lastStep >= _test.walkDelay) {
                _test.lastStep = now;
                _test.index = (_test.index + 1) % _activeCount;
            }
            fill_solid(_leds, _activeCount, CRGB::Black);
            _leds[_test.index] = _test.color;
            /* soft markers every 10 LEDs so counting is easy */
            for (uint16_t i = 0; i < _activeCount; i += 10)
                if (i != _test.index) _leds[i] = CRGB(12, 12, 12);
            break;

        case TEST_IDENTIFY: {
            if (now > _identifyUntil) { testOff(); return; }
            const Segment *s = Segments.get(_test.segment);
            if (!s) { testOff(); return; }
            bool on = ((now / 200) & 1);
            for (uint16_t i = s->start; i <= s->end && i < _activeCount; i++)
                _leds[i] = on ? CRGB::White : CRGB::Black;
            break;
        }
    }
}

uint16_t LedController::estimatedMilliamps() const {
    uint32_t sum = 0;
    for (uint16_t i = 0; i < _activeCount; i++)
        sum += _leds[i].r + _leds[i].g + _leds[i].b;
    /* ~20 mA per channel at full, scaled by master brightness */
    uint32_t ma = (sum * 20UL * FastLED.getBrightness()) / (255UL * 255UL);
    return (uint16_t)min<uint32_t>(ma + _activeCount, 65535);
}
