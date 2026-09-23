/*
 * =====================================================================
 *  OIL LAMP - RFID READER + AMBIENT LAMP
 *
 *  Reads MIFARE cards and prints each tap on serial. The laptop this is
 *  plugged into runs bridge/lamp_bridge.py, which forwards the tap to the
 *  ESP32 sign over its Wi-Fi AP, and the sign lights the segment that card
 *  is assigned to.
 *
 *      Arduino  --USB serial-->  laptop  --HTTP-->  ESP32 sign
 *        CARD:A1B2C3D4                    logo card A1B2C3D4
 *
 *  SERIAL PROTOCOL (9600 baud)
 *    out  READY                 once, after boot
 *    out  CARD:<UID>            on every tap, UID as uppercase hex
 *    in   OK                    tap accepted   -> green confirm flash
 *    in   NEW                   card was learned -> blue confirm flash
 *    in   ERR                   unknown card   -> red reject flash
 *
 *  The lamp keeps its own ambient animation on the 21 LEDs here; the reply
 *  from the laptop only changes the confirmation flash, so the lamp still
 *  looks alive if the bridge is not running.
 * =====================================================================
 */

#include <SPI.h>
#include <MFRC522.h>
#include <FastLED.h>

/* ----------------------------------------------------------- RFID ---- */
#define SS_PIN   10
#define RST_PIN  9

MFRC522 rfid(SS_PIN, RST_PIN);

String        lastUid    = "";
unsigned long lastReadAt = 0;
const unsigned long REPEAT_LOCKOUT = 1500;   // ignore the same card for this long

/* ------------------------------------------------------------ LED ---- */
#define LED_PIN      6
#define NUM_LEDS     21
#define BRIGHTNESS   80
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB

CRGB leds[NUM_LEDS];

/* ------------------------------------------------------- ambient ---- */
struct Bubble {
  float   pos;
  float   speed;
  uint8_t brightness;
  int8_t  direction;
};

Bubble bubbles[3];

uint8_t breath   = 0;
bool    breathUp = true;

unsigned long lastFrameTime  = 0;
unsigned long lastBreathStep = 0;
unsigned long lastFlipCheck  = 0;
unsigned long lastLedUpdate  = 0;

const unsigned long LED_FRAME_INTERVAL = 10;

/* ---------------------------------------------------- experience ---- */
bool          experienceActive = false;
unsigned long experienceStart  = 0;
const unsigned long EXPERIENCE_DURATION = 2200;

/* The confirmation colour is set by the laptop's reply, so the lamp shows
   whether the sign actually accepted the card. Defaults to warm gold if no
   reply arrives - the bridge may not be running. */
CRGB confirmColor = CRGB(255, 100, 10);

/* =====================================================================
 *  SETUP
 * ===================================================================== */
void setup() {
  Serial.begin(9600);

  SPI.begin();
  rfid.PCD_Init();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  randomSeed(analogRead(A0));

  for (int i = 0; i < 3; i++) {
    bubbles[i].pos        = random(NUM_LEDS * 10) / 10.0;
    bubbles[i].speed      = random(150, 400) / 100.0;
    bubbles[i].brightness = random(60, 120);
    bubbles[i].direction  = random(2) ? 1 : -1;
  }

  lastFrameTime = millis();
  Serial.println(F("READY"));
}

/* =====================================================================
 *  LOOP
 * ===================================================================== */
void loop() {
  handleRfid();
  handleReply();      // the laptop tells us whether the tap was accepted
  updateLeds();
}

/* =====================================================================
 *  RFID
 * ===================================================================== */
