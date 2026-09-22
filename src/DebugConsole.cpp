#include "DebugConsole.h"
#include "Config.h"
#include "SegmentManager.h"
#include "LedController.h"
#include "Logger.h"
#include <ArduinoJson.h>

DebugConsole Console;

/* ------------------------------------------------------------------ helpers */
static String tok(const String &s, int n, char sep = ' ') {
    int start = 0, idx = 0;
    while (idx <= n) {
        int sp = s.indexOf(sep, start);
        String part = (sp < 0) ? s.substring(start) : s.substring(start, sp);
        if (idx == n) { part.trim(); return part; }
        if (sp < 0) return "";
        start = sp + 1;
        idx++;
    }
    return "";
}

static RGB parseColor(const String &s, RGB fallback = RGB_WHITE) {
    String v = s;
    v.trim();
    v.replace("#", "");
    if (v.length() == 6) {
        uint32_t rgb = strtoul(v.c_str(), nullptr, 16);
        return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    if (v.indexOf(',') > 0)
        return RGB(tok(v, 0, ',').toInt(), tok(v, 1, ',').toInt(), tok(v, 2, ',').toInt());
    v.toUpperCase();
    if (v == "RED")   return RGB(255,0,0);
    if (v == "GREEN") return RGB(0,255,0);
    if (v == "BLUE")  return RGB(0,0,255);
    if (v == "WHITE") return RGB_WHITE;
    if (v == "OFF")   return RGB_BLACK;
    return fallback;
}

static String hex(const RGB &c) {
    char b[8];
    snprintf(b, sizeof(b), "#%02X%02X%02X", c.r, c.g, c.b);
    return String(b);
}

/* --------------------------------------------------------------------- menu */
void DebugConsole::begin() {
    printMenu();
}

void DebugConsole::printMenu() {
    Print *o = _out;
    o->println();
    o->println(F("=========== INNOV+IOT SIGN - DEBUG CONSOLE ==========="));
    o->println(F(" STATE     s) status      l) list segments   v) validate"));
    o->println(F(" POWER     on | off       bright <0-255>     speed <1-255>"));
    o->println(F(" LOOK      anim <name|id> intensity <0-255>  tint on|off"));
    o->println(F("           mode <parallel|stagger|sequence>  stagger <ms>"));
    o->println(F(" SEGMENTS  seg add <name> <start> <end>"));
    o->println(F("           seg range <i|name> <start> <end>"));
    o->println(F("           seg color <i|name> <#RRGGBB|red|r,g,b>"));
    o->println(F("           seg anim  <i|name> <name|id|inherit>"));
    o->println(F("           seg on|off|rev|del|id <i|name>"));
    o->println(F("           seg bright <i|name> <0-255>       seg sort"));
    o->println(F("           seg spread     divide the whole strip between letters"));
    o->println(F(" SECTIONS  seg div <i|name> <count>          seg divlist <i|name>"));
    o->println(F("           seg divrange <i|name> <k> <start> <end>   seg divauto <i|name>"));
    o->println(F(" MAPPING   test index <n>      test range <a> <b>"));
    o->println(F("           test walk [ms]      test all        test off"));
    o->println(F(" CONFIG    save | load | defaults | ledcount <n> | ma <n>"));
    o->println(F("           pin            show wiring / list usable GPIOs"));
    o->println(F("           pin <gpio>     move the strip's data line (live)"));
    o->println(F(" PROTOCOL  json (full state)   stat (telemetry)   hello"));
    o->println(F(" SYSTEM    h) help   m) menu   clear   reboot"));
    o->println(F("======================================================"));
}

/* --------------------------------------------------------------------- loop */
void DebugConsole::loop() {
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
            String line = _buf;
            _buf = "";
            line.trim();
            if (line.length()) {
                String out = execute(line);
                if (out.length()) Serial.println(out);
            }
            Serial.print(F("sign> "));
        } else if (_buf.length() < 160) {
            _buf += ch;
        }
    }
}

