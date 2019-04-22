#include <Arduino.h>

#define printf Serial.printf

void setup() {
    Serial.begin(115200);
    printf("\n");

#ifdef BUILTIN_LED
    printf("BUILTIN_LED = %d\n", BUILTIN_LED);
#endif

    uint64_t chipid = ESP.getEfuseMac();
    //The chip ID is essentially its MAC address(length: 6 bytes).
    printf("ESP32 Chip ID   %04X%08X\n",
            (uint16_t) (chipid>>32), (uint32_t) chipid);
    printf("\n");

    printf("HeapSize        %d\n", ESP.getHeapSize());
    printf("FreeHeap        %d\n", ESP.getFreeHeap());
    printf("MinFreeHeap     %d\n", ESP.getMinFreeHeap());
    printf("MaxAllocHeap    %d\n", ESP.getMaxAllocHeap());
    printf("\n");

    printf("PsramSize       %d\n", ESP.getPsramSize());
    printf("FreePsram       %d\n", ESP.getFreePsram());
    printf("MinFreePsram    %d\n", ESP.getMinFreePsram());
    printf("MaxAllocPsram   %d\n", ESP.getMaxAllocPsram());
    printf("\n");

    printf("ChipRevision    %d\n", ESP.getChipRevision());
    printf("CpuFreqMHz      %d\n", ESP.getCpuFreqMHz());
    printf("CycleCount      %d\n", ESP.getCycleCount());
    printf("SdkVersion      %s\n", ESP.getSdkVersion());
    printf("\n");

    printf("FlashChipSize   %d\n", ESP.getFlashChipSize());
    printf("FlashChipSpeed  %d\n", ESP.getFlashChipSpeed());
    //FlashMode_t ESP.getFlashChipMode();
    printf("\n");

    printf("SketchSize      %d\n", ESP.getSketchSize());
    printf("SketchMD5       %s\n", ESP.getSketchMD5().c_str());
    printf("FreeSketchSpace %d\n", ESP.getFreeSketchSpace());
    printf("\n");
}

void loop() {}
