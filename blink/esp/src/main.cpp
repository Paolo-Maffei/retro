#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    pinMode(22, OUTPUT);
}

void loop() {
    Serial.printf("%lu\n", millis());

    digitalWrite(22, 0);
    delay(100);
    digitalWrite(22, 1);
    delay(400);
}