/* ------------------------------------------------------------------ command */
String DebugConsole::execute(const String &cmdline) {
    String line = cmdline;
    line.trim();
    if (!line.length()) return "";

    String cmd = tok(line, 0);
    cmd.toLowerCase();
    String rest = line.substring(cmd.length());
    rest.trim();

    GlobalSettings &g = Segments.globals();

    if (cmd == "h" || cmd == "help" || cmd == "m" || cmd == "menu") { printMenu(); return ""; }
    if (cmd == "s" || cmd == "status")   return cmdStatus();
    if (cmd == "l" || cmd == "list")     return cmdList();
    if (cmd == "clear")                  { Log.clear(); return "log cleared"; }
    if (cmd == "reboot")                 { Serial.println("rebooting..."); delay(200); ESP.restart(); }

    if (cmd == "on")  { g.power = true;  Segments.markDirty(); return "power ON"; }
    if (cmd == "off") { g.power = false; Segments.markDirty(); return "power OFF"; }

    if (cmd == "bright")    { g.brightness = constrain(rest.toInt(), 0, 255); Segments.markDirty(); return "brightness=" + String(g.brightness); }
    if (cmd == "speed")     { g.speed = constrain(rest.toInt(), 1, 255);      Segments.markDirty(); return "speed=" + String(g.speed); }
    if (cmd == "intensity") { g.intensity = constrain(rest.toInt(), 0, 255);  Segments.markDirty(); return "intensity=" + String(g.intensity); }
    if (cmd == "stagger")   { g.stagger = constrain(rest.toInt(), 0, 5000);   Segments.markDirty(); return "stagger=" + String(g.stagger) + "ms"; }
    if (cmd == "ma") {
        long v = rest.toInt();
        g.maxMilliamps = (v <= 0) ? 0 : (uint16_t)constrain(v, 100L, 60000L);
        Segments.markDirty();
        return g.maxMilliamps ? ("power budget=" + String(g.maxMilliamps) + "mA")
                              : String("power cap OFF - brightness is not limited "
                                       "(size the PSU for it)");
    }

    if (cmd == "tint") { g.rainbowTint = rest.startsWith("on"); Segments.markDirty(); return String("tint ") + (g.rainbowTint ? "on" : "off"); }

    if (cmd == "anim") {
        g.animation = animationIdFromName(rest);
        Segments.markDirty();
        return String("animation=") + animationName(g.animation);
    }

    if (cmd == "mode") {
        String m = rest; m.toLowerCase();
        if (m.startsWith("par")) g.playMode = PLAY_PARALLEL;
        else if (m.startsWith("seq")) g.playMode = PLAY_SEQUENCE;
        else g.playMode = PLAY_STAGGER;
        Segments.markDirty();
        return "playMode=" + String(g.playMode);
    }

    if (cmd == "ledcount") {
        g.ledCount = constrain(rest.toInt(), 1, MAX_LEDS);
        Segments.markDirty();
        Leds.restartStrip();
        return "ledCount=" + String(g.ledCount);
    }

    if (cmd == "json" || cmd == "state") { emitState();  return ""; }
    if (cmd == "stat")                   { emitStatus(); return ""; }
    if (cmd == "hello" || cmd == "id")   { emitHello();  return ""; }

    if (cmd == "pin")   return cmdPin(rest);

    if (cmd == "seg")   return cmdSeg(rest);
    if (cmd == "test")  return cmdTest(rest);

    if (cmd == "save")     return Segments.save() ? "config saved" : "SAVE FAILED";
    if (cmd == "load")     return Segments.load() ? "config loaded" : "nothing stored";
    if (cmd == "defaults") { Segments.loadDefaults(); Leds.restartStrip(); return "defaults loaded (not saved - type `save`)"; }
    if (cmd == "v" || cmd == "validate") {
        SegmentManager::Issue issues[12];
        uint8_t n = Segments.validate(issues, 12);
        if (!n) return "OK - no overlaps, all ranges inside 0.." + String(g.ledCount - 1);
        String out = String(n) + " issue(s):";
        for (uint8_t i = 0; i < n && i < 12; i++) {
            out += "\n  - seg " + String(issues[i].segA);
            if (issues[i].segB >= 0) out += " <-> seg " + String(issues[i].segB);
            out += ": " + String(issues[i].what);
        }
        return out;
    }

    return "unknown command: " + cmd + "   (type `h`)";
}

