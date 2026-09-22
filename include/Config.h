/*
 * Config.h - hardware setup and the sign's data model.
 *
 * The board drives the INNOV IOT letters only: 591 LEDs, contiguous.
 * The oil-lamp logo lives on a separate ESP32.
 *
 * Geometry belongs to letters; colour and animation belong to words.
 */
#pragma once

#include <Arduino.h>
#include "Color.h"

/* ---------------------------------------------------------------- hardware */
#define DEFAULT_LED_PIN     13      // WS2812B DIN (through a 330R resistor)
#define MAX_LEDS            2000    // ceiling for the static frame buffers
#define DEFAULT_LED_COUNT   591     // measured: letters I..T, 0-590
#define PSU_VOLTS           5
#define DEFAULT_MAX_MA      0       // 0 = no cap; brightness 255 is true full
#define STATUS_LED_PIN      2

/* ------------------------------------------------------------------ limits */
#define MAX_LETTERS         16
#define MAX_WORDS           4
#define NAME_LEN            8
#define TARGET_FPS          60      // upper bound; the wire decides the rest
#define LED_US_PER_PIXEL    30
#define CONFIG_VERSION      8

/* ----------------------------------------------------------------- palette */
/* Purple / cyan / amber. Gradients stay inside a base hue - see tintOf(). */
#define COL_PURPLE          0x7B2FF7
#define COL_CYAN            0x00E5FF
#define COL_AMBER           0xFFB020

/* -------------------------------------------------------------- animations */
enum AnimationId : uint8_t {
    ANIM_OFF = 0,
    ANIM_SOLID,
    ANIM_BREATHE,       // calm pulse - the resting state
    ANIM_TRAVERSE,      // band sweeps the word, letter to letter, no gaps
    ANIM_AURORA,        // layered waves, hue drifting near the base colour
    ANIM_COMET,         // bright head with a decaying tail
    ANIM_COUNT
};

/* ------------------------------------------------------------ data objects */
struct Letter {                     // geometry only
    char     name[NAME_LEN];
    uint16_t start;                 // first LED (inclusive)
    uint16_t end;                   // last  LED (inclusive)
    bool     enabled;
    bool     reversed;              // physical run is wired backwards

    uint16_t length() const { return (end >= start) ? (end - start + 1) : 0; }
};

struct Word {                       // what you actually set
    char    name[NAME_LEN];         // "INNOV", "IOT"
    uint8_t first;                  // index of its first letter
    uint8_t count;                  // how many letters it spans
    RGB     color;
    uint8_t animation;
};

struct Settings {
    uint8_t  brightness;            // master 0-255
    uint8_t  speed;                 // 1-255
    uint16_t ledCount;
    uint16_t maxMilliamps;          // 0 = uncapped
    uint8_t  dataPin;
    bool     power;
    bool     autoShow;              // run the opener on boot
};

struct SignConfig {
    uint16_t version;
    Settings s;
    uint8_t  letterCount;
    uint8_t  wordCount;
    Letter   letters[MAX_LETTERS];
    Word     words[MAX_WORDS];
};

/* Test overlay - the mapping tools paint over everything while active. */
enum TestMode : uint8_t {
    TEST_NONE = 0, TEST_INDEX, TEST_RANGE, TEST_ALL, TEST_IDENTIFY, TEST_WALK
};

struct TestState {
    uint8_t  mode      = TEST_NONE;
    uint16_t index     = 0;
    uint16_t rangeEnd  = 0;
    int8_t   letter    = -1;
    RGB      color     = RGB_WHITE;
    uint16_t walkDelay = 400;
    uint32_t lastStep  = 0;
};

#define TEST_TIMEOUT_MS 120000      // a forgotten overlay releases itself

const char *animationName(uint8_t id);
uint8_t     animationFromName(const String &name);
bool        isValidLedPin(uint8_t pin);
const char *ledPinWarning(uint8_t pin);
RGB         paletteColor(const String &name, bool *found = nullptr);
