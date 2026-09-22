#include "LedController.h"
#include "Effects.h"
#include "Show.h"

LedController Leds;

void LedController::begin() {
    ::memset(_leds, 0, sizeof(_leds));
    ::memset(_scratch, 0, sizeof(_scratch));
    restartStrip();
}

void LedController::restartStrip() {
    Settings &s = TheSign.settings();
    _activeCount = constrain(s.ledCount, (uint16_t)1, (uint16_t)MAX_LEDS);
    _activePin   = isValidLedPin(s.dataPin) ? s.dataPin : DEFAULT_LED_PIN;

    if (_strip) { delete _strip; _strip = nullptr; }
    _strip = new LedStrip(_activeCount, _activePin);
    _strip->Begin();
    _strip->ClearTo(RgbColor(0, 0, 0));
    _strip->Show();
    _started = true;

    Serial.printf("[led] %u LEDs on GPIO%u (NeoPixelBus/RMT0), ~%u ms/frame\n",
                  _activeCount, _activePin,
                  (unsigned)(((uint32_t)_activeCount * LED_US_PER_PIXEL) / 1000 + 1));
}

/* ----------------------------------------------------------------- overlay */
void LedController::testOff() { _test = TestState(); _identifyUntil = 0; _testUntil = 0; }

void LedController::testIndex(uint16_t idx, RGB c) {
    _testUntil = millis() + TEST_TIMEOUT_MS;
    _test.mode  = TEST_INDEX;
    _test.index = min<uint16_t>(idx, _activeCount - 1);
    _test.color = c;
}

void LedController::testRange(uint16_t a, uint16_t b, RGB c) {
    _testUntil = millis() + TEST_TIMEOUT_MS;
    if (b < a) { uint16_t t = a; a = b; b = t; }
    _test.mode     = TEST_RANGE;
    _test.index    = min<uint16_t>(a, _activeCount - 1);
    _test.rangeEnd = min<uint16_t>(b, _activeCount - 1);
    _test.color    = c;
}

void LedController::testAll(RGB c) {
    _testUntil = millis() + TEST_TIMEOUT_MS;
    _test.mode  = TEST_ALL;
    _test.color = c;
}

void LedController::testWalk(uint16_t delayMs) {
    _testUntil = millis() + TEST_TIMEOUT_MS;
    _test.mode      = TEST_WALK;
    _test.index     = 0;
    _test.walkDelay = max<uint16_t>(30, delayMs);
    _test.lastStep  = millis();
    _test.color     = RGB_WHITE;
}

void LedController::identify(int letterIndex) {
    if (!TheSign.letter(letterIndex)) return;
    _test.mode   = TEST_IDENTIFY;
    _test.letter = letterIndex;
    _identifyUntil = millis() + 3000;
}

/* ------------------------------------------------------------------- frame */
void LedController::loop() {
    /* Show() is asynchronous; CanShow() alone paces us at the wire's maximum.
       Gating on millis() as well made the two race and halved the rate. */
    if (_strip && !_strip->CanShow()) return;
    uint32_t now = millis();
    if (now == _lastFrame) return;
    _lastFrame = now;

    renderFrame(now);
    applyTest(now);
    pushToStrip();

    _frames++;
    _fpsCount++;
    if (now - _fpsWindow >= 1000) {
        _fps = _fpsCount * 1000.0f / (now - _fpsWindow);
        _fpsWindow = now;
        _fpsCount = 0;
    }
}

void LedController::renderFrame(uint32_t now) {
    Settings &st = TheSign.settings();
    fillSolid(_leds, _activeCount, RGB_BLACK);
    if (!st.power) return;

    /* INNOV and IOT are always treated as one continuous run, so a band that
       leaves the V carries straight on into the I of IOT instead of each word
       restarting its own copy of the animation. Each word keeps its own
       colour, so the light changes hue as it crosses the seam. */
    uint16_t chainTotal = 0;
    for (uint8_t w = 0; w < TheSign.wordCount(); w++) chainTotal += TheSign.wordLength(w);
    uint16_t chainAt = 0;

    for (uint8_t w = 0; w < TheSign.wordCount(); w++) {
        Word *wd = TheSign.word(w);
        if (!wd) continue;
        uint16_t len = TheSign.wordLength(w);
        if (!len) continue;

        uint8_t anim = wd->animation;
        uint8_t fade = 255;
        TheShow.override(w, anim, fade);

        EffectCtx ctx {
            .now   = now,
            .speed = st.speed,
            .color = wd->color,
            .fade  = fade,
            .chainOffset = chainAt,
            .chainTotal  = chainTotal
        };

        Effects::render(anim, ctx, _scratch, len);

        /* word-local buffer -> strip, letter by letter, honouring reversed */
        uint16_t local = 0;
        for (uint8_t k = 0; k < wd->count; k++) {
            Letter *l = TheSign.letter(wd->first + k);
            if (!l || !l->enabled || l->start >= _activeCount) continue;
            uint16_t n = l->length();
            if (l->start + n > _activeCount) n = _activeCount - l->start;

            for (uint16_t i = 0; i < n && local < len; i++, local++) {
                uint16_t dst = l->reversed ? (l->start + n - 1 - i) : (l->start + i);
                _leds[dst] = _scratch[local];
            }
        }
        chainAt += len;
    }
}

