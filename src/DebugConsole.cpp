#include "DebugConsole.h"
#include "Config.h"
#include "Sign.h"
#if ROLE_SIGN
  #include "LedController.h"
  #include "Show.h"
#else
  #include "Lamp.h"
#endif
#include "Logger.h"
#include "Net.h"
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

/* Everything after token n, kept intact - guest names have spaces in them. */
static String restAfter(const String &s, int n) {
    int start = 0;
    for (int i = 0; i <= n; i++) {
        int sp = s.indexOf(' ', start);
        if (sp < 0) return "";
        start = sp + 1;
        while (start < (int)s.length() && s[start] == ' ') start++;
    }
    String r = s.substring(start);
    r.trim();
    return r;
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
    o->println(F(" LOOK     anim <OFF|SOLID|BREATHE|TRAVERSE|AURORA|COMET|BOUNCE>"));
    o->println(F("          word color <INNOV|IOT|all> <purple|cyan|amber|#hex>"));
    o->println(F(" SHOW     show start | show stop | show auto on|off"));
    o->println(F(" LOGO     logo level <n> | logo up | logo down | logo all | logo off"));
    o->println(F("          logo list | logo range <i> <a> <b> | logo id <i>"));
    o->println(F("          logo segs <n> | logo spread | logo color <c> | logo glow on|off"));
    o->println(F("          logo pin <gpio> | logo leds <n> | logo fade <ms>"));
    o->println(F("          logo card <uid> | logo learn <i> | logo assign <i> <uid>"));
    o->println(F("          logo add <a> <b> <name> | logo del <i> | logo reset"));
    o->println(F("          logo name <i> <name> | logo priority <i> <n> | logo sort"));
    o->println(F("          logo test index|range|walk|off ..."));
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

#if ROLE_SIGN
#if ROLE_SIGN
    if (cmd == "ledcount") {
        st.ledCount = constrain(rest.toInt(), 1, MAX_LEDS);
        TheSign.markDirty();
        Leds.restartStrip();
        return "ledCount=" + String(st.ledCount);
    }

    if (cmd == "anim") {
        st.animation = animationFromName(rest);
        TheSign.markDirty();
        TheShow.stop();                       // a manual change takes control
        return String("animation = ") + animationName(st.animation) + " (whole sign)";
    }
#endif
#endif

    if (cmd == "word") return cmdWord(rest);
    if (cmd == "seg")  return cmdLetter(rest);
    if (cmd == "test") return cmdTest(rest);
    if (cmd == "show") return cmdShow(rest);
    if (cmd == "logo") return cmdLogo(rest);
    if (cmd == "pin")  return cmdPin(rest);

#if ROLE_LAMP
    /* The sign announces itself here after it joins, so the lamp never needs
       a hardcoded address for it. */
    if (cmd == "signip") {
        Net.setSignAddr(rest);
        return "sign is at " + rest;
    }
#endif

#if ROLE_LAMP
    /* The sign announces itself here after it joins, so the lamp never needs
       a hardcoded address - and nothing collides with the AP's own DHCP. */
    if (cmd == "signip") {
        Net.setSignAddr(rest);
        return "sign is at " + rest;
    }
#endif

    if (cmd == "json" || cmd == "state") { emitState();  return ""; }
    if (cmd == "stat")                   { emitStatus(); return ""; }
    if (cmd == "hello" || cmd == "id")   { emitHello();  return ""; }

    if (cmd == "save")     return TheSign.save() ? "config saved" : "SAVE FAILED";
    if (cmd == "load")     return TheSign.load() ? "config loaded" : "nothing stored";
    if (cmd == "defaults") {
        TheSign.loadDefaults();
#if ROLE_SIGN
        Leds.restartStrip();
#else
        Logo.restartStrip();
#endif
        return "defaults loaded (type `save` to keep)";
    }

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
#if ROLE_SIGN
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
        /* kept so old muscle memory and the UI's shortcuts still work - the
           animation is global now, so this sets it for the whole sign */
        TheSign.settings().animation = animationFromName(value);
        TheSign.markDirty();
        TheShow.stop();
        return String("animation = ") + animationName(TheSign.settings().animation) +
               " (whole sign - animations are not per word)";
    }
    return "word verbs: color <value>     (animation is global: `anim <name>`)";
}
#else
String DebugConsole::cmdWord(const String &args) { return "words live on the sign (ESP #1)"; }
#endif

/* ----------------------------------------------------------------- letters */
static int resolveLetter(const String &ref) {
    if (!ref.length()) return -1;
    if (isDigit(ref[0])) {
        int i = ref.toInt();
        return TheSign.letter(i) ? i : -1;
    }
    return TheSign.letterByName(ref);
}

#if ROLE_SIGN
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
#else
String DebugConsole::cmdLetter(const String &args) { return "letters live on the sign (ESP #1)"; }
#endif

