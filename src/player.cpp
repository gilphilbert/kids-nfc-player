#include "player.h"

mdCallback _callback;
stateCallback _stCallback;
audioMetadata currentTrack;
String nowPlaying = "";
uint32_t dataLength = 0;
uint32_t dataBitrate = 0;

/*------------------------------------------------
|                                                 |
|               SD                                |
|                                                 |
 ------------------------------------------------*/

#include <SD_MMC.h>
#include "FS.h"

#define SD_D0   12
#define SD_CMD  14
#define SD_CLK  13
#define SD_D3   15
#define SD_D2   16
#define SD_D1   11

/*------------------------------------------------
|                                                 |
|             AUDIO                               |
|                                                 |
 ------------------------------------------------*/

 #include "Audio.h" 
Audio audio;

#define I2S_DOUT      6
#define I2S_BCLK      7
#define I2S_LRC       8

#define L_SDMODE 9
#define R_SDMODE 10



struct audioMessage{
    uint8_t     cmd;
    const char* txt;
    uint32_t    value;
    uint32_t    ret;
} audioTxMessage, audioRxMessage;


enum : uint8_t { SET_VOLUME, GET_VOLUME, CONNECTTOHOST, CONNECTTOSD, STOP, PAUSE_RESUME };

QueueHandle_t audioSetQueue = NULL;
QueueHandle_t audioGetQueue = NULL;

void CreateQueues() {
    audioSetQueue = xQueueCreate(10, sizeof(struct audioMessage));
    audioGetQueue = xQueueCreate(10, sizeof(struct audioMessage));
}

