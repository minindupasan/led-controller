#include "SegmentManager.h"
#include <Preferences.h>

SegmentManager Segments;

static Preferences prefs;
static const char *NVS_NS  = "ledsign";
static const char *NVS_KEY = "cfg";

/* ------------------------------------------------------------ animation names */
static const char *const ANIM_NAMES[ANIM_COUNT] = {
    "OFF", "SOLID", "BREATHE", "PULSE", "TRAVERSE", "COMET", "WIPE",
    "THEATER", "SPARKLE", "RAINBOW", "GRADIENT", "STROBE", "FIRE", "TWINKLE",
    "BUILD", "STEPS"
};

const char *animationName(uint8_t id) {
    if (id == ANIM_INHERIT) return "INHERIT";
    return (id < ANIM_COUNT) ? ANIM_NAMES[id] : "?";
}

uint8_t animationIdFromName(const String &name) {
    String n = name;
    n.trim();
    n.toUpperCase();
    if (n == "INHERIT" || n == "-") return ANIM_INHERIT;
    for (uint8_t i = 0; i < ANIM_COUNT; i++)
        if (n == ANIM_NAMES[i]) return i;
    if (n.length() && isDigit(n[0])) {
        int v = n.toInt();
        if (v >= 0 && v < ANIM_COUNT) return (uint8_t)v;
    }
    return ANIM_SOLID;
}

/* ---------------------------------------------------------------- led pins */
static const uint8_t VALID_LED_PINS[] = {
    2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33
};

bool isValidLedPin(uint8_t pin) {
    for (uint8_t p : VALID_LED_PINS)
        if (p == pin) return true;
    return false;
}

const char *ledPinWarning(uint8_t pin) {
    switch (pin) {
        case 2:  return "also the on-board LED on most dev boards";
        case 12: return "strapping pin - a pulled-up GPIO12 can stop the board booting";
        case 15: return "strapping pin - outputs boot chatter, and must be low-ish at reset";
        default: return nullptr;
    }
}

/* --------------------------------------------------------------- divisions */
uint8_t divisionCount(const Segment &s) {
    if (s.divisions < 1) return 1;
    return (s.divisions > MAX_DIVISIONS) ? MAX_DIVISIONS : s.divisions;
}

/* Absolute LED bounds of sub-section k. Even split unless the user edited it. */
bool divisionBounds(const Segment &s, uint8_t k, uint16_t &start, uint16_t &end) {
    uint8_t n = divisionCount(s);
    uint16_t len = s.length();
    if (k >= n || !len) return false;

    if (s.customDiv) {
        start = (k == 0) ? s.start : (uint16_t)(s.divEnd[k - 1] + 1);
        end   = s.divEnd[k];
        return end >= start && start >= s.start && end <= s.end;
    }
    uint16_t base = len / n, rem = len % n, off = 0;
    for (uint8_t i = 0; i < k; i++) off += base + (i < rem ? 1 : 0);
    start = s.start + off;
    end   = start + base + (k < rem ? 1 : 0) - 1;
    return end >= start;
}

/* ------------------------------------------------------------------ helpers */
static void setName(Segment &s, const String &name) {
    ::memset(s.name, 0, SEG_NAME_LEN);
    ::strncpy(s.name, name.c_str(), SEG_NAME_LEN - 1);
}

