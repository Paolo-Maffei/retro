#include <Arduino.h>

#if LOLIN32
constexpr int LED = 22; // not 5!
#else
constexpr int LED = BUILTIN_LED;
#endif

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("BUILTIN_LED = %d\n", BUILTIN_LED);
    delay(1000);
    pinMode(LED, OUTPUT);
}

void loop() {
    Serial.printf("%lu\n", millis());

    digitalWrite(LED, 0);
    delay(100);
    digitalWrite(LED, 1);
    delay(400);
}