/* ---------------------------------------------------------------- sub-verbs */
static int resolveSeg(const String &ref) {
    if (!ref.length()) return -1;
    if (isDigit(ref[0])) {
        int i = ref.toInt();
        return Segments.get(i) ? i : -1;
    }
    return Segments.indexOfName(ref);
}

String DebugConsole::cmdSeg(const String &args) {
    String verb = tok(args, 0);
    verb.toLowerCase();

    if (verb == "sort") { Segments.sortByStart(); return "segments sorted by start index"; }

    if (verb == "spread") {
        Segments.spreadEvenly();
        GlobalSettings &g = Segments.globals();
        return "segments spread evenly across " + String(g.ledCount) + " LEDs (" +
               String(g.ledCount / max<uint8_t>(Segments.count(), 1)) + " each) - `l` to list";
    }

    if (verb == "add") {
        String name = tok(args, 1);
        int a = tok(args, 2).toInt(), b = tok(args, 3).toInt();
        int i = Segments.add(name, a, b);
        return (i < 0) ? "segment table full"
                       : "added [" + String(i) + "] " + name + " " + String(a) + "-" + String(b);
    }

    int idx = resolveSeg(tok(args, 1));
    Segment *s = Segments.get(idx);
    if (!s) return "usage: seg <verb> <index|name> ...   (`l` to list)";

    if (verb == "div") {
        uint8_t n = tok(args, 2).toInt();
        Segments.setDivisions(idx, n);
        return String(s->name) + " split into " + String(divisionCount(*s)) +
               " sections (+ all-glow step)";
    }
    if (verb == "divauto") {
        Segments.autoSplit(idx);
        return String(s->name) + " sections re-split evenly";
    }
    if (verb == "divrange") {
        uint8_t k = tok(args, 2).toInt();
        uint16_t a = tok(args, 3).toInt(), b = tok(args, 4).toInt();
        if (!Segments.setDivisionRange(idx, k, a, b))
            return "usage: seg divrange <i|name> <k> <start> <end>";
        uint16_t ra, rb;
        divisionBounds(*s, k, ra, rb);
        return String(s->name) + " section " + String(k) + " -> " +
               String(ra) + "-" + String(rb);
    }
    if (verb == "divlist") {
        uint8_t n = divisionCount(*s);
        String o = String(s->name) + ": " + String(n) + " sections" +
                   (s->customDiv ? " (custom)" : " (even split)") + " + all-glow";
        for (uint8_t k = 0; k < n; k++) {
            uint16_t a, b;
            if (!divisionBounds(*s, k, a, b)) continue;
            o += "\n   [" + String(k) + "] " + String(a) + "-" + String(b) +
                 "  (" + String(b - a + 1) + " leds)";
        }
        return o;
    }
    if (verb == "range") {
        uint16_t a = tok(args, 2).toInt(), b = tok(args, 3).toInt();
        Segments.setRange(idx, a, b);
        return String(s->name) + " -> " + String(s->start) + "-" + String(s->end) +
               " (" + String(s->length()) + " leds)";
    }
    if (verb == "color")  { s->color  = parseColor(tok(args, 2)); Segments.markDirty(); return String(s->name) + " color " + hex(s->color); }
    if (verb == "color2") { s->color2 = parseColor(tok(args, 2), RGB_BLACK); Segments.markDirty(); return String(s->name) + " color2 " + hex(s->color2); }
    if (verb == "anim")   { s->animation = animationIdFromName(tok(args, 2)); Segments.markDirty(); return String(s->name) + " anim " + animationName(s->animation); }
    if (verb == "bright") { s->brightness = constrain(tok(args, 2).toInt(), 0, 255); Segments.markDirty(); return String(s->name) + " bright " + String(s->brightness); }
    if (verb == "on")     { s->enabled = true;  Segments.markDirty(); return String(s->name) + " enabled"; }
    if (verb == "off")    { s->enabled = false; Segments.markDirty(); return String(s->name) + " disabled"; }
    if (verb == "rev")    { s->reversed = !s->reversed; Segments.markDirty(); return String(s->name) + " reversed=" + String(s->reversed); }
    if (verb == "id")     { Leds.identify(idx); return "blinking " + String(s->name); }
    if (verb == "del")    { String n = s->name; Segments.remove(idx); return "removed " + n; }
    if (verb == "name")   { String n = tok(args, 2); ::strncpy(s->name, n.c_str(), SEG_NAME_LEN - 1); s->name[SEG_NAME_LEN - 1] = 0; Segments.markDirty(); return "renamed to " + n; }

    return "seg verbs: add range color color2 anim bright on off rev id del name sort spread\n"
           "           div divrange divauto divlist";
}

