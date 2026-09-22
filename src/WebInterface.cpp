#include "WebInterface.h"
#include "WebPage.h"
#include "Config.h"
#include "SegmentManager.h"
#include "LedController.h"
#include "DebugConsole.h"
#include "Logger.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

WebInterface Web;

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static uint32_t lastStatus = 0;

/* --------------------------------------------------------------- utilities */
static CRGB hexToColor(const String &s, CRGB fb = CRGB::White) {
    String v = s;
    v.replace("#", "");
    if (v.length() != 6) return fb;
    uint32_t rgb = strtoul(v.c_str(), nullptr, 16);
    return CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void sendJson(AsyncWebServerRequest *req, JsonDocument &doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    req->send(code, "application/json", out);
}

static void ok(AsyncWebServerRequest *req, const char *msg = "ok") {
    JsonDocument d;
    d["ok"] = true;
    d["msg"] = msg;
    sendJson(req, d);
}

static void fail(AsyncWebServerRequest *req, const String &why) {
    JsonDocument d;
    d["ok"] = false;
    d["msg"] = why;
    sendJson(req, d, 400);
}

/* Body-collecting POST helper: buffers the chunks, parses once complete. */
typedef std::function<void(AsyncWebServerRequest *, JsonDocument &)> JsonHandler;

static void onJsonPost(const char *path, JsonHandler handler) {
    server.on(path, HTTP_POST,
        [](AsyncWebServerRequest *req) { /* handled in the body callback */ },
        nullptr,
        [handler](AsyncWebServerRequest *req, uint8_t *data, size_t len,
                  size_t index, size_t total) {
            String *buf = nullptr;
            if (index == 0) {
                buf = new String();
                buf->reserve(total + 1);
                req->_tempObject = buf;
            } else {
                buf = (String *)req->_tempObject;
            }
            if (!buf) return;
            for (size_t i = 0; i < len; i++) *buf += (char)data[i];

            if (index + len == total) {
                JsonDocument doc;
                DeserializationError err = buf->length() ? deserializeJson(doc, *buf)
                                                         : DeserializationError::Ok;
                delete buf;
                req->_tempObject = nullptr;
                if (err) { fail(req, String("bad json: ") + err.c_str()); return; }
                handler(req, doc);
            }
        });
}

/* ------------------------------------------------------------- status push */
static void buildStatus(JsonDocument &d) {
    const TestState &t = Leds.test();
    static const char *TEST_NAMES[] = {"none", "index", "range", "all", "identify", "walk"};

    d["type"]      = "status";
    d["fps"]       = Leds.fps();
    d["frames"]    = Leds.frames();
    d["ma"]        = Leds.estimatedMilliamps();
    d["heap"]      = ESP.getFreeHeap();
    d["uptime"]    = millis() / 1000;
    d["leds"]      = Segments.globals().ledCount;
    d["segments"]  = Segments.count();
    d["testMode"]  = TEST_NAMES[t.mode <= TEST_WALK ? t.mode : 0];
    d["testIndex"] = t.index;
    d["dirty"]     = Segments.dirty();
    d["mode"]      = (WiFi.getMode() == WIFI_AP) ? "AP" : "STA";
    d["ip"]        = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString()
                                                 : WiFi.localIP().toString();
    d["rssi"]      = WiFi.RSSI();
}

void WebInterface::pushLog(const char *line) {
    if (ws.count() == 0) return;
    JsonDocument d;
    d["type"] = "log";
    d["line"] = line;
    String out;
    serializeJson(d, out);
    ws.textAll(out);
}

bool WebInterface::clientsConnected() const { return ws.count() > 0; }

static void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Log.log("[web] client %u connected", client->id());
        for (uint8_t i = 0; i < Log.count(); i++) {
            JsonDocument d;
            d["type"] = "log";
            d["line"] = Log.line(i);
            String out;
            serializeJson(d, out);
            client->text(out);
        }
    } else if (type == WS_EVT_DISCONNECT) {
        Log.log("[web] client disconnected");
    } else if (type == WS_EVT_DATA) {
        String cmd;
        for (size_t i = 0; i < len; i++) cmd += (char)data[i];
        String out = Console.execute(cmd);
        if (out.length()) Log.log("%s", out.c_str());
    }
}

