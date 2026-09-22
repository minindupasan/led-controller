#include "Net.h"
#include "Config.h"
#include "WebPage.h"
#include "SegmentManager.h"
#include "DebugConsole.h"
#include "Logger.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>

NetInterface Net;

static AsyncWebServer server(80);
static AsyncWebSocket socket("/ws");
static bool     started    = false;
static uint32_t lastStatus = 0;

/* Collects whatever a command prints so it can be sent as one WS frame. */
class LineSink : public Print {
public:
    String buf;
    size_t write(uint8_t c) override { buf += (char)c; return 1; }
    size_t write(const uint8_t *data, size_t len) override {
        buf.concat((const char *)data, len);
        return len;
    }
};

/* Runs a command with output redirected to the caller instead of Serial. */
static String runForClient(const String &cmd) {
    LineSink sink;
    Console.setOut(&sink);
    String tail = Console.execute(cmd);
    if (tail.length()) sink.println(tail);
    Console.setOut(&Serial);
    return sink.buf;
}

/* ------------------------------------------------------------------ socket */
static void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Log.log("[net] client %u connected from %s",
                    client->id(), client->remoteIP().toString().c_str());
            client->text(runForClient("hello"));
            break;

        case WS_EVT_DISCONNECT:
            Log.log("[net] client disconnected");
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo *)arg;
            if (!info->final || info->index != 0 || info->len != len) return;  // ignore fragments
            if (info->opcode != WS_TEXT) return;

            String cmd;
            cmd.reserve(len + 1);
            for (size_t i = 0; i < len; i++) cmd += (char)data[i];
            cmd.trim();
            if (!cmd.length()) return;

            String out = runForClient(cmd);
            if (out.length()) client->text(out);
            break;
        }
        default:
            break;
    }
}

void NetInterface::broadcastLog(const char *line) {
    if (!started || socket.count() == 0) return;
    socket.textAll(String(line) + "\n");
}

/* ------------------------------------------------------------------ routes */
static void setupRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        AsyncWebServerResponse *res =
            req->beginResponse(200, "text/html", WEBPAGE_GZ, WEBPAGE_GZ_LEN);
        res->addHeader("Content-Encoding", "gzip");
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
    });

    /* Convenience shim for curl / scripts:  /api/cmd?c=seg+range+I+20+100
       CORS is open so a locally-opened webapp/index.html can use it too. */
    server.on("/api/cmd", HTTP_GET, [](AsyncWebServerRequest *req) {
        String cmd = req->hasParam("c") ? req->getParam("c")->value() : "s";
        AsyncWebServerResponse *res =
            req->beginResponse(200, "text/plain", runForClient(cmd));
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    server.onNotFound([](AsyncWebServerRequest *req) { req->redirect("/"); });
}

/* -------------------------------------------------------------------- init */
void NetInterface::begin() {
    WifiSettings &w = Segments.config().wifi;
    if (!w.enabled) {
        Log.log("[net] radio disabled - serial only (`wifi ap` to enable)");
        return;
    }

    if (w.useSta && strlen(w.ssid)) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(w.ssid, w.pass);
        Log.log("[net] joining \"%s\" ...", w.ssid);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_CONNECT_TIMEOUT) delay(250);

        if (WiFi.status() == WL_CONNECTED) {
            Log.log("[net] connected, http://%s", WiFi.localIP().toString().c_str());
        } else {
            Log.log("[net] join failed - falling back to AP");
            w.useSta = false;
        }
    }

    if (!w.useSta || WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Log.log("[net] AP \"%s\" (%s), http://%s",
                AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
    }

    if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        Log.log("[net] also at http://%s.local", MDNS_HOST);
    }

    socket.onEvent(onWsEvent);
    server.addHandler(&socket);
    setupRoutes();
    server.begin();
    started = true;

    Log.setSink([](const char *l) { Net.broadcastLog(l); });
    Log.log("[net] ui served from flash (%u B gzipped)", (unsigned)WEBPAGE_GZ_LEN);
}

void NetInterface::restart() {
    WiFi.disconnect(true);
    started = false;
    begin();
}

void NetInterface::loop() {
    if (!started) return;
    socket.cleanupClients();

    /* push telemetry to every open UI, same payload the serial `stat` prints */
    uint32_t now = millis();
    if (socket.count() && now - lastStatus >= WS_STATUS_MS) {
        lastStatus = now;
        LineSink sink;
        Console.setOut(&sink);
        Console.emitStatus();
        Console.setOut(&Serial);
        socket.textAll(sink.buf);
    }
}

bool    NetInterface::connected() const { return started; }
uint8_t NetInterface::clients() const   { return started ? socket.count() : 0; }
String  NetInterface::modeName() const  { return WiFi.getMode() == WIFI_AP ? "AP" : "STA"; }
String  NetInterface::ip() const {
    if (!started) return "-";
    return (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
