#include "Sign.h"
#include <Preferences.h>

Sign TheSign;

static Preferences prefs;
static const char *NVS_NS  = "innoviot";
static const char *NVS_KEY = "cfg";

/* ------------------------------------------------------------ name tables */
static const char *const ANIM_NAMES[ANIM_COUNT] = {
    "OFF", "SOLID", "BREATHE", "TRAVERSE", "AURORA", "COMET", "BOUNCE"
};

const char *animationName(uint8_t id) {
    return (id < ANIM_COUNT) ? ANIM_NAMES[id] : "?";
}

uint8_t animationFromName(const String &name) {
    String n = name;
    n.trim();
    n.toUpperCase();
    for (uint8_t i = 0; i < ANIM_COUNT; i++)
        if (n == ANIM_NAMES[i]) return i;
    if (n.length() && isDigit(n[0])) {
        int v = n.toInt();
        if (v >= 0 && v < ANIM_COUNT) return (uint8_t)v;
    }
    return ANIM_BREATHE;
}

RGB paletteColor(const String &name, bool *found) {
    String n = name;
    n.trim();
    n.toUpperCase();
    if (found) *found = true;
    if (n == "PURPLE" || n == "VIOLET") return RGB((uint32_t)COL_PURPLE);
    if (n == "CYAN"   || n == "BLUE")   return RGB((uint32_t)COL_CYAN);
    if (n == "AMBER"  || n == "GOLD")   return RGB((uint32_t)COL_AMBER);
    if (n == "WHITE")                   return RGB_WHITE;
    if (found) *found = false;
    return RGB_WHITE;
}

/* ------------------------------------------------------------------- pins */
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
        case 15: return "strapping pin - outputs boot chatter";
        default: return nullptr;
    }
}

/* ---------------------------------------------------------------- helpers */
static void setName(char *dst, const char *src) {
    ::memset(dst, 0, NAME_LEN);
    ::strncpy(dst, src, NAME_LEN - 1);
}

static String hex(const RGB &c) {
    char b[8];
    snprintf(b, sizeof(b), "#%02X%02X%02X", c.r, c.g, c.b);
    return String(b);
}

/* -------------------------------------------------------------------- init */
void Sign::begin() {
    if (!load()) {
        Serial.println(F("[cfg] no stored config - loading measured defaults"));
        loadDefaults();
        save();
    }
}

void Sign::loadDefaults() {
    ::memset(&_cfg, 0, sizeof(_cfg));
    _cfg.version = CONFIG_VERSION;

    _cfg.s.brightness   = 200;
    _cfg.s.speed        = 120;
    _cfg.s.animation    = ANIM_BREATHE;
    _cfg.s.ledCount     = DEFAULT_LED_COUNT;
    _cfg.s.maxMilliamps = DEFAULT_MAX_MA;
    _cfg.s.dataPin      = DEFAULT_LED_PIN;
    _cfg.s.power        = true;
    _cfg.s.autoShow     = true;

    _cfg.s.logoPin      = DEFAULT_LOGO_PIN;
    _cfg.s.logoLedCount = DEFAULT_LOGO_COUNT;
    _cfg.s.logoColor    = RGB((uint32_t)COL_AMBER);   // a lamp flame
    _cfg.s.logoFadeMs10   = 25;                        // 250 ms per segment
    _cfg.s.logoAutoTrigger = true;                     // full lamp starts the sign

    /* Measured on the real sign - see the mapping tab to re-measure. */
    struct Def { const char *n; uint16_t a, b; };
    static const Def defs[] = {
        {"I",  0,   41},    {"N1", 42,  145},  {"N2", 146, 249},  {"O",  250, 341},
        {"V",  342, 412},   {"I2", 413, 445},  {"O2", 446, 538},  {"T",  539, 590},
    };
    _cfg.letterCount = sizeof(defs) / sizeof(defs[0]);
    for (uint8_t i = 0; i < _cfg.letterCount; i++) {
        Letter &l = _cfg.letters[i];
        setName(l.name, defs[i].n);
        l.start    = defs[i].a;
        l.end      = defs[i].b;
        l.enabled  = true;
        l.reversed = false;
    }

    /* INNOV = letters 0-4, IOT = letters 5-7 */
    _cfg.wordCount = 2;
    setName(_cfg.words[0].name, "INNOV");
    _cfg.words[0].first     = 0;
    _cfg.words[0].count     = 5;
    _cfg.words[0].color     = RGB((uint32_t)COL_CYAN);

    setName(_cfg.words[1].name, "IOT");
    _cfg.words[1].first     = 5;
    _cfg.words[1].count     = 3;
    _cfg.words[1].color     = RGB((uint32_t)COL_AMBER);

    /* The guests, in priority order - 1 lights first. Their LED ranges are
       an even split until the lamp is measured with the walk tool. */
    struct Guest { uint8_t pri; const char *card; const char *name; };
    static const Guest guests[] = {
        { 1, "59A52A8E", "Anuradha Jaykody"},
        { 2, "54422B8E", "Nuwan Kodagoda"},
        { 3, "F492298E", "Koliya Pulasinghe"},
        { 4, "0294298E", "Samantha Rajapaksha"},
        { 5, "85CE298E", "Amila Senarathne"},
        { 6, "5B674000", "Uditha Dharmakeerthi"},
        { 7, "213B3F00", "Namal Cooray"},
        { 8, "4716298E", "Gayan Dissanayake"},
        { 9, "9E12298E", "Chanaka Prasad"},
        {10, "F6E62A8E", "Thejan Mendis"},
        {11, "E7242A8E", "Kasuni Navodana"},
        {12, "5461298E", "Dinith Primal"},
        {13, "191B4900", "Student Committee President"},
        {14, "B4914000", "Extra card 1"},
        {15, "FAD0D505", "Extra card 2"},
    };
    _cfg.logoSegCount = sizeof(guests) / sizeof(guests[0]);
    for (uint8_t i = 0; i < _cfg.logoSegCount; i++) {
        LogoSegment &l = _cfg.logoSegs[i];
        ::memset(&l, 0, sizeof(l));
        ::strncpy(l.name, guests[i].name, GUEST_NAME_LEN - 1);
        ::strncpy(l.card, guests[i].card, CARD_ID_LEN - 1);
        l.priority = guests[i].pri;
        l.enabled  = true;
        l.reversed = false;
    }
    spreadLogoEvenly();

    _dirty = true;
}

