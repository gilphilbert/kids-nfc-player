#include "Arduino.h"
#include <vector>

struct audioMetadata { 
    String title;
    String artist;
    String album;
    uint8_t duration;
};
struct keyVal { 
    String key;
    String value;
};
enum PlayState {
    STOPPED,
    PLAYING,
    PAUSED
};

typedef void (*mdCallback)(audioMetadata);
typedef void (*stateCallback)(PlayState);

namespace Player {
    bool init();
    bool play(String filename);
    bool stop();
    bool pause();

    uint getVolume();
    void setVolume(uint newVol);

    bool isPlaying();

    void registerMDCallback(mdCallback);
    void registerStateCallback(stateCallback);    

    std::vector<String> getAllFiles();
}