/* -------------------------------------------------------------------- show */
#if ROLE_SIGN
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
#else
String DebugConsole::cmdShow(const String &args) { return "this board is the lamp - the show runs on ESP #1"; }
#endif

/* -------------------------------------------------------------------- logo */
#if ROLE_LAMP
String DebugConsole::cmdLogo(const String &args) {
    String verb = tok(args, 0);
    verb.toLowerCase();
    Settings &st = TheSign.settings();

    /* --- the hot path: an Arduino raising the level as the lamp fills --- */
    if (verb == "level") {
        Logo.setLevel(tok(args, 1).toInt());
        return "logo level " + String(Logo.level()) + "/" + String(TheSign.logoSegCount()) +
               (Logo.full() ? "  (full)" : "");
    }
    if (verb == "up")   { Logo.step(+1); return "logo level " + String(Logo.level()); }
    if (verb == "down") { Logo.step(-1); return "logo level " + String(Logo.level()); }
    if (verb == "all")  { Logo.allOn();  return "logo full"; }
    if (verb == "off")  { Logo.allOff(); return "logo off"; }
    if (verb == "glow") { Logo.setGlow(tok(args, 1).startsWith("on")); return String("logo glow ") + (Logo.glow() ? "on" : "off"); }

    /* --- RFID ----------------------------------------------------------- *
     * `logo card <UID>` is what the laptop bridge calls on every tap.       */
    if (verb == "card") {
        String uid = tok(args, 1);
        int r = Logo.tapCard(uid);
        if (r == -2) return "card " + uid + " learned";
        if (r < 0)   return "unknown card " + uid + "   (use `logo learn <i>` first)";
        LogoSegment *sg = TheSign.logoSeg(r);
        return "card " + uid + " -> " + String(sg->name) + "  (" +
               String(Logo.level()) + "/" + String(TheSign.logoSegCount()) + ")" +
               (Logo.full() ? "  LAMP FULL" : "");
    }
    if (verb == "learn") {
        int i = tok(args, 1).toInt();
        if (!TheSign.logoSeg(i)) return "usage: logo learn <segment>";
        Logo.learn(i);
        return "tap a card now - it will be assigned to " +
               String(TheSign.logoSeg(i)->name);
    }
    if (verb == "assign") {
        int i = tok(args, 1).toInt();
        if (!TheSign.assignCard(i, tok(args, 2))) return "no such segment";
        TheSign.save();
        return String(TheSign.logoSeg(i)->name) + " card = " + String(TheSign.logoSeg(i)->card);
    }
    if (verb == "unassign") {
        int i = tok(args, 1).toInt();
        if (!TheSign.assignCard(i, "")) return "no such segment";
        TheSign.save();
        return "card cleared";
    }
    if (verb == "reset") { Logo.allOff(); return "lamp reset - nothing lit"; }
    if (verb == "name") {
        int i = tok(args, 1).toInt();
        String n = restAfter(args, 1);          // the whole name, spaces and all
        if (!TheSign.setLogoName(i, n)) return "usage: logo name <i> <name>";
        return "[" + String(i) + "] renamed to " + n;
    }
    if (verb == "priority" || verb == "pri") {
        int i = tok(args, 1).toInt();
        if (!TheSign.setLogoPriority(i, tok(args, 2).toInt()))
            return "usage: logo priority <i> <n>";
        TheSign.sortLogoByPriority();
        return "priorities updated - list re-sorted";
    }
    if (verb == "sort") { TheSign.sortLogoByPriority(); return "logo sorted by priority"; }
    if (verb == "autotrigger") {
        st.logoAutoTrigger = tok(args, 1).startsWith("on");
        TheSign.save();
        return String("full lamp starts the sign: ") + (st.logoAutoTrigger ? "on" : "off");
    }

    /* --- segments ------------------------------------------------------- */
    if (verb == "add") {
        /* logo add <start> <end> <name...> - name last so it can have spaces */
        int i = TheSign.addLogoSeg(restAfter(args, 2), tok(args, 1).toInt(), tok(args, 2).toInt());
        if (i < 0) return "logo segment table full";
        return "added [" + String(i) + "] " + String(TheSign.logoSeg(i)->name);
    }
    if (verb == "del") {
        int i = tok(args, 1).toInt();
        if (!TheSign.removeLogoSeg(i)) return "no such segment";
        return "removed logo segment " + String(i);
    }

    /* --- mapping -------------------------------------------------------- */
    if (verb == "list") {
        String o = "\n logo: " + String(TheSign.logoSegCount()) + " segments, " +
                   String(Logo.ledCount()) + " LEDs on GPIO" + String(Logo.activePin()) +
                   ", level " + String(Logo.level());
        o += "\n idx pri name                          range        card        lit";
        o += "\n -------------------------------------------------------------------";
        for (uint8_t i = 0; i < TheSign.logoSegCount(); i++) {
            const LogoSegment *l = TheSign.logoSeg(i);
            char range[16], row[128];
            snprintf(range, sizeof(range), "%u-%u", l->start, l->end);
            snprintf(row, sizeof(row), "\n %3u %3u %-29s %-12s %-11s %s",
                     i, l->priority, l->name, range,
                     l->card[0] ? l->card : "-", Logo.isLit(i) ? "LIT" : "");
            o += row;
        }
        return o;
    }
    if (verb == "range") {
        int i = tok(args, 1).toInt();
        if (!TheSign.setLogoRange(i, tok(args, 2).toInt(), tok(args, 3).toInt()))
            return "usage: logo range <i> <start> <end>";
        LogoSegment *l = TheSign.logoSeg(i);
        return String(l->name) + " -> " + String(l->start) + "-" + String(l->end) +
               " (" + String(l->length()) + " px)";
    }
    if (verb == "segs") {
        TheSign.setLogoSegCount(tok(args, 1).toInt());
        return String(TheSign.logoSegCount()) + " logo segments, split evenly";
    }
    if (verb == "spread") { TheSign.spreadLogoEvenly(); return "logo segments spread evenly"; }
    if (verb == "id")     { Logo.identify(tok(args, 1).toInt()); return "blinking logo segment " + tok(args, 1); }
    if (verb == "on")     { LogoSegment *l = TheSign.logoSeg(tok(args,1).toInt()); if(!l) return "no such segment"; l->enabled = true;  TheSign.markDirty(); return String(l->name) + " enabled"; }
    if (verb == "seg-off"){ LogoSegment *l = TheSign.logoSeg(tok(args,1).toInt()); if(!l) return "no such segment"; l->enabled = false; TheSign.markDirty(); return String(l->name) + " disabled"; }
    if (verb == "rev")    { LogoSegment *l = TheSign.logoSeg(tok(args,1).toInt()); if(!l) return "no such segment"; l->reversed = !l->reversed; TheSign.markDirty(); return String(l->name) + " reversed"; }

    /* --- setup ---------------------------------------------------------- */
    if (verb == "color") { st.logoColor = parseColor(tok(args, 1), st.logoColor); TheSign.markDirty(); return "logo colour " + hex(st.logoColor); }
    if (verb == "fade")  { st.logoFadeMs10 = constrain(tok(args, 1).toInt() / 10, 4, 255); TheSign.markDirty(); return "logo fade " + String(st.logoFadeMs10 * 10) + "ms per level"; }
    if (verb == "leds")  { st.logoLedCount = constrain(tok(args, 1).toInt(), 1, MAX_LOGO_LEDS); TheSign.spreadLogoEvenly(); Logo.restartStrip(); return "logo ledCount=" + String(st.logoLedCount); }
    if (verb == "pin") {
        int p = tok(args, 1).toInt();
        if (!isValidLedPin(p)) return "GPIO" + String(p) + " can't drive the strip";
        if (p == st.dataPin)   return "GPIO" + String(p) + " is already the letters' pin";
        st.logoPin = p;
        TheSign.save();
        Logo.restartStrip();
        return "logo pin now GPIO" + String(p);
    }

    /* --- test overlay on the logo strip --------------------------------- */
    if (verb == "test") {
        String v = tok(args, 1);
        v.toLowerCase();
        if (v == "off" || v == "") { Logo.testOff(); return "logo test off"; }
        if (v == "index") { uint16_t n = tok(args, 2).toInt(); Logo.testIndex(n, parseColor(tok(args, 3))); return "logo LED " + String(n) + " lit"; }
        if (v == "range") { uint16_t a = tok(args, 2).toInt(), b = tok(args, 3).toInt(); Logo.testRange(a, b, parseColor(tok(args, 4))); return "logo range " + String(a) + "-" + String(b); }
        if (v == "walk")  { uint16_t d = tok(args, 2).length() ? tok(args, 2).toInt() : 400; Logo.testWalk(d); return "logo walking every " + String(d) + "ms"; }
        return "logo test: index <n> | range <a> <b> | walk [ms] | off";
    }

    return String("logo level ") + Logo.level() + "/" + TheSign.logoSegCount() +
           "   verbs: level up down all off glow list range segs spread id rev\n"
           "                  color fade leds pin test\n"
           "                  card <uid> | learn <i> | assign <i> <uid> | unassign <i>\n"
           "                  add <a> <b> <name> | del <i> | name <i> <name>\n"
           "                  priority <i> <n> | sort | reset | autotrigger on|off";
}
#else
String DebugConsole::cmdLogo(const String &args) { return "this board is the sign - the lamp is ESP #2"; }
#endif