/* ------------------------------------------------------------------- logo */
LogoSegment *Sign::logoSeg(int i) {
    return (i < 0 || i >= _cfg.logoSegCount) ? nullptr : &_cfg.logoSegs[i];
}

/* Card lookup is the hot path: every tap arrives as an HTTP request and has
   to resolve to a segment. Comparison is case-insensitive so it does not
   matter how the reader formats the UID. */
int Sign::logoSegByCard(const String &uid) const {
    String want = uid;
    want.trim();
    want.toUpperCase();
    if (!want.length()) return -1;
    for (uint8_t i = 0; i < _cfg.logoSegCount; i++)
        if (want.equalsIgnoreCase(_cfg.logoSegs[i].card)) return i;
    return -1;
}

int Sign::addLogoSeg(const String &name, uint16_t start, uint16_t end) {
    if (_cfg.logoSegCount >= MAX_LOGO_SEGS) return -1;
    LogoSegment &l = _cfg.logoSegs[_cfg.logoSegCount];
    ::memset(&l, 0, sizeof(l));
    ::strncpy(l.name, name.length() ? name.c_str()
                                    : (String("Guest ") + (_cfg.logoSegCount + 1)).c_str(),
              GUEST_NAME_LEN - 1);
    l.priority = _cfg.logoSegCount + 1;
    if (end < start) { uint16_t t = start; start = end; end = t; }
    l.start    = start;
    l.end      = end;
    l.enabled  = true;
    l.reversed = false;
    l.card[0]  = 0;
    _dirty = true;
    return _cfg.logoSegCount++;
}

bool Sign::removeLogoSeg(int i) {
    if (i < 0 || i >= _cfg.logoSegCount) return false;
    for (int k = i; k < _cfg.logoSegCount - 1; k++)
        _cfg.logoSegs[k] = _cfg.logoSegs[k + 1];
    _cfg.logoSegCount--;
    _dirty = true;
    return true;
}

bool Sign::assignCard(int i, const String &uid) {
    LogoSegment *l = logoSeg(i);
    if (!l) return false;
    String v = uid;
    v.trim();
    v.toUpperCase();
    ::memset(l->card, 0, CARD_ID_LEN);
    if (v.length()) ::strncpy(l->card, v.c_str(), CARD_ID_LEN - 1);
    _dirty = true;
    return true;
}

int Sign::logoSegByName(const String &name) const {
    for (uint8_t i = 0; i < _cfg.logoSegCount; i++)
        if (name.equalsIgnoreCase(_cfg.logoSegs[i].name)) return i;
    return -1;
}

bool Sign::setLogoRange(int i, uint16_t start, uint16_t end) {
    LogoSegment *l = logoSeg(i);
    if (!l) return false;
    if (end < start) { uint16_t t = start; start = end; end = t; }
    l->start = start;
    l->end   = min<uint16_t>(end, MAX_LOGO_LEDS - 1);
    _dirty = true;
    return true;
}

bool Sign::setLogoSegCount(uint8_t n) {
    _cfg.logoSegCount = constrain((int)n, 1, MAX_LOGO_SEGS);
    spreadLogoEvenly();
    return true;
}