void LedController::applyTest(uint32_t now) {
    /* A test overlay paints over the animation every frame, so a forgotten
       one looks exactly like a dead controller. Let it go by itself. */
    if (_test.mode != TEST_NONE && _test.mode != TEST_IDENTIFY &&
        _testUntil && now > _testUntil) {
        Serial.println(F("[led] test overlay timed out"));
        testOff();
        return;
    }

    switch (_test.mode) {
        case TEST_NONE:
            return;

        case TEST_INDEX:
            fillSolid(_leds, _activeCount, RGB_BLACK);
            if (_test.index < _activeCount) _leds[_test.index] = _test.color;
            break;

        case TEST_RANGE:
            fillSolid(_leds, _activeCount, RGB_BLACK);
            for (uint16_t i = _test.index; i <= _test.rangeEnd && i < _activeCount; i++)
                _leds[i] = _test.color;
            break;

        case TEST_ALL:
            fillSolid(_leds, _activeCount, _test.color);
            break;

        case TEST_WALK:
            if (now - _test.lastStep >= _test.walkDelay) {
                _test.lastStep = now;
                _test.index = (_test.index + 1) % _activeCount;
            }
            fillSolid(_leds, _activeCount, RGB_BLACK);
            _leds[_test.index] = _test.color;
            for (uint16_t i = 0; i < _activeCount; i += 10)     // counting marks
                if (i != _test.index) _leds[i] = RGB(12, 12, 12);
            break;

        case TEST_IDENTIFY: {
            if (now > _identifyUntil) { testOff(); return; }
            Letter *l = TheSign.letter(_test.letter);
            if (!l) { testOff(); return; }
            bool on = ((now / 200) & 1);
            for (uint16_t i = l->start; i <= l->end && i < _activeCount; i++)
                _leds[i] = on ? RGB_WHITE : RGB_BLACK;
            break;
        }
    }
}

/* Master brightness and the power cap, neither of which is the driver's job. */
void LedController::pushToStrip() {
    if (!_strip) return;
    Settings &st = TheSign.settings();

    bool lit = st.power || _test.mode != TEST_NONE;   // testing overrides power
    uint8_t bright = lit ? st.brightness : 0;

    if (bright && st.maxMilliamps > 0) {
        uint32_t sum = 0;
        for (uint16_t i = 0; i < _activeCount; i++)
            sum += _leds[i].r + _leds[i].g + _leds[i].b;
        uint32_t fullMa = (sum * 20UL) / 255UL + _activeCount;
        uint32_t wantMa = (fullMa * bright) / 255UL;
        if (wantMa > st.maxMilliamps && fullMa)
            bright = (uint8_t)((uint32_t)st.maxMilliamps * 255UL / fullMa);
    }

    for (uint16_t i = 0; i < _activeCount; i++) {
        RGB c = _leds[i];
        if (bright < 255) c = scaleColorVideo(c, bright);
        _strip->SetPixelColor(i, RgbColor(c.r, c.g, c.b));
    }
    _appliedBrightness = bright;
    _strip->Show();
}

uint16_t LedController::estimatedMilliamps() const {
    uint32_t sum = 0;
    for (uint16_t i = 0; i < _activeCount; i++)
        sum += _leds[i].r + _leds[i].g + _leds[i].b;
    uint32_t ma = (sum * 20UL * _appliedBrightness) / (255UL * 255UL);
    return (uint16_t)min<uint32_t>(ma + _activeCount, 65535);
}