static RGB jsonToColor(JsonVariantConst v, RGB fallback) {
    if (v.is<const char *>()) {                       // "#RRGGBB"
        String s = v.as<const char *>();
        s.replace("#", "");
        if (s.length() == 6) {
            uint32_t rgb = strtoul(s.c_str(), nullptr, 16);
            return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
    } else if (v.is<JsonArrayConst>()) {              // [r,g,b]
        JsonArrayConst a = v.as<JsonArrayConst>();
        if (a.size() == 3) return RGB(a[0].as<int>(), a[1].as<int>(), a[2].as<int>());
    } else if (v.is<uint32_t>()) {
        uint32_t rgb = v.as<uint32_t>();
        return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    return fallback;
}

static String colorToHex(const RGB &c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return String(buf);
}

/* -------------------------------------------------------------------- setup */
void SegmentManager::begin() {
    if (!load()) {
        Serial.println(F("[cfg] no stored config - loading defaults"));
        loadDefaults();
        save();
    }
}

void SegmentManager::loadDefaults() {
    ::memset(&_cfg, 0, sizeof(_cfg));
    _cfg.version = CONFIG_VERSION;

    _cfg.g.brightness   = 140;
    _cfg.g.speed        = 120;
    _cfg.g.intensity    = 128;
    _cfg.g.animation    = ANIM_BREATHE;
    _cfg.g.playMode     = PLAY_STAGGER;
    _cfg.g.stagger      = 120;
    _cfg.g.ledCount     = DEFAULT_LED_COUNT;
    _cfg.g.maxMilliamps = DEFAULT_MAX_MA;
    _cfg.g.dataPin      = DEFAULT_LED_PIN;
    _cfg.g.power        = true;
    _cfg.g.rainbowTint  = false;

    _cfg.wifi.enabled = true;       // AP by default, reachable with no setup
    _cfg.wifi.useSta  = false;
    _cfg.wifi.ssid[0] = 0;
    _cfg.wifi.pass[0] = 0;

    /* ---- INNOV + IOT + LOGO -------------------------------------------
       Placeholder ranges, 30 LEDs each. Re-map them from the web UI's
       "Mapping" tab (walk / range test) once the board is wired.        */
    struct Def { const char *n; uint32_t col; uint8_t div; };
    static const Def defs[] = {
        {"I",    0xFFFFFF, 1},
        {"N1",   0xFFFFFF, 1},
        {"N2",   0xFFFFFF, 1},
        {"O",    0xFFFFFF, 1},
        {"V",    0xFFFFFF, 1},
        {"PLUS", 0xFF9000, 1},
        {"I2",   0xFFFFFF, 1},
        {"O2",   0xFFFFFF, 1},
        {"T",    0xFFFFFF, 1},
        {"LOGO", 0x00A8FF, 10},   // 10 pieces + the all-glow step
    };
    _cfg.segmentCount = sizeof(defs) / sizeof(defs[0]);

    /* Spread the letters across the whole strip rather than assuming a
       length. Re-map them properly from the MAPPING tab once wired. */
    uint16_t each = _cfg.g.ledCount / _cfg.segmentCount;
    uint16_t rem  = _cfg.g.ledCount % _cfg.segmentCount;
    uint16_t cursor = 0;

    for (uint8_t i = 0; i < _cfg.segmentCount; i++) {
        Segment &s = _cfg.segments[i];
        setName(s, defs[i].n);
        uint16_t n   = each + (i < rem ? 1 : 0);
        s.start      = cursor;
        s.end        = cursor + n - 1;
        cursor      += n;
        s.enabled    = true;
        s.reversed   = false;
        s.color      = RGB((defs[i].col >> 16) & 0xFF, (defs[i].col >> 8) & 0xFF, defs[i].col & 0xFF);
        s.color2     = RGB_BLACK;
        s.animation  = ANIM_INHERIT;
        s.brightness = 255;
        s.divisions  = defs[i].div;
        s.customDiv  = false;
    }
    /* the logo builds piece by piece by default */
    _cfg.segments[_cfg.segmentCount - 1].animation = ANIM_BUILD;
    _dirty = true;
}

/* -------------------------------------------------------------- persistence */
bool SegmentManager::load() {
    if (!prefs.begin(NVS_NS, true)) return false;
    size_t sz = prefs.getBytesLength(NVS_KEY);
    bool ok = false;
    if (sz == sizeof(SignConfig)) {
        SignConfig tmp{};
        prefs.getBytes(NVS_KEY, &tmp, sizeof(tmp));
        if (tmp.version == CONFIG_VERSION && tmp.segmentCount <= MAX_SEGMENTS) {
            _cfg = tmp;
            ok = true;
        }
    }
    prefs.end();
    if (ok) {
        _dirty = false;
        Serial.printf("[cfg] loaded %u segments from NVS\n", _cfg.segmentCount);
    }
    return ok;
}

bool SegmentManager::save() {
    if (!prefs.begin(NVS_NS, false)) return false;
    _cfg.version = CONFIG_VERSION;
    size_t w = prefs.putBytes(NVS_KEY, &_cfg, sizeof(_cfg));
    prefs.end();
    _dirty = (w != sizeof(_cfg));
    Serial.printf("[cfg] save %s (%u bytes)\n", _dirty ? "FAILED" : "ok", (unsigned)w);
    return !_dirty;
}

void SegmentManager::factoryReset() {
    prefs.begin(NVS_NS, false);
    prefs.clear();
    prefs.end();
    loadDefaults();
    save();
}

/* ----------------------------------------------------------------- segments */
Segment *SegmentManager::get(int i) {
    if (i < 0 || i >= _cfg.segmentCount) return nullptr;
    return &_cfg.segments[i];
}

const Segment *SegmentManager::get(int i) const {
    if (i < 0 || i >= _cfg.segmentCount) return nullptr;
    return &_cfg.segments[i];
}

int SegmentManager::indexOfName(const String &name) const {
    for (uint8_t i = 0; i < _cfg.segmentCount; i++)
        if (name.equalsIgnoreCase(_cfg.segments[i].name)) return i;
    return -1;
}

int SegmentManager::add(const String &name, uint16_t start, uint16_t end) {
    if (_cfg.segmentCount >= MAX_SEGMENTS) return -1;
    Segment &s = _cfg.segments[_cfg.segmentCount];
    ::memset(&s, 0, sizeof(s));
    setName(s, name.length() ? name : String("SEG") + _cfg.segmentCount);
    s.start      = start;
    s.end        = end;
    s.enabled    = true;
    s.reversed   = false;
    s.color      = RGB_WHITE;
    s.color2     = RGB_BLACK;
    s.animation  = ANIM_INHERIT;
    s.brightness = 255;
    s.divisions  = 1;
    s.customDiv  = false;
    _dirty = true;
    return _cfg.segmentCount++;
}

bool SegmentManager::remove(int i) {
    if (i < 0 || i >= _cfg.segmentCount) return false;
    for (int k = i; k < _cfg.segmentCount - 1; k++)
        _cfg.segments[k] = _cfg.segments[k + 1];
    _cfg.segmentCount--;
    _dirty = true;
    return true;
}

bool SegmentManager::setRange(int i, uint16_t start, uint16_t end) {
    Segment *s = get(i);
    if (!s) return false;
    if (end < start) { uint16_t t = start; start = end; end = t; }
    s->start = start;
    s->end   = end;
    _dirty = true;
    return true;
}

bool SegmentManager::setDivisions(int i, uint8_t n) {
    Segment *s = get(i);
    if (!s) return false;
    s->divisions = constrain((int)n, 1, MAX_DIVISIONS);
    s->customDiv = false;                 // back to an even split
    _dirty = true;
    return true;
}

bool SegmentManager::autoSplit(int i) {
    Segment *s = get(i);
    if (!s) return false;
    s->customDiv = false;
    _dirty = true;
    return true;
}

/* Divisions are contiguous, so editing one piece moves the seam it shares
   with its neighbour. Bounds are clamped inside the segment and kept
   monotonic so no piece can swallow another. */
bool SegmentManager::setDivisionRange(int i, uint8_t k, uint16_t a, uint16_t b) {
    Segment *s = get(i);
    if (!s) return false;
    uint8_t n = divisionCount(*s);
    if (k >= n) return false;
    if (b < a) { uint16_t t = a; a = b; b = t; }

    if (!s->customDiv) {                  // materialise the even split first
        for (uint8_t j = 0; j < n; j++) {
            uint16_t ea, eb;
            if (divisionBounds(*s, j, ea, eb)) s->divEnd[j] = eb;
        }
        s->customDiv = true;
    }

    a = constrain((int)a, (int)s->start, (int)s->end);
    b = constrain((int)b, (int)a, (int)s->end);

    if (k > 0) s->divEnd[k - 1] = a - 1;  // move the shared seam
    s->divEnd[k] = b;
    s->divEnd[n - 1] = s->end;            // the last piece always ends the segment

    for (uint8_t j = 1; j < n; j++)       // keep the seams ordered
        if (s->divEnd[j] < s->divEnd[j - 1]) s->divEnd[j] = s->divEnd[j - 1];

    _dirty = true;
    return true;
}

/* Re-divide the strip evenly between the existing segments, keeping their
   names, colours and order. The quick way to fix a map that covers only part
   of the strip. */
bool SegmentManager::spreadEvenly() {
    if (!_cfg.segmentCount) return false;
    uint16_t each = _cfg.g.ledCount / _cfg.segmentCount;
    uint16_t rem  = _cfg.g.ledCount % _cfg.segmentCount;
    uint16_t cursor = 0;
    for (uint8_t i = 0; i < _cfg.segmentCount; i++) {
        uint16_t n = each + (i < rem ? 1 : 0);
        _cfg.segments[i].start = cursor;
        _cfg.segments[i].end   = cursor + n - 1;
        _cfg.segments[i].customDiv = false;      // sub-sections re-split too
        cursor += n;
    }
    _dirty = true;
    return true;
}

void SegmentManager::sortByStart() {
    for (int i = 1; i < _cfg.segmentCount; i++) {
        Segment key = _cfg.segments[i];
        int j = i - 1;
        while (j >= 0 && _cfg.segments[j].start > key.start) {
            _cfg.segments[j + 1] = _cfg.segments[j];
            j--;
        }
        _cfg.segments[j + 1] = key;
    }
    _dirty = true;
}

/* --------------------------------------------------------------- validation */
uint8_t SegmentManager::validate(Issue *out, uint8_t maxIssues) const {
    uint8_t n = 0;
    auto push = [&](int a, int b, const char *what) {
        if (n < maxIssues) { out[n] = {a, b, what}; }
        n++;
    };
    for (uint8_t i = 0; i < _cfg.segmentCount; i++) {
        const Segment &a = _cfg.segments[i];
        if (a.end < a.start)          push(i, -1, "end before start");
        if (a.end >= _cfg.g.ledCount) push(i, -1, "range past ledCount");
        for (uint8_t j = i + 1; j < _cfg.segmentCount; j++) {
            const Segment &b = _cfg.segments[j];
            if (a.start <= b.end && b.start <= a.end) push(i, j, "overlap");
        }
    }
    return n;
}

/* -------------------------------------------------------------------- JSON */
void SegmentManager::segmentToJson(int i, JsonObject o) const {
    const Segment *s = get(i);
    if (!s) return;
    o["i"]          = i;
    o["name"]       = s->name;
    o["start"]      = s->start;
    o["end"]        = s->end;
    o["len"]        = s->length();
    o["enabled"]    = s->enabled;
    o["reversed"]   = s->reversed;
    o["color"]      = colorToHex(s->color);
    o["color2"]     = colorToHex(s->color2);
    o["anim"]       = s->animation;
    o["animName"]   = animationName(s->animation);
    o["brightness"] = s->brightness;
    o["divisions"]  = divisionCount(*s);
    o["customDiv"]  = s->customDiv;
    divisionsToJson(i, o["div"].to<JsonArray>());
}

void SegmentManager::divisionsToJson(int i, JsonArray arr) const {
    const Segment *s = get(i);
    if (!s) return;
    uint8_t n = divisionCount(*s);
    for (uint8_t k = 0; k < n; k++) {
        uint16_t a, b;
        if (!divisionBounds(*s, k, a, b)) continue;
        JsonObject o = arr.add<JsonObject>();
        o["k"]     = k;
        o["start"] = a;
        o["end"]   = b;
        o["len"]   = b - a + 1;
    }
}

void SegmentManager::toJson(JsonObject root) const {
    JsonObject g = root["global"].to<JsonObject>();
    g["power"]        = _cfg.g.power;
    g["brightness"]   = _cfg.g.brightness;
    g["speed"]        = _cfg.g.speed;
    g["intensity"]    = _cfg.g.intensity;
    g["anim"]         = _cfg.g.animation;
    g["animName"]     = animationName(_cfg.g.animation);
    g["playMode"]     = _cfg.g.playMode;
    g["stagger"]      = _cfg.g.stagger;
    g["ledCount"]     = _cfg.g.ledCount;
    g["maxMilliamps"] = _cfg.g.maxMilliamps;
    g["dataPin"]      = _cfg.g.dataPin;
    g["rainbowTint"]  = _cfg.g.rainbowTint;

    JsonArray segs = root["segments"].to<JsonArray>();
    for (uint8_t i = 0; i < _cfg.segmentCount; i++)
        segmentToJson(i, segs.add<JsonObject>());

    JsonArray anims = root["animations"].to<JsonArray>();
    for (uint8_t i = 0; i < ANIM_COUNT; i++) {
        JsonObject a = anims.add<JsonObject>();
        a["id"]   = i;
        a["name"] = animationName(i);
    }

    JsonArray issues = root["issues"].to<JsonArray>();
    Issue found[16];
    uint8_t n = validate(found, 16);
    for (uint8_t i = 0; i < n && i < 16; i++) {
        JsonObject o = issues.add<JsonObject>();
        o["a"]    = found[i].segA;
        o["b"]    = found[i].segB;
        o["what"] = found[i].what;
    }
    root["dirty"] = _dirty;
}

bool SegmentManager::applySegmentJson(int i, JsonObjectConst o, String &err) {
    Segment *s = get(i);
    if (!s) { err = "no such segment"; return false; }

    if (o["name"].is<const char *>())  setName(*s, o["name"].as<const char *>());
    if (o["start"].is<uint16_t>())     s->start      = o["start"].as<uint16_t>();
    if (o["end"].is<uint16_t>())       s->end        = o["end"].as<uint16_t>();
    if (o["enabled"].is<bool>())       s->enabled    = o["enabled"].as<bool>();
    if (o["reversed"].is<bool>())      s->reversed   = o["reversed"].as<bool>();
    if (o["brightness"].is<uint8_t>()) s->brightness = o["brightness"].as<uint8_t>();
    if (!o["color"].isNull())          s->color      = jsonToColor(o["color"], s->color);
    if (!o["color2"].isNull())         s->color2     = jsonToColor(o["color2"], s->color2);
    if (!o["anim"].isNull()) {
        JsonVariantConst v = o["anim"];
        s->animation = v.is<const char *>() ? animationIdFromName(v.as<const char *>())
                                            : (uint8_t)v.as<int>();
    }
    if (o["divisions"].is<uint8_t>()) {
        s->divisions = constrain((int)o["divisions"].as<uint8_t>(), 1, MAX_DIVISIONS);
        s->customDiv = false;
    }
    if (s->end < s->start) { uint16_t t = s->start; s->start = s->end; s->end = t; }
    if (s->end >= MAX_LEDS) s->end = MAX_LEDS - 1;
    _dirty = true;
    return true;
}

bool SegmentManager::applyGlobalJson(JsonObjectConst o, String &err) {
    GlobalSettings &g = _cfg.g;
    if (o["power"].is<bool>())          g.power       = o["power"].as<bool>();
    if (o["brightness"].is<uint8_t>())  g.brightness  = o["brightness"].as<uint8_t>();
    if (o["speed"].is<uint8_t>())       g.speed       = max<uint8_t>(1, o["speed"].as<uint8_t>());
    if (o["intensity"].is<uint8_t>())   g.intensity   = o["intensity"].as<uint8_t>();
    if (o["playMode"].is<uint8_t>())    g.playMode    = min<uint8_t>(PLAY_SEQUENCE, o["playMode"].as<uint8_t>());
    if (o["stagger"].is<uint16_t>())    g.stagger     = o["stagger"].as<uint16_t>();
    if (o["rainbowTint"].is<bool>())    g.rainbowTint = o["rainbowTint"].as<bool>();
    if (o["maxMilliamps"].is<uint16_t>()) g.maxMilliamps = o["maxMilliamps"].as<uint16_t>();
    if (o["ledCount"].is<uint16_t>()) {
        uint16_t n = o["ledCount"].as<uint16_t>();
        g.ledCount = constrain(n, (uint16_t)1, (uint16_t)MAX_LEDS);
    }
    if (!o["anim"].isNull()) {
        JsonVariantConst v = o["anim"];
        uint8_t a = v.is<const char *>() ? animationIdFromName(v.as<const char *>())
                                         : (uint8_t)v.as<int>();
        if (a >= ANIM_COUNT) a = ANIM_SOLID;
        g.animation = a;
    }
    _dirty = true;
    return true;
}

bool SegmentManager::importJson(JsonObjectConst root, String &err) {
    if (root["global"].is<JsonObjectConst>())
        applyGlobalJson(root["global"].as<JsonObjectConst>(), err);

    if (root["segments"].is<JsonArrayConst>()) {
        JsonArrayConst arr = root["segments"].as<JsonArrayConst>();
        if (arr.size() > MAX_SEGMENTS) { err = "too many segments"; return false; }
        _cfg.segmentCount = 0;
        for (JsonObjectConst o : arr) {
            int i = add(o["name"].is<const char *>() ? o["name"].as<const char *>() : "SEG",
                        o["start"] | 0, o["end"] | 0);
            if (i < 0) break;
            applySegmentJson(i, o, err);
        }
    }
    _dirty = true;
    return true;
}