void audioTask(void *parameter) {
    CreateQueues();
    if(!audioSetQueue || !audioGetQueue){
        log_e("queues are not initialized");
        while(true){;}  // endless loop
    }

    struct audioMessage audioRxTaskMessage;
    struct audioMessage audioTxTaskMessage;

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(16); // 0...21 //probably need a way to be able to set this, although kids will just leave it at max. gated?
    audio.setTone(5, 2, 0); //3db boost for lowpass

    while(true){
        if(xQueueReceive(audioSetQueue, &audioRxTaskMessage, 1) == pdPASS) {
            if(audioRxTaskMessage.cmd == SET_VOLUME){
                audioTxTaskMessage.cmd = SET_VOLUME;
                audio.setVolume(audioRxTaskMessage.value);
                audioTxTaskMessage.ret = 1;
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == CONNECTTOHOST){
                audioTxTaskMessage.cmd = CONNECTTOHOST;
                audioTxTaskMessage.ret = audio.connecttohost(audioRxTaskMessage.txt);
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == CONNECTTOSD){
                audioTxTaskMessage.cmd = CONNECTTOSD;
                audioTxTaskMessage.ret = audio.connecttoFS(SD_MMC, audioRxTaskMessage.txt);
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == GET_VOLUME){
                audioTxTaskMessage.cmd = GET_VOLUME;
                audioTxTaskMessage.ret = audio.getVolume();
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == STOP) {
                audioTxTaskMessage.cmd = STOP;
                audioTxTaskMessage.ret = audio.stopSong();
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == PAUSE_RESUME) {
                audioTxTaskMessage.cmd = PAUSE_RESUME;
                audioTxTaskMessage.ret = audio.pauseResume();
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else{
                log_i("error");
            }
        }
        audio.loop();
        vTaskDelay(1);
    }
}

void audioInit() {
    xTaskCreatePinnedToCore(
        audioTask,
        "audioplay",
        5000,
        NULL,
        2 | portPRIVILEGE_BIT,
        NULL,
        0 //run on core 0 (main program runs on core 1)
    );
}

audioMessage transmitReceive(audioMessage msg){
    xQueueSend(audioSetQueue, &msg, portMAX_DELAY);
    if(xQueueReceive(audioGetQueue, &audioRxMessage, portMAX_DELAY) == pdPASS){
        if(msg.cmd != audioRxMessage.cmd){
            log_e("wrong reply from message queue");
        }
    }
    return audioRxMessage;
}

void audioSetVolume(uint8_t vol){
    audioTxMessage.cmd = SET_VOLUME;
    audioTxMessage.value = vol;
    audioMessage RX = transmitReceive(audioTxMessage);
}

uint8_t audioGetVolume(){
    audioTxMessage.cmd = GET_VOLUME;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioConnecttohost(const char* host){
    audioTxMessage.cmd = CONNECTTOHOST;
    audioTxMessage.txt = host;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioConnecttoSD(const char* filename){
    audioTxMessage.cmd = CONNECTTOSD;
    audioTxMessage.txt = filename;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}


/*------------------------------------------------
|                                                 |
|                FUNCTIONS                        |
|                                                 |
 ------------------------------------------------*/

 void mute() {
    digitalWrite(L_SDMODE, LOW);
    digitalWrite(R_SDMODE, LOW);
 }

 void unmute() {
    digitalWrite(L_SDMODE, HIGH);
    digitalWrite(R_SDMODE, HIGH);
 }

//stream ready

keyVal splitKV(String str) {
    keyVal kv;
    if (str.indexOf(':')) {
        kv.key = str.substring(0, str.indexOf(':'));
        kv.value = str.substring(str.indexOf(':') + 2);
    } else {
        kv.key = str;
        kv.value = "";
    }
    return kv;
}

void calculateDuration() {
    currentTrack.duration = dataLength / (dataBitrate / 8);
    dataLength = 0;
    dataBitrate = 0;
}

void stopPlayback(bool doStop = false) {
    mute();
    nowPlaying = "";
    currentTrack.album = "";
    currentTrack.artist = "";
    currentTrack.title = "";
    currentTrack.duration = 0;
    _stCallback(STOPPED);
}

// optional
void audio_info(const char *info) {
    keyVal data = splitKV((String)info);
    if (data.key == "stream ready") {
        //probably don't need this, because we will trigger on BitRate
        _stCallback(PLAYING);
    } else if (data.key == "Audio-Length") {
        dataLength = data.value.toInt();
    } else if (data.key == "BitRate") {
        dataBitrate = data.value.toInt();
        calculateDuration();
        _callback(currentTrack);
    }
}
void audio_id3data(const char *info) {
    keyVal data = splitKV((String)info);
    if (data.key == "Album") {
        currentTrack.album = data.value;
    } else if (data.key == "Artist") {
        currentTrack.artist = data.value;
    } else if (data.key == "Title") {
        currentTrack.title = data.value;
    }
}
void audio_eof_mp3(const char *info) {  //end of file
    stopPlayback();
}


/*------------------------------------------------
|                                                 |
|               NAMESPACE FUNCTIONS               |
|                                                 |
 ------------------------------------------------*/
bool Player::init() {
    //Serial.print("Initializing SD card...");

    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
        //Serial.println("SD Card open failed");
        return false;
    }

    /*
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return false;
    }

    Serial.print("SD_MMC Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    */

    pinMode(L_SDMODE, OUTPUT);
    pinMode(R_SDMODE, OUTPUT);

    audioInit();
    return true;
}


 void Player::registerMDCallback(mdCallback _cb) {
    //mdCallback = _cb;
    //mdCallback = std::bind(_cb, std::placeholders::_1);
    _callback = _cb;
}

void Player::registerStateCallback(stateCallback _cb) {
    _stCallback = _cb;
}

bool Player::play(String filename) {
    if (filename != nowPlaying) {
        if (!filename.startsWith("/"))
          filename = "/" + filename;
        if (SD_MMC.exists(filename)) {
            Serial.println("file exists, playing");
            if (audioConnecttoSD(filename.c_str())) {
                unmute();
                nowPlaying = filename;
                return true;
            }
        } else {
            Serial.println("file doesn't exist");
        }
    }
    return false;
}

bool Player::isPlaying() {
    return nowPlaying.length() == 0 ? false : true;
}

bool Player::stop() {
    audioTxMessage.cmd = STOP;
    audioMessage RX = transmitReceive(audioTxMessage);
    nowPlaying = "";
    stopPlayback();
    return RX.ret;
}

bool Player::pause() {
    audioTxMessage.cmd = PAUSE_RESUME;
    audioMessage RX = transmitReceive(audioTxMessage);
    _stCallback(PAUSED);
    return RX.ret;
}

uint Player::getVolume() {
    return audioGetVolume();
}

void Player::setVolume(uint newVol) {
    audioSetVolume(newVol);
}



std::vector<String> Player::getAllFiles() {
    std::vector<String> files;
    File root = SD_MMC.open("/");
    while (true) {
        File entry = root.openNextFile();
        if (! entry)
        {
        break;
        }
        String fn = entry.name();
        if (fn.endsWith(".mp3")) {
        files.push_back(entry.name());
        }
        entry.close();
    }
    return files;
}