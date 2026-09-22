#include "DebugConsole.h"
#include "Config.h"
#include "Sign.h"
#include "LedController.h"
#include "Show.h"
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
    if (!v.length()) return fallback;

    bool named = false;
    RGB pal = paletteColor(v, &named);
    if (named) return pal;

    v.replace("#", "");
    if (v.length() == 6) {
        uint32_t rgb = strtoul(v.c_str(), nullptr, 16);
        return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    if (v.indexOf(',') > 0)
        return RGB(tok(v, 0, ',').toInt(), tok(v, 1, ',').toInt(), tok(v, 2, ',').toInt());
    return fallback;
}

static String hex(const RGB &c) {
    char b[8];
    snprintf(b, sizeof(b), "#%02X%02X%02X", c.r, c.g, c.b);
    return String(b);
}

/* --------------------------------------------------------------------- menu */
void DebugConsole::begin() { printMenu(); }

void DebugConsole::printMenu() {
    Print *o = _out;
    o->println();
    o->println(F("======= INNOV IOT LED CONSOLE ======="));
    o->println(F(" STATE    s) status    l) list letters    v) validate"));
    o->println(F(" POWER    on | off | bright <0-255> | speed <1-255>"));
    o->println(F(" WORDS    word color <INNOV|IOT|all> <purple|cyan|amber|#hex>"));
    o->println(F("          word anim  <INNOV|IOT|all> <OFF|SOLID|BREATHE|"));
    o->println(F("                                      TRAVERSE|AURORA|COMET>"));
    o->println(F(" SHOW     show start | show stop | show auto on|off"));
    o->println(F(" LETTERS  seg range <i|name> <start> <end>"));
    o->println(F("          seg on|off|rev|id <i|name>"));
    o->println(F(" MAPPING  test index <n> | test range <a> <b>"));
    o->println(F("          test walk [ms] | test all | test off"));
    o->println(F(" CONFIG   save | load | defaults | ledcount <n> | ma <n>"));
    o->println(F("          pin | pin <gpio>"));
    o->println(F(" PROTOCOL json | stat | hello        SYSTEM  h | clear | reboot"));
    o->println(F("====================================="));
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

    Settings &st = TheSign.settings();

    if (cmd == "h" || cmd == "help" || cmd == "m" || cmd == "menu") { printMenu(); return ""; }
    if (cmd == "s" || cmd == "status") return cmdStatus();
    if (cmd == "l" || cmd == "list")   return cmdList();
    if (cmd == "clear")  { Log.clear(); return "log cleared"; }
    if (cmd == "reboot") { Serial.println("rebooting..."); delay(200); ESP.restart(); }

    if (cmd == "on")  { st.power = true;  TheSign.markDirty(); return "power ON"; }
    if (cmd == "off") { st.power = false; TheSign.markDirty(); return "power OFF"; }

    if (cmd == "bright") { st.brightness = constrain(rest.toInt(), 0, 255); TheSign.markDirty(); return "brightness=" + String(st.brightness); }
    if (cmd == "speed")  { st.speed = constrain(rest.toInt(), 1, 255); TheSign.markDirty(); return "speed=" + String(st.speed); }

    if (cmd == "ma") {
        long v = rest.toInt();
        st.maxMilliamps = (v <= 0) ? 0 : (uint16_t)constrain(v, 100L, 60000L);
        TheSign.markDirty();
        return st.maxMilliamps ? ("power budget=" + String(st.maxMilliamps) + "mA")
                               : String("power cap OFF - brightness is not limited");
    }

    if (cmd == "ledcount") {
        st.ledCount = constrain(rest.toInt(), 1, MAX_LEDS);
        TheSign.markDirty();
        Leds.restartStrip();
        return "ledCount=" + String(st.ledCount);
    }

    if (cmd == "word") return cmdWord(rest);
    if (cmd == "seg")  return cmdLetter(rest);
    if (cmd == "test") return cmdTest(rest);
    if (cmd == "show") return cmdShow(rest);
    if (cmd == "pin")  return cmdPin(rest);

    if (cmd == "json" || cmd == "state") { emitState();  return ""; }
    if (cmd == "stat")                   { emitStatus(); return ""; }
    if (cmd == "hello" || cmd == "id")   { emitHello();  return ""; }

    if (cmd == "save")     return TheSign.save() ? "config saved" : "SAVE FAILED";
    if (cmd == "load")     return TheSign.load() ? "config loaded" : "nothing stored";
    if (cmd == "defaults") { TheSign.loadDefaults(); Leds.restartStrip(); return "defaults loaded (type `save` to keep)"; }

    if (cmd == "v" || cmd == "validate") {
        Sign::Issue issues[12];
        uint8_t n = TheSign.validate(issues, 12);
        if (!n) return "OK - no overlaps, all ranges inside 0.." + String(st.ledCount - 1);
        String o = String(n) + " issue(s):";
        for (uint8_t i = 0; i < n && i < 12; i++) {
            o += "\n  - letter " + String(issues[i].a);
            if (issues[i].b >= 0) o += " <-> " + String(issues[i].b);
            o += ": " + String(issues[i].what);
        }
        return o;
    }

    return "unknown command: " + cmd + "   (type `h`)";
}

