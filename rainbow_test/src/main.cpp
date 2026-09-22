#include <FastLED.h>

#define LED_PIN     14
#define NUM_LEDS    100
#define BRIGHTNESS  80

CRGB leds[NUM_LEDS];
uint8_t hue = 0;

void setup() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
    fill_rainbow(leds, NUM_LEDS, hue, 255 / NUM_LEDS);
    FastLED.show();
    hue++;
    delay(20);
}
