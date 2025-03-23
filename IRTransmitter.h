#ifndef IR_TRANSMITTER_H
#define IR_TRANSMITTER_H

#include <Arduino.h>

class IRTransmitter {
  public:
    IRTransmitter(int pin);
    void begin();
    void transmitFrame(uint8_t* buffer, uint8_t dataSize, uint16_t repeat);
    void transmitFrames(uint8_t** frames, uint8_t* sizes, uint16_t* repeats, uint8_t frameCount);
    bool isBusy();
    void testFrequency();
    
  private:
    int _irPin;
    bool _busy;
    uint32_t _pinMask;
    
    void ICACHE_RAM_ATTR sendBurst(int durationUs);
    void ICACHE_RAM_ATTR sendPause(uint8_t symbol);
};

#endif