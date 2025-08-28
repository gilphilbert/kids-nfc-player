#include "update.h"
#include <SD_MMC.h>

bool SDUpdate::check() {
    return SD_MMC.exists("/firmware.bin");
}

void SDUpdate::update() {
    File firmware =  SD_MMC.open("/firmware.bin");
    if (firmware) {
        Update.begin(firmware.size(), U_FLASH);
        Update.writeStream(firmware);
        if (Update.end()){
            Serial.println(F("Update finished!"));
        } else {
            Serial.println(F("Update error!"));
            Serial.println(Update.getError());
        }
        firmware.close();

        SD_MMC.rename("/firmware.bin", "/firmware.bin.current");
    
        ESP.restart();
    }
}