String DebugConsole::cmdTest(const String &args) {
    String verb = tok(args, 0);
    verb.toLowerCase();

    if (verb == "off" || verb == "")   { Leds.testOff(); return "test overlay off"; }
    if (verb == "index") { uint16_t n = tok(args, 1).toInt(); Leds.testIndex(n, parseColor(tok(args, 2))); return "LED " + String(n) + " lit"; }
    if (verb == "range") {
        uint16_t a = tok(args, 1).toInt(), b = tok(args, 2).toInt();
        Leds.testRange(a, b, parseColor(tok(args, 3)));
        return "range " + String(a) + "-" + String(b) + " lit (" + String(b - a + 1) + " leds)";
    }
    if (verb == "div") {
        int i = resolveSeg(tok(args, 1));
        const Segment *sg = Segments.get(i);
        if (!sg) return "usage: test div <i|name> <k>";
        uint8_t k = tok(args, 2).toInt();
        uint16_t a, b;
        if (!divisionBounds(*sg, k, a, b)) return "no such section";
        Leds.testRange(a, b, parseColor(tok(args, 3)));
        return String(sg->name) + " section " + String(k) + ": " + String(a) + "-" + String(b);
    }
    if (verb == "all")  { Leds.testAll(parseColor(tok(args, 1))); return "all LEDs lit"; }
    if (verb == "walk") {
        uint16_t d = tok(args, 1).length() ? tok(args, 1).toInt() : 400;
        Leds.testWalk(d);
        return "walking one LED every " + String(d) + "ms (markers every 10) - `test off` to stop";
    }
    return "test verbs: index <n> | range <a> <b> | div <seg> <k> | walk [ms] | all | off";
}

String DebugConsole::cmdPin(const String &args) {
    GlobalSettings &g = Segments.globals();

    if (!args.length()) {
        String o = "\n--- LED DATA PIN ---------------------------------";
        o += "\n driving now : GPIO" + String(Leds.activePin());
        o += "\n configured  : GPIO" + String(g.dataPin);

        o += "\n usable      : 2 4 5 12 13 14 15 16 17 18 19 21 22 23 25 26 27 32 33";
        o += "\n avoid       : 1/3 (this console), 6-11 (flash), 34-39 (input only)";
        o += "\n\n set it with:  pin <gpio>     e.g.  pin 5";
        o += "\n--------------------------------------------------";
        return o;
    }

    int p = args.toInt();
    if (!isValidLedPin(p))
        return "GPIO" + String(p) + " can't drive the strip. Usable: "
               "2 4 5 12 13 14 15 16 17 18 19 21 22 23 25 26 27 32 33";

    g.dataPin = (uint8_t)p;
    Segments.save();
    Leds.restartStrip();            // NeoPixelBus rebuilds live - no reboot

    String o = "data pin now GPIO" + String(p) + " (saved, applied immediately)";
    const char *warn = ledPinWarning(p);
    if (warn) o += "\n  ! " + String(warn);
    return o;
}

