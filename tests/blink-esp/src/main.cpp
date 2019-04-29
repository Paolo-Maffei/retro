#include <Arduino.h>

#if LOLIN32
constexpr int LED = 22; // not 5!
#elif WROVER
constexpr int LED = 5; // no built-in, use the LCD backlight
#elif TTGOT8
constexpr int LED = 21; // reusing wrover board def
#elif ESP32SD
constexpr int LED = -1; // doesn't appear to have an LED
#else
constexpr int LED = LED_BUILTIN;
#endif

void setup() {
    Serial.begin(115200);
#ifdef BUILTIN_LED
    Serial.printf("\nBUILTIN_LED = %d\n", BUILTIN_LED);
#endif
    pinMode(LED, OUTPUT);
}

void loop() {
    Serial.printf("%lu\n", millis());

    digitalWrite(LED, 0);
    delay(100);
    digitalWrite(LED, 1);
    delay(400);
}
