/*
 * ============================================================================
 *  INNOV+IOT LED SIGN CONTROLLER
 *  ESP32 + WS2812B  -  per-letter segments, animation engine,
 *  serial debug console and a black & white web UI.
 *
 *  Boot:  tries stored Wi-Fi credentials, otherwise starts the AP
 *         SSID "INNOV-IOT-SIGN" / pass "innoviot123"  ->  http://192.168.4.1
 *         (also reachable at http://ledsign.local)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "Config.h"
#include "Logger.h"
#include "SegmentManager.h"
#include "LedController.h"
#include "AnimationEngine.h"
#include "DebugConsole.h"
#include "WebInterface.h"

static uint32_t lastHeartbeat = 0;
static uint32_t lastAutosave  = 0;

static void startNetwork() {
    WifiSettings &w = Segments.config().wifi;

    if (w.useSta && strlen(w.ssid)) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(w.ssid, w.pass);
        Log.log("[net] joining \"%s\" ...", w.ssid);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_CONNECT_TIMEOUT) {
            delay(250);
        }
        if (WiFi.status() == WL_CONNECTED) {
            Log.log("[net] connected, ip %s", WiFi.localIP().toString().c_str());
        } else {
            Log.log("[net] join failed - falling back to AP");
            w.useSta = false;
        }
    }

    if (!w.useSta || WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Log.log("[net] AP \"%s\", ip %s", AP_SSID, WiFi.softAPIP().toString().c_str());
    }

    if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        Log.log("[net] mdns: http://%s.local", MDNS_HOST);
    }
}

void setup() {
    Log.begin(115200);
    pinMode(STATUS_LED_PIN, OUTPUT);

    Log.log("=== INNOV+IOT SIGN CONTROLLER ===");
    Log.log("[sys] chip %s, %u MHz, flash %u MB",
            ESP.getChipModel(), ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / (1024 * 1024));

    Segments.begin();          // NVS config (or defaults)
    Leds.begin();              // FastLED + animation state
    startNetwork();
    Web.begin();               // HTTP + websocket
    Console.begin();           // serial menu

    Log.log("[sys] ready: %u segments, %u leds", Segments.count(), Segments.globals().ledCount);
    Serial.print(F("sign> "));
}

void loop() {
    Leds.loop();               // render + show
    Console.loop();            // serial commands
    Web.loop();                // websocket housekeeping

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
