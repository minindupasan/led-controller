/*
 * SegmentManager.h - owns the SignConfig, its NVS persistence and all
 * segment bookkeeping (add / edit / remove / validate / JSON).
 */
#pragma once

#include "Config.h"
#include <ArduinoJson.h>

class SegmentManager {
public:
    void begin();                       // load from NVS, or defaults
    void loadDefaults();                // INNOV+IOT + LOGO layout
    bool load();                        // NVS -> cfg   (false = nothing stored)
    bool save();                        // cfg -> NVS
    void factoryReset();                // wipe NVS and reload defaults

    SignConfig       &config()       { return _cfg; }
    GlobalSettings   &globals()      { return _cfg.g; }
    uint8_t           count() const  { return _cfg.segmentCount; }
    Segment          *get(int i);
    const Segment    *get(int i) const;
    int               indexOfName(const String &name) const;

    int  add(const String &name, uint16_t start, uint16_t end);
    bool remove(int i);
    bool setRange(int i, uint16_t start, uint16_t end);
    void sortByStart();
    bool spreadEvenly();               // divide ledCount evenly between segments

    /* sub-sections (logo pieces) */
    bool setDivisions(int i, uint8_t n);                       // even split
    bool setDivisionRange(int i, uint8_t k, uint16_t a, uint16_t b);
    bool autoSplit(int i);                                     // back to even
    void divisionsToJson(int i, JsonArray arr) const;

    /* Validation: reports overlaps / out-of-range / inverted ranges. */
    struct Issue { int segA; int segB; const char *what; };
    uint8_t validate(Issue *out, uint8_t maxIssues) const;
    bool    isValid() const { Issue tmp[1]; return validate(tmp, 1) == 0; }

    /* Serialisation */
    void toJson(JsonObject root) const;             // full state
    void segmentToJson(int i, JsonObject o) const;
    bool applySegmentJson(int i, JsonObjectConst o, String &err);
    bool applyGlobalJson(JsonObjectConst o, String &err);
    bool importJson(JsonObjectConst root, String &err);

    bool dirty() const { return _dirty; }
    void markDirty()   { _dirty = true; }
    void clearDirty()  { _dirty = false; }

private:
    SignConfig _cfg{};
    bool       _dirty = false;
};

extern SegmentManager Segments;
