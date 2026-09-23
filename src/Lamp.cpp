#include "Lamp.h"
#include "Logger.h"
#include "Net.h"

LogoController Logo;

void LogoController::begin() {
    ::memset(_leds, 0, sizeof(_leds));
    restartStrip();
}

void LogoController::restartStrip() {
    Settings &st = TheSign.settings();
    _activeCount = constrain(st.logoLedCount, (uint16_t)1, (uint16_t)MAX_LOGO_LEDS);
    _activePin   = isValidLedPin(st.logoPin) ? st.logoPin : DEFAULT_LOGO_PIN;

    if (_strip) { delete _strip; _strip = nullptr; }
    _strip = new LogoStrip(_activeCount, _activePin);
    _strip->Begin();
    _strip->ClearTo(RgbColor(0, 0, 0));
    _strip->Show();

    Serial.printf("[lamp] %u LEDs on GPIO%u (RMT1), %u segments\n",
                  _activeCount, _activePin, TheSign.logoSegCount());
}

/* ----------------------------------------------------------------- control */
bool LogoController::isLit(int i) const {
    return (i >= 0 && i < MAX_LOGO_SEGS) && (_lit & (1UL << i));
}

uint8_t LogoController::level() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < TheSign.logoSegCount(); i++)
        if (_lit & (1UL << i)) n++;
    return n;
}

bool LogoController::full() const {
    uint8_t n = TheSign.logoSegCount();
    if (!n) return false;
    for (uint8_t i = 0; i < n; i++) {
        LogoSegment *sg = TheSign.logoSeg(i);
        if (sg && sg->enabled && !(_lit & (1UL << i))) return false;
    }
    return true;
}

void LogoController::lightSegment(int i) {
    if (i < 0 || i >= TheSign.logoSegCount()) return;
    if (_lit & (1UL << i)) return;                  // already lit, keep its fade
    _lit |= (1UL << i);
    _litAt[i] = millis();

    /* The lamp filling completely is the cue for the sign - the whole point
       of the sequence. The sign is ESP #1 on this AP, so the trigger is one
       short HTTP call; if it does not answer the lamp carries on regardless
       and the sign can be started by hand. */
    if (full() && TheSign.settings().logoAutoTrigger) {
        Log.log("[lamp] full - triggering the sign");
        Net.sendToSign("show start");
    }
}

void LogoController::clearSegment(int i) {
    if (i < 0 || i >= MAX_LOGO_SEGS) return;
    _lit &= ~(1UL << i);
}

void LogoController::setLevel(uint8_t n) {
    n = min<uint8_t>(n, TheSign.logoSegCount());
    for (uint8_t i = 0; i < TheSign.logoSegCount(); i++) {
        if (i < n) lightSegment(i);
        else       clearSegment(i);
    }
}

void LogoController::step(int8_t delta) {
    int v = (int)level() + delta;
    setLevel((uint8_t)constrain(v, 0, (int)TheSign.logoSegCount()));
}

void LogoController::allOn()  { setLevel(TheSign.logoSegCount()); }
void LogoController::allOff() { _lit = 0; _glow = false; }

/* -------------------------------------------------------------------- RFID */
void LogoController::learn(int segIndex) {
    _learn = (segIndex >= 0 && segIndex < TheSign.logoSegCount()) ? (int8_t)segIndex : -1;
    if (_learn >= 0) Log.log("[lamp] learning: next card -> segment %d", _learn);
}

/*
 * A tap arrives here from the laptop bridge. In learn mode the card is bound
 * to the waiting segment instead of lighting anything, which is how segments
 * get their IDs without typing UIDs by hand.
 */
int LogoController::tapCard(const String &uid) {
    _lastCard = uid;
    _lastCard.trim();
    _lastCard.toUpperCase();

    if (_learn >= 0) {
        int target = _learn;
        _learn = -1;
        TheSign.assignCard(target, _lastCard);
        TheSign.save();
        Log.log("[lamp] card %s -> segment %d", _lastCard.c_str(), target);
        lightSegment(target);                       // show which one it was
        return -2;
    }

    int i = TheSign.logoSegByCard(_lastCard);
    if (i < 0) {
        Log.log("[lamp] unknown card %s", _lastCard.c_str());
        return -1;
    }
    lightSegment(i);
    Log.log("[lamp] card %s lit segment %d (%u/%u)", _lastCard.c_str(), i,
            level(), TheSign.logoSegCount());
    return i;
}

/* ----------------------------------------------------------------- mapping */
void LogoController::testOff() { _test = TestState(); _identifyUntil = 0; _testUntil = 0; }

void LogoController::testIndex(uint16_t idx, RGB c) {
    _testUntil = millis() + TEST_TIMEOUT_MS;
    _test.mode  = TEST_INDEX;
    _test.index = min<uint16_t>(idx, _activeCount - 1);
    _test.color = c;
}