/* -------------------------------------------------------------------- test */
#if ROLE_SIGN
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
#else
String DebugConsole::cmdTest(const String &args) { return "use `logo test ...` on the lamp"; }
#endif

/* --------------------------------------------------------------------- pin */
#if ROLE_SIGN
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
#else
String DebugConsole::cmdPin(const String &args) { return "use `logo pin <gpio>` on the lamp"; }
#endif

/* ----------------------------------------------------------------- reports */
String DebugConsole::cmdStatus() {
    Settings &st = TheSign.settings();
    String o = "\n--- STATUS ---------------------------------------";
    o += "\n power        : " + String(st.power ? "ON" : "OFF");
    o += "\n brightness   : " + String(st.brightness) + "/255   speed: " + String(st.speed);
#if ROLE_SIGN
    o += "\n animation    : " + String(animationName(st.animation)) + "  (whole sign)";
#endif
#if ROLE_SIGN
    o += "\n show         : " + String(TheShow.stageName()) +
         (st.autoShow ? "  (auto on boot)" : "");
#endif
#if ROLE_SIGN
    for (uint8_t i = 0; i < TheSign.wordCount(); i++) {
        Word *w = TheSign.word(i);
        char row[72];
        snprintf(row, sizeof(row), "\n %-12s: %-9s (%u px)",
                 w->name, hex(w->color).c_str(), TheSign.wordLength(i));
        o += row;
    }
#endif
#if ROLE_SIGN
    o += "\n leds         : " + String(st.ledCount) + " on GPIO" + String(Leds.activePin());
    o += "\n letters      : " + String(TheSign.letterCount()) + "/" + String(MAX_LETTERS);
    o += "\n fps          : " + String(Leds.fps(), 1) + "   frames: " + String(Leds.frames());
    o += "\n est. current : ~" + String(Leds.estimatedMilliamps()) + " mA (budget " +
         (st.maxMilliamps ? String(st.maxMilliamps) : String("off")) + ")";
    o += "\n test overlay : " + String(Leds.test().mode == TEST_NONE ? "none" : "ACTIVE");
#else
    o += "\n lamp         : " + String(Logo.level()) + "/" + String(TheSign.logoSegCount()) +
         " guests lit, " + String(Logo.ledCount()) + " LEDs on GPIO" + String(Logo.activePin());
    o += "\n sign link    : " + (Net.signAddr().length()
             ? ("http://" + Net.signAddr()) : String("not announced yet"));
    o += "\n test overlay : " + String(Logo.test().mode == TEST_NONE ? "none" : "ACTIVE");
#endif
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
    d["device"]   = DEVICE_NAME;
    d["role"]     = ROLE_NAME;
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
#if ROLE_SIGN
#if ROLE_SIGN
    root["show"] = TheShow.stageName();
#endif
    root["role"] = ROLE_NAME;
#endif
    root["role"] = ROLE_NAME;
    emitLine(d);
}