/* Divide the logo strip evenly between its segments, naming them L1..Ln.
   The starting point before the real boundaries are measured. */
bool Sign::setLogoName(int i, const String &name) {
    LogoSegment *l = logoSeg(i);
    if (!l || !name.length()) return false;
    ::memset(l->name, 0, GUEST_NAME_LEN);
    ::strncpy(l->name, name.c_str(), GUEST_NAME_LEN - 1);
    _dirty = true;
    return true;
}

bool Sign::setLogoPriority(int i, uint8_t priority) {
    LogoSegment *l = logoSeg(i);
    if (!l) return false;
    l->priority = priority;
    _dirty = true;
    return true;
}

/* Keep the table in the order the lamp fills. Insertion sort - the list is
   short and this runs only when a priority is edited. */
void Sign::sortLogoByPriority() {
    for (int i = 1; i < _cfg.logoSegCount; i++) {
        LogoSegment key = _cfg.logoSegs[i];
        int j = i - 1;
        while (j >= 0 && _cfg.logoSegs[j].priority > key.priority) {
            _cfg.logoSegs[j + 1] = _cfg.logoSegs[j];
            j--;
        }
        _cfg.logoSegs[j + 1] = key;
    }
    _dirty = true;
}

void Sign::spreadLogoEvenly() {
    uint8_t n = _cfg.logoSegCount ? _cfg.logoSegCount : 1;
    uint16_t each = _cfg.s.logoLedCount / n;
    uint16_t rem  = _cfg.s.logoLedCount % n;
    uint16_t cursor = 0;
    for (uint8_t i = 0; i < n; i++) {
        LogoSegment &l = _cfg.logoSegs[i];
        /* names, cards and priorities survive - this only moves the ranges */
        uint16_t len = each + (i < rem ? 1 : 0);
        l.start    = cursor;
        l.end      = cursor + len - 1;
        l.enabled  = true;
        l.reversed = false;
        /* keep any card already assigned - re-splitting is a geometry change */
        cursor += len;
    }
    _dirty = true;
}

/* -------------------------------------------------------------- persistence */
bool Sign::load() {
    if (!prefs.begin(NVS_NS, true)) return false;
    size_t sz = prefs.getBytesLength(NVS_KEY);
    bool ok = false;
    if (sz == sizeof(SignConfig)) {
        SignConfig tmp{};
        prefs.getBytes(NVS_KEY, &tmp, sizeof(tmp));
        if (tmp.version == CONFIG_VERSION &&
            tmp.letterCount <= MAX_LETTERS && tmp.wordCount <= MAX_WORDS &&
            tmp.logoSegCount <= MAX_LOGO_SEGS) {
            _cfg = tmp;
            ok = true;
        }
    }
    prefs.end();
    if (ok) {
        _dirty = false;
        Serial.printf("[cfg] loaded %u letters / %u words from NVS\n",
                      _cfg.letterCount, _cfg.wordCount);
    }
    return ok;
}

bool Sign::save() {
    if (!prefs.begin(NVS_NS, false)) return false;
    _cfg.version = CONFIG_VERSION;
    size_t w = prefs.putBytes(NVS_KEY, &_cfg, sizeof(_cfg));
    prefs.end();
    _dirty = (w != sizeof(_cfg));
    return !_dirty;
}

void Sign::factoryReset() {
    prefs.begin(NVS_NS, false);
    prefs.clear();
    prefs.end();
    loadDefaults();
    save();
}

/* ----------------------------------------------------------------- access */
Letter *Sign::letter(int i) {
    return (i < 0 || i >= _cfg.letterCount) ? nullptr : &_cfg.letters[i];
}

Word *Sign::word(int i) {
    return (i < 0 || i >= _cfg.wordCount) ? nullptr : &_cfg.words[i];
}

int Sign::letterByName(const String &name) const {
    for (uint8_t i = 0; i < _cfg.letterCount; i++)
        if (name.equalsIgnoreCase(_cfg.letters[i].name)) return i;
    return -1;
}

int Sign::wordByName(const String &name) const {
    for (uint8_t i = 0; i < _cfg.wordCount; i++)
        if (name.equalsIgnoreCase(_cfg.words[i].name)) return i;
    return -1;
}

uint16_t Sign::wordLength(int w) const {
    if (w < 0 || w >= _cfg.wordCount) return 0;
    const Word &wd = _cfg.words[w];
    uint16_t total = 0;
    for (uint8_t k = 0; k < wd.count; k++) {
        uint8_t li = wd.first + k;
        if (li >= _cfg.letterCount) break;
        const Letter &l = _cfg.letters[li];
        if (!l.enabled || l.start >= _cfg.s.ledCount) continue;
        uint16_t len = l.length();
        if (l.start + len > _cfg.s.ledCount) len = _cfg.s.ledCount - l.start;
        total += len;
    }
    return total;
}

