#include <Arduino.h>
#include <SD.h>
#include <SPIFFS.h>
#include <jeeh-esp.h>

#define printf Serial.printf

void listDir(fs::FS &fs, const char * dirname) {
    printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory() && file.name()[1] != '.') {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

Pin<21> LED;

// see https://randomnerdtutorials.com/esp32-pinout-reference-gpios/
Pin<32> XIN;
Pin<33> TXD;
Pin<34> RXD;
Pin<25> ZCL;
Pin<26> ZDA;
Pin<27> RST;

#define SLOW 200

#include <zdi-util.h>

void setup() {
    Serial.begin(115200);
    LED.mode(Pinmode::out);

    SPI.begin(14, 2, 15, 13);
    if(SD.begin(13, SPI, 12000000)) // looks like µSD still works @ 120 MHz
        listDir(SD, "/");

    if (SPIFFS.begin())
        listDir(SPIFFS, "/");

    constexpr int CHAN = 0;
    ledcSetup(CHAN, 4*1000*1000, 1);
    //ledcSetup(CHAN, 40*1000*1000, 1);
    ledcWrite(CHAN, 1);
    ledcAttachPin(XIN.pin, CHAN);

    Serial2.begin(9600, SERIAL_8N1, RXD.pin, TXD.pin);
    //Serial2.begin(9600*36/4, SERIAL_8N1, RXD.pin, TXD.pin);
    //Serial2.begin(9600*40/4, SERIAL_8N1, RXD.pin, TXD.pin);
    
    printf("ready\n");
}

void loop() {
    while (Serial2.available())
        Serial.write(Serial2.read());

    if (Serial.available())
        switch (Serial.read()) {

#include <zdi-cmds.h>

            default: printf("?\n");
        }
}