void DebugConsole::emitStatus() {
    static const char *TEST_NAMES[] = {"none", "index", "range", "all", "identify", "walk"};

    JsonDocument d;
    d["type"]   = "status";
    d["role"]   = ROLE_NAME;
    d["heap"]   = ESP.getFreeHeap();
    d["uptime"] = millis() / 1000;
    d["power"]  = TheSign.settings().power;
    d["dirty"]  = TheSign.dirty();

#if ROLE_SIGN
    const TestState &t = Leds.test();
    d["fps"]       = (int)(Leds.fps() + 0.5f);
    d["frames"]    = Leds.frames();
    d["ma"]        = Leds.estimatedMilliamps();
    d["leds"]      = TheSign.settings().ledCount;
    d["pin"]       = Leds.activePin();
    d["show"]      = TheShow.stageName();
    d["testMode"]  = TEST_NAMES[t.mode <= TEST_WALK ? t.mode : 0];
    d["testIndex"] = t.index;
#else
    const TestState &t = Logo.test();
    d["leds"]      = Logo.ledCount();
    d["pin"]       = Logo.activePin();
    d["signSeen"]  = Net.signSeen();
    d["signAddr"]  = Net.signAddr();
    d["signAddr"]  = Net.signAddr();
    d["logoLevel"] = Logo.level();
    d["logoSegs"]  = TheSign.logoSegCount();
    d["logoFull"]  = Logo.full();
    d["logoLearn"] = Logo.learning();
    d["logoLit"]   = Logo.litMask();      // which segments are lit, one bit each
    d["lastCard"]  = Logo.lastCard();
    d["logoTest"]  = TEST_NAMES[t.mode <= TEST_WALK ? t.mode : 0];
    d["logoIndex"] = t.index;
    d["testMode"]  = TEST_NAMES[t.mode <= TEST_WALK ? t.mode : 0];
    d["testIndex"] = t.index;
#endif
    emitLine(d);
}
