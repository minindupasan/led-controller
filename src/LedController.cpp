#include "LedController.h"
#include "AnimationEngine.h"

LedController Leds;

void LedController::begin() {
    ::memset(_leds, 0, sizeof(_leds));
    ::memset(_scratch, 0, sizeof(_scratch));
    AnimationEngine::begin();
    restartStrip();   // creates the output driver
}

void LedController::restartStrip() {
    GlobalSettings &g = Segments.globals();
    _activeCount = constrain(g.ledCount, (uint16_t)1, (uint16_t)MAX_LEDS);
    _activePin   = isValidLedPin(g.dataPin) ? g.dataPin : DEFAULT_LED_PIN;

    /* The driver takes pin and length as plain arguments, so changing either
       is just a matter of rebuilding it - no reboot, no template per pin. */
    if (_strip) { delete _strip; _strip = nullptr; }
    _strip = new LedStrip(_activeCount, _activePin);
    _strip->Begin();
    _strip->ClearTo(RgbColor(0, 0, 0));
    _strip->Show();

    /* One frame cannot go out faster than the wire allows, so pace the loop
       to whichever is slower: TARGET_FPS, or the strip's own clock-out time. */
    uint16_t wireMs = ((uint32_t)_activeCount * LED_US_PER_PIXEL) / 1000 + 1;
    _frameBudgetMs = max<uint16_t>(1000 / TARGET_FPS, wireMs);

    _started = true;
    AnimationEngine::resetState();
    Serial.printf("[led] strip ready: %u LEDs on GPIO%u (NeoPixelBus/RMT0), "
                  "%u ms/frame -> max %u fps\n",
                  _activeCount, _activePin, _frameBudgetMs, 1000 / _frameBudgetMs);
}

/* Copies the rendered RGB frame out to the strip, applying master brightness
   and a power cap - neither is the output driver's job. */
void LedController::pushToStrip() {
    if (!_strip) return;
    GlobalSettings &g = Segments.globals();

    bool lit = g.power || _test.mode != TEST_NONE;   // testing overrides power
    uint8_t bright = lit ? g.brightness : 0;

    /* Power cap: estimate the draw at full brightness, then scale the whole
       frame down if it would exceed the budget. ~20 mA per channel, plus
       ~1 mA per LED of quiescent draw.
       maxMilliamps == 0 disables this entirely, so `bright 255` really is
       full output - the PSU then has to be sized for it. */
    if (bright && g.maxMilliamps > 0) {
        uint32_t sum = 0;
        for (uint16_t i = 0; i < _activeCount; i++)
            sum += _leds[i].r + _leds[i].g + _leds[i].b;
        uint32_t fullMa = (sum * 20UL) / 255UL + _activeCount;
        uint32_t wantMa = (fullMa * bright) / 255UL;
        if (wantMa > g.maxMilliamps && fullMa)
            bright = (uint8_t)((uint32_t)g.maxMilliamps * 255UL / fullMa);
    }

    for (uint16_t i = 0; i < _activeCount; i++) {
        RGB c = _leds[i];
        if (bright < 255) c = scaleColorVideo(c, bright);
        _strip->SetPixelColor(i, RgbColor(c.r, c.g, c.b));
    }
    _appliedBrightness = bright;
    _strip->Show();
}

/* ----------------------------------------------------------------- overlay */
void LedController::testOff()  { _test = TestState(); _identifyUntil = 0; _testUntil = 0; }

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
    /* Show() is asynchronous and takes ~23 ms at 750 LEDs. Gating on millis()
       as well as CanShow() made the two race, dropping every other frame and
       halving the effective rate - which is what made motion look steppy.
       CanShow() alone paces us at exactly the wire's maximum. */
    if (_strip && !_strip->CanShow()) return;
    if (now == _lastFrame) return;              // never render twice in one ms
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
    GlobalSettings &g = Segments.globals();
    fillSolid(_leds, _activeCount, RGB_BLACK);
    if (!g.power) return;

    if (g.rainbowTint) _tintHue = (uint8_t)((now / 24) & 0xFF);

    const uint8_t n = Segments.count();

    /* Lay the enabled segments end to end so an animation can sweep the whole
       sign as one run (TRAVERSE phase B) regardless of how they are mapped. */
    uint16_t chainTotal = 0, chainMaxLen = 0;
    uint16_t chainAt[MAX_SEGMENTS] = {0};
    for (uint8_t i = 0; i < n; i++) {
        const Segment *sg = Segments.get(i);
        if (!sg || !sg->enabled) continue;
        uint16_t l = sg->length();
        if (sg->start >= _activeCount) continue;
        if (sg->start + l > _activeCount) l = _activeCount - sg->start;
        chainAt[i]  = chainTotal;
        chainTotal += l;
        if (l > chainMaxLen) chainMaxLen = l;
    }
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
            .absStart  = s->start,
            .chainOffset = chainAt[i],
            .chainTotal  = chainTotal,
            .chainMaxLen = chainMaxLen
        };

        AnimationEngine::render(anim, *s, ctx, _scratch, len);

        for (uint16_t k = 0; k < len; k++) {
            uint16_t dst = s->reversed ? (s->start + len - 1 - k) : (s->start + k);
            RGB px = _scratch[k];
            if (s->brightness < 255) px = scaleColorVideo(px, s->brightness);
            _leds[dst] = px;
        }
    }
}

void LedController::applyTest(uint32_t now) {
    /* Safety net: a test overlay paints over the animation every frame, so a
       forgotten one looks exactly like a dead controller. Let it go. */
    if (_test.mode != TEST_NONE && _test.mode != TEST_IDENTIFY &&
        _testUntil && now > _testUntil) {
        Serial.println(F("[led] test overlay timed out - back to normal"));
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
            /* soft markers every 10 LEDs so counting is easy */
            for (uint16_t i = 0; i < _activeCount; i += 10)
                if (i != _test.index) _leds[i] = RGB(12, 12, 12);
            break;

        case TEST_IDENTIFY: {
            if (now > _identifyUntil) { testOff(); return; }
            const Segment *s = Segments.get(_test.segment);
            if (!s) { testOff(); return; }
            bool on = ((now / 200) & 1);
            for (uint16_t i = s->start; i <= s->end && i < _activeCount; i++)
                _leds[i] = on ? RGB_WHITE : RGB_BLACK;
            break;
        }
    }
}

uint16_t LedController::estimatedMilliamps() const {
    uint32_t sum = 0;
    for (uint16_t i = 0; i < _activeCount; i++)
        sum += _leds[i].r + _leds[i].g + _leds[i].b;
    /* ~20 mA per channel at full, scaled by the brightness actually applied */
    uint32_t ma = (sum * 20UL * _appliedBrightness) / (255UL * 255UL);
    return (uint16_t)min<uint32_t>(ma + _activeCount, 65535);
}