/* ------------------------------------------------------------------ reports */
String DebugConsole::cmdStatus() {
    GlobalSettings &g = Segments.globals();
    String o = "\n--- STATUS ---------------------------------------";
    o += "\n power        : " + String(g.power ? "ON" : "OFF");
    o += "\n animation    : " + String(animationName(g.animation));
    o += "\n brightness   : " + String(g.brightness) + "/255";
    o += "\n speed        : " + String(g.speed) + "   intensity: " + String(g.intensity);
    o += "\n playMode     : " + String(g.playMode == PLAY_PARALLEL ? "parallel" :
                                       g.playMode == PLAY_STAGGER  ? "stagger"  : "sequence");
    o += "  stagger: " + String(g.stagger) + "ms";
    o += "\n leds         : " + String(g.ledCount) + " on GPIO" + String(Leds.activePin());
    o += "\n segments     : " + String(Segments.count()) + "/" + String(MAX_SEGMENTS);
    o += "\n fps          : " + String(Leds.fps(), 1) + "   frames: " + String(Leds.frames());
    o += "\n est. current : ~" + String(Leds.estimatedMilliamps()) + " mA (budget " +
         (g.maxMilliamps ? String(g.maxMilliamps) : String("off")) + ")";
    o += "\n test overlay : " + String(Leds.test().mode == TEST_NONE ? "none" : "ACTIVE");
    o += "\n free heap    : " + String(ESP.getFreeHeap()) + " B";
    o += "\n uptime       : " + String(millis() / 1000) + " s";
    o += "\n unsaved      : " + String(Segments.dirty() ? "yes (type `save`)" : "no");
    o += "\n--------------------------------------------------";
    return o;
}

String DebugConsole::cmdList() {
    String o = "\n idx name      range        len  en rev anim       bright  color";
    o += "\n ---------------------------------------------------------------------";
    for (uint8_t i = 0; i < Segments.count(); i++) {
        const Segment *s = Segments.get(i);
        char row[128];
        char range[16];
        snprintf(range, sizeof(range), "%u-%u", s->start, s->end);
        snprintf(row, sizeof(row), "\n %3u %-9s %-12s %4u  %s  %s  %-10s %3u    %s",
                 i, s->name, range, s->length(),
                 s->enabled ? "Y" : "n", s->reversed ? "Y" : "n",
                 animationName(s->animation), s->brightness, hex(s->color).c_str());
        o += row;
    }
    o += "\n ---------------------------------------------------------------------";
    return o;
}

/* ----------------------------------------------------------------- protocol */
static void emitLine(JsonDocument &d) {
    Print *o = Console.out();
    o->print(F("#J"));
    serializeJson(d, *o);
    o->println();
}

void DebugConsole::emitHello() {
    JsonDocument d;
    d["type"]     = "hello";
    d["device"]   = "INNOV+IOT SIGN";
    d["fw"]       = "1.0.0";
    d["protocol"] = 1;
    d["chip"]     = ESP.getChipModel();
    d["maxLeds"]  = MAX_LEDS;
    d["maxSegs"]  = MAX_SEGMENTS;
    emitLine(d);
}

void DebugConsole::emitState() {
    JsonDocument d;
    JsonObject root = d.to<JsonObject>();
    root["type"] = "state";
    Segments.toJson(root);
    emitLine(d);
}

void DebugConsole::emitStatus() {
    static const char *TEST_NAMES[] = {"none", "index", "range", "all", "identify", "walk"};
    const TestState &t = Leds.test();

    JsonDocument d;
    d["type"]      = "status";
    d["fps"]       = (int)(Leds.fps() + 0.5f);
    d["frames"]    = Leds.frames();
    d["ma"]        = Leds.estimatedMilliamps();
    d["heap"]      = ESP.getFreeHeap();
    d["uptime"]    = millis() / 1000;
    d["leds"]      = Segments.globals().ledCount;
    d["pin"]       = Leds.activePin();
    d["segments"]  = Segments.count();
    d["power"]     = Segments.globals().power;
    d["testMode"]  = TEST_NAMES[t.mode <= TEST_WALK ? t.mode : 0];
    d["testIndex"] = t.index;
    d["dirty"]     = Segments.dirty();
    emitLine(d);
}

void DebugConsole::emitAck(const String &cmd, const String &out) {
    JsonDocument d;
    d["type"] = "ack";
    d["cmd"]  = cmd;
    d["out"]  = out;
    emitLine(d);
}