void handleRfid() {
  if (!rfid.PICC_IsNewCardPresent())  return;
  if (!rfid.PICC_ReadCardSerial())    return;

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  unsigned long now = millis();
  if (uid != lastUid || now - lastReadAt > REPEAT_LOCKOUT) {

    /* The one line the bridge parses. Keep the format exactly. */
    Serial.print(F("CARD:"));
    Serial.println(uid);

    lastUid    = uid;
    lastReadAt = now;

    confirmColor     = CRGB(255, 100, 10);   // assume good until told otherwise
    experienceActive = true;
    experienceStart  = now;
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

/* Reply from the laptop: OK accepted, NEW learned, ERR unknown card. */
void handleReply() {
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    line.toUpperCase();

    if      (line == "OK")  confirmColor = CRGB(255, 100, 10);   // warm gold
    else if (line == "NEW") confirmColor = CRGB(40, 130, 255);   // electric blue
    else if (line == "ERR") confirmColor = CRGB(255, 20, 10);    // red reject
  }
}

/* =====================================================================
 *  LED CONTROLLER
 * ===================================================================== */
void updateLeds() {
  unsigned long now = millis();
  if (now - lastLedUpdate < LED_FRAME_INTERVAL) return;

  float dt = (now - lastFrameTime) / 1000.0;
  lastFrameTime = now;
  lastLedUpdate = now;

  if (experienceActive) {
    unsigned long elapsed = now - experienceStart;

    if      (elapsed < 250)                  lightningCharge(elapsed);
    else if (elapsed < 700)                  lightningStrike(elapsed);
    else if (elapsed < 1400)                 energyPulse(elapsed - 700);
    else if (elapsed < EXPERIENCE_DURATION)  experienceFade(elapsed - 1400);
    else {
      experienceActive = false;
      Serial.println(F("IDLE"));
    }
    return;
  }

  updateAmbient(dt, now);
}

/* ------------------------------------------------------- ambient ---- */
void updateAmbient(float dt, unsigned long now) {
  if (now - lastBreathStep >= 30) {
    lastBreathStep = now;
    if (breathUp) { if (++breath >= 90) breathUp = false; }
    else          { if (--breath <= 15) breathUp = true;  }
  }

  fill_solid(leds, NUM_LEDS, CRGB(0, 0, breath));

  bool doFlipCheck = (now - lastFlipCheck >= 100);

  for (int i = 0; i < 3; i++) {
    bubbles[i].pos += bubbles[i].speed * bubbles[i].direction * dt;

    if (bubbles[i].pos < 0)             { bubbles[i].pos = 0;            bubbles[i].direction =  1; }
    if (bubbles[i].pos > NUM_LEDS - 1)  { bubbles[i].pos = NUM_LEDS - 1; bubbles[i].direction = -1; }

    if (doFlipCheck && random(100) < 4) bubbles[i].direction *= -1;

    bubbles[i].brightness =
      constrain(bubbles[i].brightness + random(-3, 4), 40, 140);

    int   i0   = (int)floor(bubbles[i].pos);
    int   i1   = i0 + 1;
    float frac = bubbles[i].pos - i0;

    uint8_t bMain = bubbles[i].brightness;
    uint8_t b0 = bMain * (1.0 - frac);
    uint8_t b1 = bMain * frac;

    if (i0 >= 0 && i0 < NUM_LEDS) leds[i0] += CRGB(b0, b0 * 0.55, b0 * 0.12);
    if (i1 >= 0 && i1 < NUM_LEDS) leds[i1] += CRGB(b1, b1 * 0.55, b1 * 0.12);

    int gLeft  = i0 - 1;
    int gRight = i1 + 1;
    uint8_t glow0 = 25 * (1.0 - frac);
    uint8_t glow1 = 25 * frac;

    if (gLeft  >= 0)        leds[gLeft]  += CRGB(glow0, glow0 * 0.6, glow0 * 0.12);
    if (gRight < NUM_LEDS)  leds[gRight] += CRGB(glow1, glow1 * 0.6, glow1 * 0.12);
  }

  if (doFlipCheck) lastFlipCheck = now;
  FastLED.show();
}

/* --------------------------------------------- phase 1: charging ---- */
void lightningCharge(unsigned long elapsed) {
  fill_solid(leds, NUM_LEDS, CRGB(0, 0, 0));
  uint8_t intensity = map(elapsed, 0, 250, 10, 150);

  for (int i = 0; i < NUM_LEDS; i++)
    if (random(100) < 25)
      leds[i] = CRGB(intensity * 0.15, intensity * 0.45, intensity);

  FastLED.show();
}

/* ----------------------------------------------- phase 2: strike ---- */
void lightningStrike(unsigned long elapsed) {
  int flash = random(100);

  if      (flash < 35) fill_solid(leds, NUM_LEDS, CRGB(255, 255, 255));
  else if (flash < 55) fill_solid(leds, NUM_LEDS, CRGB(40, 130, 255));
  else                 fill_solid(leds, NUM_LEDS, CRGB(0, 0, random(10, 35)));

  FastLED.show();
}

/* ------------------------------------------------ phase 3: pulse ---- */
void energyPulse(unsigned long elapsed) {
  fill_solid(leds, NUM_LEDS, CRGB(0, 0, 10));

  float center   = (NUM_LEDS - 1) / 2.0;
  float progress = elapsed / 700.0;
  float radius   = progress * (NUM_LEDS / 2.0);

  for (int i = 0; i < NUM_LEDS; i++) {
    float distance = abs(i - center);
    if (distance < radius) {
      float intensity = constrain(1.0 - (distance / radius), 0.0, 1.0);
      /* confirmColor carries the sign's verdict on this tap */
      leds[i] += CRGB(confirmColor.r * intensity,
                      confirmColor.g * intensity,
                      confirmColor.b * intensity);
    }
  }
  FastLED.show();
}

/* ------------------------------------------------- phase 4: fade ---- */
void experienceFade(unsigned long elapsed) {
  float progress = constrain(elapsed / 800.0, 0.0, 1.0);

  uint8_t blue  = 20 + 50 * progress;
  uint8_t red   = (confirmColor.r / 2) * (1.0 - progress);
  uint8_t green = (confirmColor.g / 2) * (1.0 - progress);

  fill_solid(leds, NUM_LEDS, CRGB(red, green, blue));

  for (int i = 0; i < 3; i++)
    leds[random(NUM_LEDS)] += CRGB(80, 30, 5);

  FastLED.show();
}
