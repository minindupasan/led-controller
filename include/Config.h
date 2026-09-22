/*
 * Config.h - compile-time configuration & shared data types
 * INNOV+IOT WS2812B sign controller
 */
#pragma once

#include <Arduino.h>
#include <FastLED.h>

/* ---------------------------------------------------------------- hardware */
#define LED_DATA_PIN        13      // WS2812B DIN (through a 330R resistor)
#define LED_COLOR_ORDER     GRB     // WS2812B is GRB
#define MAX_LEDS            1200    // hard ceiling for the static buffers
#define DEFAULT_LED_COUNT   300     // runtime count, editable from the web UI
#define PSU_VOLTS           5
#define DEFAULT_MAX_MA      8000    // power budget handed to FastLED

#define STATUS_LED_PIN      2       // on-board LED, heartbeat

/* ------------------------------------------------------------------ system */
#define MAX_SEGMENTS        24
#define SEG_NAME_LEN        12
#define TARGET_FPS          120
#define CONFIG_VERSION      2

/* ----------------------------------------------------------------- network */
#define AP_SSID             "INNOV-IOT-SIGN"
#define AP_PASSWORD         "innoviot123"   // >= 8 chars
#define MDNS_HOST           "ledsign"       // http://ledsign.local
#define STA_CONNECT_TIMEOUT 12000           // ms before falling back to AP

/* -------------------------------------------------------------- animations */
enum AnimationId : uint8_t {
    ANIM_OFF = 0,
    ANIM_SOLID,
    ANIM_BREATHE,
    ANIM_PULSE,
    ANIM_TRAVERSE,
    ANIM_COMET,
    ANIM_WIPE,
    ANIM_THEATER,
    ANIM_SPARKLE,
    ANIM_RAINBOW,
    ANIM_GRADIENT,
    ANIM_STROBE,
    ANIM_FIRE,
    ANIM_TWINKLE,
    ANIM_COUNT,
    ANIM_INHERIT = 255              // segment follows the global animation
};

/* Order in which segments are staggered / sequenced. */
enum PlayMode : uint8_t {
    PLAY_PARALLEL = 0,   // every segment animates together
    PLAY_STAGGER,        // each segment phase-shifted by `stagger` ms
    PLAY_SEQUENCE        // one segment at a time, round-robin
};

/* ------------------------------------------------------------ data objects */
struct Segment {
    char     name[SEG_NAME_LEN];
    uint16_t start;                 // first LED index (inclusive)
    uint16_t end;                   // last  LED index (inclusive)
    bool     enabled;
    bool     reversed;              // physical wiring runs backwards
    CRGB     color;                 // primary colour
    CRGB     color2;                // secondary (gradient / comet tail)
    uint8_t  animation;             // AnimationId or ANIM_INHERIT
    uint8_t  brightness;            // 0-255 local scale

    uint16_t length() const { return (end >= start) ? (end - start + 1) : 0; }
};

struct GlobalSettings {
    uint8_t  brightness;            // master 0-255
    uint8_t  speed;                 // 1-255
    uint8_t  intensity;             // animation-specific "amount" 0-255
    uint8_t  animation;             // default animation for segments
    uint8_t  playMode;              // PlayMode
    uint16_t stagger;               // ms offset per segment
    uint16_t ledCount;              // active LEDs on the strip
    uint16_t maxMilliamps;          // power budget
    bool     power;                 // master on/off
    bool     rainbowTint;           // auto-cycle hue over all segments
};

struct WifiSettings {
    char ssid[33];
    char pass[65];
    bool useSta;
};

/* Whole persisted configuration. */
struct SignConfig {
    uint16_t       version;
    GlobalSettings g;
    WifiSettings   wifi;
    uint8_t        segmentCount;
    Segment        segments[MAX_SEGMENTS];
};

/* Live, non-persisted debug/test overlay. */
enum TestMode : uint8_t {
    TEST_NONE = 0,
    TEST_INDEX,      // light exactly one LED
    TEST_RANGE,      // light an arbitrary range
    TEST_ALL,        // every LED white
    TEST_IDENTIFY,   // blink one segment
    TEST_WALK        // auto-advancing single LED (mapping helper)
};

struct TestState {
    uint8_t  mode      = TEST_NONE;
    uint16_t index     = 0;
    uint16_t rangeEnd  = 0;
    int8_t   segment   = -1;
    CRGB     color     = CRGB::White;
    uint16_t walkDelay = 400;
    uint32_t lastStep  = 0;
};

extern const char *animationName(uint8_t id);
extern uint8_t     animationIdFromName(const String &name);