/* ------------------------------------------------------------------- words */
String DebugConsole::cmdWord(const String &args) {
    String verb = tok(args, 0);
    verb.toLowerCase();
    String target = tok(args, 1);
    String value  = tok(args, 2);

    bool all = target.equalsIgnoreCase("all");
    int wi = all ? 0 : TheSign.wordByName(target);
    if (!all && wi < 0 && target.length() && isDigit(target[0])) wi = target.toInt();
    if (!all && !TheSign.word(wi)) return "usage: word <color|anim> <INNOV|IOT|all> <value>";

    uint8_t from = all ? 0 : wi;
    uint8_t to   = all ? TheSign.wordCount() - 1 : wi;

    if (verb == "color") {
        RGB c = parseColor(value);
        for (uint8_t i = from; i <= to; i++) TheSign.word(i)->color = c;
        TheSign.markDirty();
        TheShow.stop();                       // a manual change takes control
        return String(all ? "all words" : TheSign.word(wi)->name) + " colour " + hex(c);
    }
    if (verb == "anim") {
        uint8_t a = animationFromName(value);
        for (uint8_t i = from; i <= to; i++) TheSign.word(i)->animation = a;
        TheSign.markDirty();
        TheShow.stop();
        return String(all ? "all words" : TheSign.word(wi)->name) + " -> " + animationName(a);
    }
    return "word verbs: color <value> | anim <name>";
}

/* ----------------------------------------------------------------- letters */
static int resolveLetter(const String &ref) {
    if (!ref.length()) return -1;
    if (isDigit(ref[0])) {
        int i = ref.toInt();
        return TheSign.letter(i) ? i : -1;
    }
    return TheSign.letterByName(ref);
}

String DebugConsole::cmdLetter(const String &args) {
    String verb = tok(args, 0);
    verb.toLowerCase();
    int idx = resolveLetter(tok(args, 1));
    Letter *l = TheSign.letter(idx);
    if (!l) return "usage: seg <range|on|off|rev|id|name> <index|name> ...  (`l` to list)";

    if (verb == "range") {
        TheSign.setRange(idx, tok(args, 2).toInt(), tok(args, 3).toInt());
        return String(l->name) + " -> " + String(l->start) + "-" + String(l->end) +
               " (" + String(l->length()) + " px)";
    }
    if (verb == "on")   { l->enabled = true;  TheSign.markDirty(); return String(l->name) + " enabled"; }
    if (verb == "off")  { l->enabled = false; TheSign.markDirty(); return String(l->name) + " disabled"; }
    if (verb == "rev")  { l->reversed = !l->reversed; TheSign.markDirty(); return String(l->name) + " reversed=" + String(l->reversed); }
    if (verb == "id")   { Leds.identify(idx); return "blinking " + String(l->name); }
    if (verb == "name") {
        String n = tok(args, 2);
        ::strncpy(l->name, n.c_str(), NAME_LEN - 1);
        l->name[NAME_LEN - 1] = 0;
        TheSign.markDirty();
        return "renamed to " + n;
    }
    return "seg verbs: range on off rev id name";
}

/* -------------------------------------------------------------------- show */
String DebugConsole::cmdShow(const String &args) {
    String verb = tok(args, 0);
    verb.toLowerCase();

    if (verb == "start") { TheShow.start(); return "show started - wait, opener, then calm"; }
    if (verb == "stop")  { TheShow.stop();  return "show stopped - words under manual control"; }
    if (verb == "auto") {
        String v = tok(args, 1);
        TheSign.settings().autoShow = v.startsWith("on");
        TheSign.save();
        return String("auto-run on boot ") + (TheSign.settings().autoShow ? "on" : "off");
    }
    return String("show stage: ") + TheShow.stageName() + "   (show start | stop | auto on|off)";
}

/* -------------------------------------------------------------------- test */
String DebugConsole::cmdTest(const String &args) {
    String verb = tok(args, 0);
    verb.toLowerCase();

    if (verb == "off" || verb == "") { Leds.testOff(); return "test overlay off"; }
    if (verb == "index") {
        uint16_t n = tok(args, 1).toInt();
        Leds.testIndex(n, parseColor(tok(args, 2)));
        return "LED " + String(n) + " lit";
    }
    if (verb == "range") {
        uint16_t a = tok(args, 1).toInt(), b = tok(args, 2).toInt();
        Leds.testRange(a, b, parseColor(tok(args, 3)));
        return "range " + String(a) + "-" + String(b) + " lit (" + String(b - a + 1) + " px)";
    }
    if (verb == "all")  { Leds.testAll(parseColor(tok(args, 1))); return "all LEDs lit"; }
    if (verb == "walk") {
        uint16_t d = tok(args, 1).length() ? tok(args, 1).toInt() : 400;
        Leds.testWalk(d);
        return "walking one LED every " + String(d) + "ms (marks every 10) - `test off` to stop";
    }
    return "test verbs: index <n> | range <a> <b> | walk [ms] | all | off";
}

