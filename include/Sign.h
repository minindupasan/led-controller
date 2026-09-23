/*
 * Sign.h - the sign's configuration: letters (geometry), words (look), and
 * the settings around them, persisted in NVS.
 */
#pragma once

#include "Config.h"
#include <ArduinoJson.h>

class Sign {
public:
    void begin();
    void loadDefaults();               // the measured INNOV IOT map
    bool load();
    bool save();
    void factoryReset();

    SignConfig &config()   { return _cfg; }
    Settings   &settings() { return _cfg.s; }

    /* letters ---------------------------------------------------------- */
    uint8_t  letterCount() const { return _cfg.letterCount; }
    Letter  *letter(int i);
    int      letterByName(const String &name) const;
    bool     setRange(int i, uint16_t start, uint16_t end);

    /* words ------------------------------------------------------------ */
    uint8_t  wordCount() const { return _cfg.wordCount; }
    Word    *word(int i);
    int      wordByName(const String &name) const;   // "all" handled by caller
    uint16_t wordLength(int w) const;                // total lit pixels

    /* logo segments ---------------------------------------------------- */
    uint8_t      logoSegCount() const { return _cfg.logoSegCount; }
    LogoSegment *logoSeg(int i);
    int          logoSegByName(const String &name) const;
    int          logoSegByCard(const String &uid) const;   // -1 = unknown card
    bool         setLogoRange(int i, uint16_t start, uint16_t end);
    bool         setLogoSegCount(uint8_t n);               // re-splits evenly
    void         spreadLogoEvenly();
    int          addLogoSeg(const String &name, uint16_t start, uint16_t end);
    bool         removeLogoSeg(int i);
    bool         assignCard(int i, const String &uid);     // "" clears it
    bool         setLogoName(int i, const String &name);
    bool         setLogoPriority(int i, uint8_t priority);
    void         sortLogoByPriority();

    /* validation ------------------------------------------------------- */
    struct Issue { int a; int b; const char *what; };
    uint8_t validate(Issue *out, uint8_t max) const;

    /* serialisation ---------------------------------------------------- */
    void toJson(JsonObject root) const;

    bool dirty() const { return _dirty; }
    void markDirty()   { _dirty = true; }

private:
    SignConfig _cfg{};
    bool       _dirty = false;
};

extern Sign TheSign;