bool Sign::setRange(int i, uint16_t start, uint16_t end) {
    Letter *l = letter(i);
    if (!l) return false;
    if (end < start) { uint16_t t = start; start = end; end = t; }
    l->start = start;
    l->end   = min<uint16_t>(end, MAX_LEDS - 1);
    _dirty = true;
    return true;
}

/* ------------------------------------------------------------- validation */
uint8_t Sign::validate(Issue *out, uint8_t max) const {
    uint8_t n = 0;
    auto push = [&](int a, int b, const char *what) {
        if (n < max) out[n] = {a, b, what};
        n++;
    };
    for (uint8_t i = 0; i < _cfg.letterCount; i++) {
        const Letter &a = _cfg.letters[i];
        if (a.end < a.start)            push(i, -1, "end before start");
        if (a.end >= _cfg.s.ledCount)   push(i, -1, "range past ledCount");
        for (uint8_t j = i + 1; j < _cfg.letterCount; j++) {
            const Letter &b = _cfg.letters[j];
            if (a.start <= b.end && b.start <= a.end) push(i, j, "overlap");
        }
    }
    return n;
}

/* -------------------------------------------------------------------- JSON */
void Sign::toJson(JsonObject root) const {
    JsonObject s = root["settings"].to<JsonObject>();
    s["power"]        = _cfg.s.power;
    s["brightness"]   = _cfg.s.brightness;
    s["speed"]        = _cfg.s.speed;
    s["ledCount"]     = _cfg.s.ledCount;
    s["maxMilliamps"] = _cfg.s.maxMilliamps;
    s["dataPin"]      = _cfg.s.dataPin;
    s["autoShow"]     = _cfg.s.autoShow;
    s["anim"]         = _cfg.s.animation;
    s["animName"]     = animationName(_cfg.s.animation);
    s["logoPin"]      = _cfg.s.logoPin;
    s["logoLedCount"] = _cfg.s.logoLedCount;
    s["logoColor"]    = hex(_cfg.s.logoColor);
    s["logoFadeMs"]     = _cfg.s.logoFadeMs10 * 10;
    s["logoAutoTrigger"] = _cfg.s.logoAutoTrigger;

    JsonArray words = root["words"].to<JsonArray>();
    for (uint8_t i = 0; i < _cfg.wordCount; i++) {
        const Word &w = _cfg.words[i];
        JsonObject o = words.add<JsonObject>();
        o["i"]        = i;
        o["name"]     = w.name;
        o["color"]    = hex(w.color);
        o["first"]    = w.first;
        o["count"]    = w.count;
        o["len"]      = wordLength(i);
    }

    JsonArray letters = root["letters"].to<JsonArray>();
    for (uint8_t i = 0; i < _cfg.letterCount; i++) {
        const Letter &l = _cfg.letters[i];
        JsonObject o = letters.add<JsonObject>();
        o["i"]        = i;
        o["name"]     = l.name;
        o["start"]    = l.start;
        o["end"]      = l.end;
        o["len"]      = l.length();
        o["enabled"]  = l.enabled;
        o["reversed"] = l.reversed;
        /* which word owns it, so the UI can group the table */
        int owner = -1;
        for (uint8_t w = 0; w < _cfg.wordCount; w++)
            if (i >= _cfg.words[w].first && i < _cfg.words[w].first + _cfg.words[w].count)
                owner = w;
        o["word"] = owner;
    }

    JsonArray logo = root["logo"].to<JsonArray>();
    for (uint8_t i = 0; i < _cfg.logoSegCount; i++) {
        const LogoSegment &l = _cfg.logoSegs[i];
        JsonObject o = logo.add<JsonObject>();
        o["i"]        = i;
        o["name"]     = l.name;
        o["start"]    = l.start;
        o["end"]      = l.end;
        o["len"]      = l.length();
        o["enabled"]  = l.enabled;
        o["reversed"] = l.reversed;
        o["card"]     = l.card;
        o["priority"] = l.priority;
    }

    JsonArray anims = root["animations"].to<JsonArray>();
    for (uint8_t i = 0; i < ANIM_COUNT; i++) {
        JsonObject a = anims.add<JsonObject>();
        a["id"]   = i;
        a["name"] = animationName(i);
    }

    JsonArray issues = root["issues"].to<JsonArray>();
    Issue found[12];
    uint8_t n = validate(found, 12);
    for (uint8_t i = 0; i < n && i < 12; i++) {
        JsonObject o = issues.add<JsonObject>();
        o["a"] = found[i].a;
        o["b"] = found[i].b;
        o["what"] = found[i].what;
    }
    root["dirty"] = _dirty;
}