void LogoController::testRange(uint16_t a, uint16_t b, RGB c) {
    _testUntil = millis() + TEST_TIMEOUT_MS;
    if (b < a) { uint16_t t = a; a = b; b = t; }
    _test.mode     = TEST_RANGE;
    _test.index    = min<uint16_t>(a, _activeCount - 1);
    _test.rangeEnd = min<uint16_t>(b, _activeCount - 1);
    _test.color    = c;
}

void LogoController::testWalk(uint16_t delayMs) {
    _testUntil = millis() + TEST_TIMEOUT_MS;
    _test.mode      = TEST_WALK;
    _test.index     = 0;
    _test.walkDelay = max<uint16_t>(30, delayMs);
    _test.lastStep  = millis();
    _test.color     = RGB_WHITE;
}

void LogoController::identify(int segIndex) {
    if (!TheSign.logoSeg(segIndex)) return;
    _test.mode   = TEST_IDENTIFY;
    _test.letter = segIndex;
    _identifyUntil = millis() + 3000;
}

/* ------------------------------------------------------------------ frame */
void LogoController::loop() {
    if (!_strip || !_strip->CanShow()) return;
    uint32_t now = millis();
    if (now == _lastFrame) return;
    _lastFrame = now;

    render(now);
    applyTest(now);

    Settings &st = TheSign.settings();
    bool lit = st.power || _test.mode != TEST_NONE;
    uint8_t bright = lit ? st.brightness : 0;

    for (uint16_t i = 0; i < _activeCount; i++) {
        RGB c = _leds[i];
        if (bright < 255) c = scaleColorVideo(c, bright);
        _strip->SetPixelColor(i, RgbColor(c.r, c.g, c.b));
    }
    _strip->Show();
}

void LogoController::render(uint32_t now) {
    Settings &st = TheSign.settings();
    fillSolid(_leds, _activeCount, RGB_BLACK);
    if (!st.power) return;

    const RGB base = st.logoColor;
    const uint16_t fadeMs = max<uint16_t>(40, (uint16_t)st.logoFadeMs10 * 10);

    /* Once full, the whole lamp breathes gently so it reads as alight rather
       than simply switched on. */
    uint8_t glowScale = 255;
    if (_glow || full()) {
        uint8_t ph = (uint8_t)((now / 12) & 0xFF);
        glowScale = qadd8(scale8(cubicwave8(ph), 90), 165);
    }

    for (uint8_t k = 0; k < TheSign.logoSegCount(); k++) {
        if (!(_lit & (1UL << k))) continue;
        LogoSegment *sg = TheSign.logoSeg(k);
        if (!sg || !sg->enabled) continue;

        /* each segment eases in from the moment its card was tapped */
        uint8_t lvl = 255;
        uint32_t since = now - _litAt[k];
        if (since < fadeMs)
            lvl = ease8InOutCubic((uint8_t)((since * 255UL) / fadeMs));

        RGB col = scaleColorVideo(base, scale8(lvl, glowScale));
        uint16_t len = sg->length();
        if (sg->start >= _activeCount) continue;
        if (sg->start + len > _activeCount) len = _activeCount - sg->start;

        for (uint16_t i = 0; i < len; i++) {
            uint16_t dst = sg->reversed ? (sg->start + len - 1 - i) : (sg->start + i);
            _leds[dst] = col;
        }
    }
}

void LogoController::applyTest(uint32_t now) {
    if (_test.mode != TEST_NONE && _test.mode != TEST_IDENTIFY &&
        _testUntil && now > _testUntil) { testOff(); return; }

    switch (_test.mode) {
        case TEST_NONE: return;

        case TEST_INDEX:
            fillSolid(_leds, _activeCount, RGB_BLACK);
            if (_test.index < _activeCount) _leds[_test.index] = _test.color;
            break;

        case TEST_RANGE:
            fillSolid(_leds, _activeCount, RGB_BLACK);
            for (uint16_t i = _test.index; i <= _test.rangeEnd && i < _activeCount; i++)
                _leds[i] = _test.color;
            break;

        case TEST_WALK:
            if (now - _test.lastStep >= _test.walkDelay) {
                _test.lastStep = now;
                _test.index = (_test.index + 1) % _activeCount;
            }
            fillSolid(_leds, _activeCount, RGB_BLACK);
            _leds[_test.index] = _test.color;
            for (uint16_t i = 0; i < _activeCount; i += 10)
                if (i != _test.index) _leds[i] = RGB(12, 12, 12);
            break;

        case TEST_IDENTIFY: {
            if (now > _identifyUntil) { testOff(); return; }
            LogoSegment *sg = TheSign.logoSeg(_test.letter);
            if (!sg) { testOff(); return; }
            bool on = ((now / 200) & 1);
            for (uint16_t i = sg->start; i <= sg->end && i < _activeCount; i++)
                _leds[i] = on ? RGB_WHITE : RGB_BLACK;
            break;
        }

        default: break;
    }
}
