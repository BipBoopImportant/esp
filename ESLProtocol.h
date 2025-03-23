#ifndef ESL_PROTOCOL_H
#define ESL_PROTOCOL_H

#include <Arduino.h>
#include "IRTransmitter.h"

class ESLProtocol {
  public:
    ESLProtocol(IRTransmitter* irTransmitter);
    
    // Main functions matching the Python scripts
    bool transmitImage(const char* barcodeStr, uint8_t* imageData, 
                      uint16_t width, uint16_t height, uint8_t page = 0, 
                      bool colorMode = false, uint16_t posX = 0, uint16_t posY = 0,
                      bool forcePP4 = false);
                      
    bool transmitRawCommand(const char* barcodeStr, const char* typeStr, 
                           uint8_t* frameData, uint16_t dataSize, uint16_t repeatCount);
                           
    bool setSegments(const char* barcodeStr, uint8_t* bitmap);
    
    // Utility functions
    bool makePingFrame(const char* barcodeStr, bool pp16, uint16_t repeats);
    bool makeRefreshFrame(const char* barcodeStr, bool pp16);
    
    // Helper functions
    uint16_t calculateCRC16(uint8_t* data, uint16_t length);
    void getPLIDFromBarcode(const char* barcode, uint8_t* PLID);
    void compressImage(uint8_t* inputData, uint16_t width, uint16_t height, 
                      uint8_t* outputData, uint16_t* outputSize, bool colorMode);
    
  private:
    IRTransmitter* _irTransmitter;
    
    // Frame creation functions
    void createPingFrame(uint8_t* PLID, bool pp16, uint16_t repeats, 
                        uint8_t* frameData, uint8_t* frameSize);
                        
    void createMCUFrame(uint8_t* PLID, uint8_t cmd, uint8_t* data, 
                       uint16_t dataLength, bool pp16, uint16_t repeats,
                       uint8_t* frameData, uint8_t* frameSize);
                       
    void createRawFrame(uint8_t protocol, uint8_t* PLID, uint8_t cmd, 
                       uint8_t* data, uint16_t dataLength, bool pp16, 
                       uint16_t repeats, uint8_t* frameData, uint8_t* frameSize);
                       
    void terminateFrame(uint8_t* frame, uint16_t frameLength, bool pp16, 
                       uint16_t repeats, uint8_t* frameSize);
                       
    void appendWord(uint8_t* buffer, uint16_t offset, uint16_t value);
};

#endif