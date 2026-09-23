/*
 * Config.h - hardware setup and the data model, shared by both boards.
 *
 * ROLE_SIGN  ESP #1: the INNOV IOT letters on GPIO13. Joins the lamp's AP as
 *            a client at 192.168.4.2 and waits to be told to run the opener.
 * ROLE_LAMP  ESP #2: the oil lamp on GPIO27. Hosts the AP, takes RFID taps
 *            from the Mac bridge, and triggers the sign when the lamp fills.
 *
 * The two share this header, the colour maths, the console and the web
 * transport, so a fix to either board lands on both.
 *
 * The board drives the INNOV IOT letters only: 591 LEDs, contiguous.
 * The oil-lamp logo lives on a separate ESP32.
 *
 * Letters carry geometry, words carry colour, the animation is global -
 * both words always run the same one.
 */
#pragma once

#include <Arduino.h>
#include "Color.h"

/* ---------------------------------------------------------------- hardware */
#define DEFAULT_LED_PIN     13      // ESP #1 letters data line (D13)
#define MAX_LEDS            2000    // ceiling for the static frame buffers
#define DEFAULT_LED_COUNT   591     // measured: letters I..T, 0-590
#define PSU_VOLTS           5
#define DEFAULT_MAX_MA      0       // 0 = no cap; brightness 255 is true full
#define STATUS_LED_PIN      2

/* ------------------------------------------------------- logo (oil lamp) --
 * A second WS2812B run on its own GPIO, driven from this same board on RMT
 * channel 1. It is split into segments like the letters, but each segment can
 * also carry an RFID card ID: tapping that card lights that segment.
 *
 * The reader is an Arduino on a laptop joined to this board's AP; the laptop
 * forwards each tap as  GET /api/cmd?c=logo+card+<UID>  (see bridge/).
 * When every segment is lit the lamp is full, which triggers the sign.
 */
#define DEFAULT_LOGO_PIN    27      // ESP #2 oil lamp data line (D27)
#define MAX_LOGO_LEDS       600
#define DEFAULT_LOGO_COUNT  180
#define MAX_LOGO_SEGS       24
#define DEFAULT_LOGO_SEGS   18
#define CARD_ID_LEN         14      // RFID UID as uppercase hex, plus a NUL
#define GUEST_NAME_LEN      30      // "Student Committee President" and friends

/* ------------------------------------------------------------------ limits */
#define MAX_LETTERS         16
#define MAX_WORDS           4
#define NAME_LEN            8
#define TARGET_FPS          60      // upper bound; the wire decides the rest
#define LED_US_PER_PIXEL    30
#define CONFIG_VERSION      13

/* ----------------------------------------------------------------- palette */
/* Purple / cyan / amber. Gradients stay inside a base hue - see tintOf(). */
#define COL_PURPLE          0x7B2FF7
#define COL_CYAN            0x00E5FF
#define COL_AMBER           0xFFB020

/* ----------------------------------------------------------------- network */
#define AP_SSID             "INNOV-IOT-SIGN"
#define AP_PASSWORD         "innoviot123"   // >= 8 characters
#define WS_STATUS_MS        500             // telemetry push interval

/* The lamp hosts the network at a fixed .1. The sign takes an ordinary DHCP
   lease and announces itself to the lamp, rather than claiming a static
   address - 192.168.4.2 is the first address the AP hands out, so pinning
   the sign there collides with whatever joined first. */
#define LAMP_IP             "192.168.4.1"
#define STA_RETRY_MS        5000            // the sign keeps trying to join
#define SIGN_ANNOUNCE_MS    20000           // and re-announces itself this often

#if ROLE_LAMP
  #define MDNS_HOST         "lamp"          // http://lamp.local
  #define DEVICE_NAME       "INNOV IOT LAMP"
  #define ROLE_NAME         "lamp"
#else
  #define MDNS_HOST         "sign"          // http://sign.local
  #define DEVICE_NAME       "INNOV IOT SIGN"
  #define ROLE_NAME         "sign"
#endif

/* -------------------------------------------------------------- animations */
enum AnimationId : uint8_t {
    ANIM_OFF = 0,
    ANIM_SOLID,
    ANIM_BREATHE,       // calm pulse - the resting state
    ANIM_TRAVERSE,      // band sweeps the word, letter to letter, no gaps
    ANIM_AURORA,        // layered waves, hue drifting near the base colour
    ANIM_COMET,         // bright head with a decaying tail
    ANIM_BOUNCE,        // traverse out, then sweep back the other way
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

/*
 * A logo segment: one guest.
 *
 *   priority  the order they appear in - 1 lights first. Also the order
 *             `logo level n` fills, and how the table is sorted.
 *   card      their RFID UID; tapping it lights this segment.
 *   start/end where their piece of the lamp actually is on the strip.
 */
struct LogoSegment {
    char     name[GUEST_NAME_LEN];
    uint8_t  priority;
    uint16_t start;
    uint16_t end;
    bool     enabled;
    bool     reversed;
    char     card[CARD_ID_LEN];     // empty = not assigned yet

    uint16_t length() const { return (end >= start) ? (end - start + 1) : 0; }
};

struct Word {                       // colour only - the animation is global
    char    name[NAME_LEN];         // "INNOV", "IOT"
    uint8_t first;                  // index of its first letter
    uint8_t count;                  // how many letters it spans
    RGB     color;
};

struct Settings {
    uint8_t  brightness;            // master 0-255
    uint8_t  speed;                 // 1-255
    uint8_t  animation;             // one animation for the whole sign
    uint16_t ledCount;
    uint16_t maxMilliamps;          // 0 = uncapped
    uint8_t  dataPin;
    bool     power;
    bool     autoShow;              // run the opener on boot

    /* logo strip - the oil lamp, on its own GPIO */
    uint8_t  logoPin;
    uint16_t logoLedCount;
    RGB      logoColor;
    uint8_t  logoFadeMs10;          // fade-in per segment, in units of 10 ms
    bool     logoAutoTrigger;       // full lamp starts the sign's opener
};

struct SignConfig {
    uint16_t version;
    Settings s;
    uint8_t  letterCount;
    uint8_t  wordCount;
    uint8_t  logoSegCount;
    Letter   letters[MAX_LETTERS];
    Word     words[MAX_WORDS];
    LogoSegment logoSegs[MAX_LOGO_SEGS];
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
