#include "SegmentManager.h"
#include <Preferences.h>

SegmentManager Segments;

static Preferences prefs;
static const char *NVS_NS  = "ledsign";
static const char *NVS_KEY = "cfg";

/* ------------------------------------------------------------ animation names */
static const char *const ANIM_NAMES[ANIM_COUNT] = {
    "OFF", "SOLID", "BREATHE", "PULSE", "TRAVERSE", "COMET", "WIPE",
    "THEATER", "SPARKLE", "RAINBOW", "GRADIENT", "STROBE", "FIRE", "TWINKLE"
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

/* ------------------------------------------------------------------ helpers */
static void setName(Segment &s, const String &name) {
    ::memset(s.name, 0, SEG_NAME_LEN);
    ::strncpy(s.name, name.c_str(), SEG_NAME_LEN - 1);
}

static CRGB jsonToColor(JsonVariantConst v, CRGB fallback) {
    if (v.is<const char *>()) {                       // "#RRGGBB"
        String s = v.as<const char *>();
        s.replace("#", "");
        if (s.length() == 6) {
            uint32_t rgb = strtoul(s.c_str(), nullptr, 16);
            return CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
    } else if (v.is<JsonArrayConst>()) {              // [r,g,b]
        JsonArrayConst a = v.as<JsonArrayConst>();
        if (a.size() == 3) return CRGB(a[0].as<int>(), a[1].as<int>(), a[2].as<int>());
    } else if (v.is<uint32_t>()) {
        uint32_t rgb = v.as<uint32_t>();
        return CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    return fallback;
}

static String colorToHex(const CRGB &c) {
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
    _cfg.g.power        = true;
    _cfg.g.rainbowTint  = false;

    _cfg.wifi.useSta = false;
    _cfg.wifi.ssid[0] = 0;
    _cfg.wifi.pass[0] = 0;

    /* ---- INNOV + IOT + LOGO -------------------------------------------
       Placeholder ranges, 30 LEDs each. Re-map them from the web UI's
       "Mapping" tab (walk / range test) once the board is wired.        */
    struct Def { const char *n; uint16_t a, b; uint32_t col; };
    static const Def defs[] = {
        {"I",    0,   29,  0xFFFFFF},
        {"N1",   30,  59,  0xFFFFFF},
        {"N2",   60,  89,  0xFFFFFF},
        {"O",    90,  119, 0xFFFFFF},
        {"V",    120, 149, 0xFFFFFF},
        {"PLUS", 150, 179, 0xFF9000},
        {"I2",   180, 209, 0xFFFFFF},
        {"O2",   210, 239, 0xFFFFFF},
        {"T",    240, 269, 0xFFFFFF},
        {"LOGO", 270, 299, 0x00A8FF},
    };
    _cfg.segmentCount = sizeof(defs) / sizeof(defs[0]);
    for (uint8_t i = 0; i < _cfg.segmentCount; i++) {
        Segment &s = _cfg.segments[i];
        setName(s, defs[i].n);
        s.start      = defs[i].a;
        s.end        = defs[i].b;
        s.enabled    = true;
        s.reversed   = false;
        s.color      = CRGB((defs[i].col >> 16) & 0xFF, (defs[i].col >> 8) & 0xFF, defs[i].col & 0xFF);
        s.color2     = CRGB::Black;
        s.animation  = ANIM_INHERIT;
        s.brightness = 255;
    }
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
    s.color      = CRGB::White;
    s.color2     = CRGB::Black;
    s.animation  = ANIM_INHERIT;
    s.brightness = 255;
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
