/*
 * Config.h - compile-time configuration & shared data types
 * INNOV+IOT WS2812B sign controller
 */
#pragma once

#include <Arduino.h>
#include "Color.h"

/* ---------------------------------------------------------------- hardware */
#define DEFAULT_LED_PIN     13      // WS2812B DIN (through a 330R resistor)
                                    // runtime-selectable: `pin <n>` + reboot
                                    // GPIO14 has no boot-strapping role, unlike
                                    // 12 and 15, so it is a safe data line.
#define LED_COLOR_ORDER     GRB     // WS2812B is GRB
#define MAX_LEDS            2000    // hard ceiling for the static buffers
                                    // (4 static RGB/uint8 buffers ~ 16 kB RAM)
#define DEFAULT_LED_COUNT   1000    // the real strip length     // runtime count, editable from the web UI
#define PSU_VOLTS           5
#define DEFAULT_MAX_MA      0       // total draw cap in mA, enforced in
                                    // pushToStrip(). 0 = no cap, run the
                                    // strip at the brightness asked for.

#define STATUS_LED_PIN      2       // on-board LED, heartbeat

/* ------------------------------------------------------------------ system */
#define MAX_SEGMENTS        24
#define MAX_DIVISIONS       16      // sub-sections inside one segment (logo)
#define SEG_NAME_LEN        12
/* Upper bound only. WS2812B needs ~30 us per LED, so the real ceiling falls
   out of the strip length (1000 LEDs -> 30 ms -> ~33 fps). LedController
   derives the actual frame budget from ledCount at runtime. */
#define TARGET_FPS          60
#define LED_US_PER_PIXEL    30
#define TEST_TIMEOUT_MS     120000  // a forgotten test overlay releases itself
#define CONFIG_VERSION      7

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
    ANIM_BUILD,                     // sub-sections light up cumulatively, then all glow
    ANIM_STEPS,                     // one sub-section at a time, then all glow
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
    RGB      color;                 // primary colour
    RGB      color2;                // secondary (gradient / comet tail)
    uint8_t  animation;             // AnimationId or ANIM_INHERIT
    uint8_t  brightness;            // 0-255 local scale

    /* Sub-sections: the logo is built from several pieces that BUILD / STEPS
       light in turn before the final all-glow step. Divisions are a
       contiguous partition of the segment, described by each piece's last
       LED; with customDiv == false they are recomputed as an even split. */
    uint8_t  divisions;             // 0 or 1 = undivided
    bool     customDiv;             // true = divEnd[] was edited by hand
    uint16_t divEnd[MAX_DIVISIONS];

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
    uint8_t  dataPin;               // GPIO driving the strip (needs a reboot)
    uint16_t maxMilliamps;          // power budget
    bool     power;                 // master on/off
    bool     rainbowTint;           // auto-cycle hue over all segments
};

/* Unused while the Wi-Fi transport is parked in extras/wifi/, but kept in the
   config blob so re-enabling it does not invalidate saved settings. */
struct WifiSettings {
    char ssid[33];
    char pass[65];
    bool useSta;
    bool enabled;
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
    RGB      color     = RGB_WHITE;
    uint16_t walkDelay = 400;
    uint32_t lastStep  = 0;
};

/* Sub-section helpers (implemented in SegmentManager.cpp). */
uint8_t divisionCount(const Segment &s);
bool    divisionBounds(const Segment &s, uint8_t k, uint16_t &start, uint16_t &end);

/* Output-capable GPIOs that are safe for a WS2812B data line on an ESP32:
   not the flash pins (6-11), not input-only (34-39), and not UART0 (1/3),
   which this firmware needs for the console. */
bool        isValidLedPin(uint8_t pin);
const char *ledPinWarning(uint8_t pin);   // nullptr when the pin is unremarkable

extern const char *animationName(uint8_t id);
extern uint8_t     animationIdFromName(const String &name);