/* -------------------------------------------------------------------- routes */
static void setupRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        AsyncWebServerResponse *res = req->beginResponse(200, "text/html", INDEX_HTML);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
    });

    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument d;
        Segments.toJson(d.to<JsonObject>());
        sendJson(req, d);
    });

    server.on("/api/export", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument d;
        Segments.toJson(d.to<JsonObject>());
        String out;
        serializeJsonPretty(d, out);
        AsyncWebServerResponse *res = req->beginResponse(200, "application/json", out);
        res->addHeader("Content-Disposition", "attachment; filename=sign-config.json");
        req->send(res);
    });

    onJsonPost("/api/global", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        String err;
        uint16_t before = Segments.globals().ledCount;
        Segments.applyGlobalJson(doc.as<JsonObjectConst>(), err);
        if (Segments.globals().ledCount != before ||
            doc["maxMilliamps"].is<uint16_t>()) Leds.restartStrip();
        ok(req);
    });

    onJsonPost("/api/segment", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        if (!req->hasParam("i")) { fail(req, "missing ?i="); return; }
        int i = req->getParam("i")->value().toInt();
        String err;
        if (!Segments.applySegmentJson(i, doc.as<JsonObjectConst>(), err)) { fail(req, err); return; }
        ok(req);
    });

    onJsonPost("/api/segment/add", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        int i = Segments.add(doc["name"] | "SEG", doc["start"] | 0, doc["end"] | 0);
        if (i < 0) { fail(req, "segment table full"); return; }
        Log.log("[seg] added %s %u-%u", (const char *)(doc["name"] | "SEG"),
                (unsigned)(doc["start"] | 0), (unsigned)(doc["end"] | 0));
        ok(req);
    });

    onJsonPost("/api/segment/del", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        if (!Segments.remove(doc["i"] | -1)) { fail(req, "no such segment"); return; }
        ok(req);
    });

    onJsonPost("/api/segments/sort", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        Segments.sortByStart();
        ok(req);
    });

    onJsonPost("/api/identify", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        Leds.identify(doc["i"] | -1);
        ok(req);
    });

    onJsonPost("/api/test", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        String mode = doc["mode"] | "off";
        CRGB col = hexToColor(doc["color"] | "#FFFFFF");
        if      (mode == "index") Leds.testIndex(doc["index"] | 0, col);
        else if (mode == "range") Leds.testRange(doc["index"] | 0, doc["end"] | 0, col);
        else if (mode == "all")   Leds.testAll(col);
        else if (mode == "walk")  Leds.testWalk(doc["delay"] | 400);
        else                      Leds.testOff();
        ok(req);
    });

    onJsonPost("/api/cmd", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        String out = Console.execute(doc["cmd"] | "");
        JsonDocument d;
        d["ok"]  = true;
        d["out"] = out;
        sendJson(req, d);
    });

    onJsonPost("/api/save", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        Segments.save() ? ok(req, "saved") : fail(req, "save failed");
    });

    onJsonPost("/api/load", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        Segments.load() ? ok(req, "loaded") : fail(req, "nothing stored");
        Leds.restartStrip();
    });

    onJsonPost("/api/defaults", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        Segments.loadDefaults();
        Leds.restartStrip();
        ok(req, "defaults restored");
    });

    onJsonPost("/api/import", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        String err;
        if (!Segments.importJson(doc.as<JsonObjectConst>(), err)) { fail(req, err); return; }
        Leds.restartStrip();
        ok(req, "imported");
    });

    onJsonPost("/api/reboot", [](AsyncWebServerRequest *req, JsonDocument &doc) {
        ok(req, "rebooting");
        delay(150);
        ESP.restart();
    });

    server.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("/");
    });
}

/* --------------------------------------------------------------------- api */
void WebInterface::begin() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    setupRoutes();
    server.begin();
    Log.setSink([](const char *l) { Web.pushLog(l); });
    Log.log("[web] http server on port 80");
}

void WebInterface::loop() {
    ws.cleanupClients();
    uint32_t now = millis();
    if (now - lastStatus >= 500 && ws.count()) {
        lastStatus = now;
        JsonDocument d;
        buildStatus(d);
        String out;
        serializeJson(d, out);
        ws.textAll(out);
    }
}
