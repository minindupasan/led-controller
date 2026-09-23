/*
 * ============================================================================
 *  INNOV IOT OIL LAMP  -  ESP #2
 *
 *  Drives the lamp only: 15 guest segments on GPIO27, one per RFID card.
 *
 *  This board hosts the Wi-Fi everything else joins:
 *
 *      Mac + Arduino  --HTTP-->  LAMP (192.168.4.1)  --HTTP-->  SIGN (.2)
 *        CARD:<uid>              logo card <uid>              show start
 *
 *  A tap arrives from the Mac bridge as  /api/cmd?c=logo+card+<UID>  and
 *  lights that guest's segment. When the last one is lit the lamp is full,
 *  and it tells the sign to run its opener.
 *
 *  Control: http://192.168.4.1 (or http://lamp.local), or USB serial @115200.
 * ============================================================================
 */

#include <Arduino.h>

#include "Config.h"
#include "Logger.h"
#include "Sign.h"
#include "Lamp.h"
#include "Net.h"
#include "DebugConsole.h"

static uint32_t lastHeartbeat = 0;
static uint32_t lastAutosave  = 0;

void setup() {
    Log.begin(115200);
    pinMode(STATUS_LED_PIN, OUTPUT);

    Log.log("=== INNOV IOT OIL LAMP (ESP #2) ===");
    Log.log("[sys] chip %s, %u MHz", ESP.getChipModel(), ESP.getCpuFreqMHz());

    TheSign.begin();           // shared config store; the lamp uses its half
    Logo.begin();              // the lamp strip on GPIO27
    Net.begin();               // hosts the AP + web UI + websocket
    Console.begin();

    Log.log("[sys] ready: %u guests, %u LEDs on GPIO%u",
            TheSign.logoSegCount(), Logo.ledCount(), Logo.activePin());
    Console.emitHello();
    Serial.print(F("lamp> "));
}

void loop() {
    Logo.loop();
    Console.loop();
    Net.loop();

    uint32_t now = millis();

    /* heartbeat: slow when idle, fast while a test overlay is painting */
    uint32_t period = (Logo.test().mode == TEST_NONE) ? 2000 : 250;
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
