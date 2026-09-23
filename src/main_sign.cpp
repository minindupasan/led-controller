/*
 * ============================================================================
 *  INNOV IOT LED SIGN  -  ESP #1
 *  ESP32 + WS2812B, 591 LEDs on GPIO13, driven through NeoPixelBus/RMT.
 *
 *  Letters carry geometry; words (INNOV, IOT) carry colour and animation.
 *  The show runs: wait -> TRAVERSE opener -> calm BREATHE, and any manual
 *  change hands control back to you.
 *
 *  Drives the letters only. The oil lamp is ESP #2, a separate board with
 *  its own firmware (env:lamp) - it hosts the Wi-Fi and owns the RFID.
 *
 *  This board joins that network as 192.168.4.2 and waits. When the last
 *  guest taps their card, the lamp sends `show start` here and the sign runs
 *  TRAVERSE once, then settles into BREATHE.
 *
 *  Control: http://192.168.4.2 (or http://sign.local) on the lamp's network,
 *  or the same commands over USB serial @115200.
 *
 *  The oil-lamp logo runs on a separate ESP32; it can trigger the opener
 *  here by sending `show start`.
 * ============================================================================
 */

#include <Arduino.h>

#include "Config.h"
#include "Logger.h"
#include "Sign.h"
#include "LedController.h"
#include "Show.h"
#include "DebugConsole.h"
#include "Net.h"

static uint32_t lastHeartbeat = 0;
static uint32_t lastAutosave  = 0;

void setup() {
    Log.begin(115200);
    pinMode(STATUS_LED_PIN, OUTPUT);

    Log.log("=== INNOV IOT SIGN (ESP #1) ===");
    Log.log("[sys] chip %s, %u MHz, flash %u MB",
            ESP.getChipModel(), ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / (1024 * 1024));

    TheSign.begin();
    Leds.begin();
    Net.begin();               // AP + web UI + websocket
    TheShow.begin();
    Console.begin();

    Log.log("[sys] ready: %u letters, %u words, %u leds on GPIO%u",
            TheSign.letterCount(), TheSign.wordCount(),
            TheSign.settings().ledCount, Leds.activePin());
    Console.emitHello();
    Serial.print(F("sign> "));
}

void loop() {
    Leds.loop();
    TheShow.loop();
    Console.loop();
    Net.loop();

    uint32_t now = millis();

    /* heartbeat: slow when idle, fast while a test overlay is painting */
    uint32_t period = (Leds.test().mode == TEST_NONE) ? 2000 : 250;
    if (now - lastHeartbeat >= period) {
        lastHeartbeat = now;
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    }

    /* autosave 30 s after the last change so edits survive a power cut */
    if (TheSign.dirty() && now - lastAutosave > 30000) {
        lastAutosave = now;
        TheSign.save();
        Log.log("[cfg] autosaved");
    }
}
