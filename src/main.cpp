/*
 * ============================================================================
 *  INNOV+IOT LED SIGN CONTROLLER
 *  ESP32 + WS2812B  -  per-letter segments, animation engine, debug console.
 *
 *  Control surface: a line-based text protocol on USB serial @115200.
 *    - humans  : type commands in any serial monitor (`h` for the menu)
 *    - the UI  : webapp/index.html speaks the same protocol over Web Serial
 *
 *  (A Wi-Fi transport for the same protocol is parked in extras/wifi/.)
 *
 *  Machine-readable replies are single lines prefixed with "#J" followed by
 *  JSON; everything else is plain text meant for a human.
 * ============================================================================
 */

#include <Arduino.h>

#include "Config.h"
#include "Logger.h"
#include "SegmentManager.h"
#include "LedController.h"
#include "AnimationEngine.h"
#include "DebugConsole.h"

static uint32_t lastHeartbeat = 0;
static uint32_t lastAutosave  = 0;

void setup() {
    Log.begin(115200);
    pinMode(STATUS_LED_PIN, OUTPUT);

    Log.log("=== INNOV+IOT SIGN CONTROLLER ===");
    Log.log("[sys] chip %s, %u MHz, flash %u MB",
            ESP.getChipModel(), ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / (1024 * 1024));

    Segments.begin();          // NVS config (or defaults)
    Leds.begin();              // output driver + animation state
    Console.begin();           // serial menu + protocol

    Log.log("[sys] ready: %u segments, %u leds", Segments.count(), Segments.globals().ledCount);
    Console.emitHello();       // lets the web UI detect the board
    Serial.print(F("sign> "));
}

void loop() {
    Leds.loop();               // render + show
    Console.loop();            // serial commands

    uint32_t now = millis();

    /* heartbeat on the on-board LED: slow = idle, fast = test overlay active */
    uint32_t period = (Leds.test().mode == TEST_NONE) ? 2000 : 250;
    if (now - lastHeartbeat >= period) {
        lastHeartbeat = now;
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    }

    /* autosave 30 s after the last change so edits survive a power cut */
    if (Segments.dirty() && now - lastAutosave > 30000) {
        lastAutosave = now;
        Segments.save();
        Log.log("[cfg] autosaved");
    }
}
