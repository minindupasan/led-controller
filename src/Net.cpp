#include "Net.h"
#include "Config.h"
#include "WebPage.h"
#include "Sign.h"
#include "DebugConsole.h"
#include "Logger.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>

NetInterface Net;

static AsyncWebServer server(80);
static AsyncWebSocket socket("/ws");
static bool     started    = false;
static uint32_t lastStatus = 0;

/* Minimal percent-encoding - commands are plain words and digits. */
static String urlEncode(const String &in) {
    String out;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') out += c;
        else if (c == ' ') out += '+';
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
            out += buf;
        }
    }
    return out;
}

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
#if ROLE_LAMP
    /* The lamp hosts the network: no router to depend on at an event, and
       the Mac bridge and the sign both join it. */
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    WiFi.setSleep(false);
    Log.log("[net] AP \"%s\" (%s) -> http://%s",
            AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
#else
    /* The sign is an ordinary client: a DHCP lease, then it tells the lamp
       where it landed. Claiming a fixed address would fight the AP's own
       DHCP, which starts handing out at .2. */
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(AP_SSID, AP_PASSWORD);
    Log.log("[net] joining \"%s\" ...", AP_SSID);
#endif

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

/*
 * Lamp -> sign. One short GET, with a tight timeout: the sign might be off,
 * still booting or out of range, and none of that may hold up the lamp while
 * guests are tapping.
 */
bool NetInterface::sendToSign(const String &command) {
#if ROLE_LAMP
    if (!_signAddr.length()) {
        Log.log("[net] no sign has announced itself yet - is ESP #1 powered "
                "and joined to this AP?");
        return false;
    }

    HTTPClient http;
    String url = String("http://") + _signAddr + "/api/cmd?c=" + urlEncode(command);

    http.setConnectTimeout(800);
    http.setTimeout(1200);
    if (!http.begin(url)) {
        Log.log("[net] sign trigger: bad url");
        return false;
    }

    int code = http.GET();
    http.end();

    _signSeen = (code == 200);
    if (_signSeen) Log.log("[net] sign told: %s", command.c_str());
    else           Log.log("[net] sign at %s did not answer (%d)",
                           _signAddr.c_str(), code);
    return _signSeen;
#else
    (void)command;
    return false;
#endif
}

void NetInterface::restart() {
    WiFi.softAPdisconnect(true);
    started = false;
    begin();
}

void NetInterface::loop() {
    if (!started) return;
    socket.cleanupClients();

#if ROLE_SIGN
    /* Keep trying to join the lamp's AP so the boards can be powered up in
       any order, and so the sign recovers if the lamp is restarted. */
    static uint32_t lastTry = 0;
    static bool wasUp = false;
    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - lastTry > STA_RETRY_MS) {
            lastTry = millis();
            WiFi.begin(AP_SSID, AP_PASSWORD);
        }
        wasUp = false;
    } else if (!wasUp) {
        wasUp = true;
        Log.log("[net] joined %s as %s", AP_SSID, WiFi.localIP().toString().c_str());
        announceToLamp();
    }

    /* Re-announce periodically: the lamp may have rebooted and forgotten us,
       and a lease renewal can change our address. */
    static uint32_t lastAnnounce = 0;
    if (WiFi.status() == WL_CONNECTED && millis() - lastAnnounce > SIGN_ANNOUNCE_MS) {
        lastAnnounce = millis();
        announceToLamp();
    }
#endif

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

/* Sign -> lamp: "I am at this address." The lamp stores it and uses it for
   the trigger, so neither board needs a fixed IP. */
void NetInterface::announceToLamp() {
#if ROLE_SIGN
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    String url = String("http://") + LAMP_IP + "/api/cmd?c=signip+" +
                 WiFi.localIP().toString();
    http.setConnectTimeout(700);
    http.setTimeout(1000);
    if (!http.begin(url)) return;
    int code = http.GET();
    http.end();
    if (code == 200) _signSeen = true;      // the lamp is up and heard us
#endif
}

void NetInterface::setSignAddr(const String &ip) {
    _signAddr = ip;
    _signSeen = ip.length() > 0;
}

bool    NetInterface::connected() const { return started; }
uint8_t NetInterface::clients() const   { return started ? socket.count() : 0; }
String  NetInterface::modeName() const  {
#if ROLE_LAMP
    return "AP";
#else
    return WiFi.status() == WL_CONNECTED ? "STA" : "joining";
#endif
}
String  NetInterface::ip() const {
    if (!started) return "-";
    return (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