/* --------------------------------------------------------------------- pin */
String DebugConsole::cmdPin(const String &args) {
    Settings &st = TheSign.settings();

    if (!args.length()) {
        String o = "\n--- LED DATA PIN ---------------------------------";
        o += "\n driving now : GPIO" + String(Leds.activePin());
        o += "\n configured  : GPIO" + String(st.dataPin);
        o += "\n usable      : 2 4 5 12 13 14 15 16 17 18 19 21 22 23 25 26 27 32 33";
        o += "\n avoid       : 1/3 (this console), 6-11 (flash), 34-39 (input only)";
        o += "\n--------------------------------------------------";
        return o;
    }

    int p = args.toInt();
    if (!isValidLedPin(p)) return "GPIO" + String(p) + " can't drive the strip";

    st.dataPin = (uint8_t)p;
    TheSign.save();
    Leds.restartStrip();

    String o = "data pin now GPIO" + String(p) + " (saved, applied immediately)";
    const char *warn = ledPinWarning(p);
    if (warn) o += "\n  ! " + String(warn);
    return o;
}

/* ----------------------------------------------------------------- reports */
String DebugConsole::cmdStatus() {
    Settings &st = TheSign.settings();
    String o = "\n--- STATUS ---------------------------------------";
    o += "\n power        : " + String(st.power ? "ON" : "OFF");
    o += "\n brightness   : " + String(st.brightness) + "/255   speed: " + String(st.speed);
    o += "\n show         : " + String(TheShow.stageName()) +
         (st.autoShow ? "  (auto on boot)" : "");
    for (uint8_t i = 0; i < TheSign.wordCount(); i++) {
        Word *w = TheSign.word(i);
        o += "\n " + String(w->name) + String(9 - min<int>(8, strlen(w->name)), ' ') + ": " +
             animationName(w->animation) + "  " + hex(w->color) +
             "  (" + String(TheSign.wordLength(i)) + " px)";
    }
    o += "\n leds         : " + String(st.ledCount) + " on GPIO" + String(Leds.activePin());
    o += "\n letters      : " + String(TheSign.letterCount()) + "/" + String(MAX_LETTERS);
    o += "\n fps          : " + String(Leds.fps(), 1) + "   frames: " + String(Leds.frames());
    o += "\n est. current : ~" + String(Leds.estimatedMilliamps()) + " mA (budget " +
         (st.maxMilliamps ? String(st.maxMilliamps) : String("off")) + ")";
    o += "\n test overlay : " + String(Leds.test().mode == TEST_NONE ? "none" : "ACTIVE");
    o += "\n free heap    : " + String(ESP.getFreeHeap()) + " B";
    o += "\n uptime       : " + String(millis() / 1000) + " s";
    o += "\n unsaved      : " + String(TheSign.dirty() ? "yes (type `save`)" : "no");
    o += "\n--------------------------------------------------";
    return o;
}

String DebugConsole::cmdList() {
    String o = "\n idx name   range        len  en rev  word";
    o += "\n ------------------------------------------------";
    for (uint8_t i = 0; i < TheSign.letterCount(); i++) {
        const Letter *l = TheSign.letter(i);
        int owner = -1;
        for (uint8_t w = 0; w < TheSign.wordCount(); w++)
            if (i >= TheSign.word(w)->first && i < TheSign.word(w)->first + TheSign.word(w)->count)
                owner = w;

        char range[16], row[96];
        snprintf(range, sizeof(range), "%u-%u", l->start, l->end);
        snprintf(row, sizeof(row), "\n %3u %-6s %-12s %4u  %s  %s   %s",
                 i, l->name, range, l->length(),
                 l->enabled ? "Y" : "n", l->reversed ? "Y" : "n",
                 owner >= 0 ? TheSign.word(owner)->name : "-");
        o += row;
    }
    o += "\n ------------------------------------------------";
    return o;
}

/* ---------------------------------------------------------------- protocol */
static void emitLine(JsonDocument &d) {
    Print *o = Console.out();
    o->print(F("#J"));
    serializeJson(d, *o);
    o->println();
}

void DebugConsole::emitHello() {
    JsonDocument d;
    d["type"]     = "hello";
    d["device"]   = "INNOV IOT LED CONSOLE";
    d["fw"]       = "2.0.0";
    d["protocol"] = 2;
    d["chip"]     = ESP.getChipModel();
    d["maxLeds"]  = MAX_LEDS;
    emitLine(d);
}

void DebugConsole::emitState() {
    JsonDocument d;
    JsonObject root = d.to<JsonObject>();
    root["type"] = "state";
    TheSign.toJson(root);
    root["show"] = TheShow.stageName();
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
    d["leds"]      = TheSign.settings().ledCount;
    d["pin"]       = Leds.activePin();
    d["power"]     = TheSign.settings().power;
    d["show"]      = TheShow.stageName();
    d["testMode"]  = TEST_NAMES[t.mode <= TEST_WALK ? t.mode : 0];
    d["testIndex"] = t.index;
    d["dirty"]     = TheSign.dirty();
    emitLine(d);
}
