#include <Arduino.h>

#if LOLIN32
constexpr int LED = 22; // not 5!
#elif WROVER
constexpr int LED = 5; // no built-in, use the LCD backlight
#else
constexpr int LED = BUILTIN_LED;
#endif

void setup() {
    Serial.begin(115200);
    delay(1000);
#ifdef BUILTIN_LED
    Serial.printf("BUILTIN_LED = %d\n", BUILTIN_LED);
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
