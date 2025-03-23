#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include "IRTransmitter.h"
#include "OLEDInterface.h"

// Forward declaration to avoid circular dependency
class ESLProtocol;

class WebInterface {
  public:
    WebInterface(ESP8266WebServer* server, IRTransmitter* irTransmitter, OLEDInterface* oledInterface);
    void setupRoutes();
    
  private:
    ESP8266WebServer* _server;
    IRTransmitter* _irTransmitter;
    OLEDInterface* _oledInterface;
    ESLProtocol* _eslProtocol;
    
    // Handler functions
    void handleRoot();
    void handleTransmitImage();
    void handleRawCommand();
    void handleSetSegments();
    void handlePing();
    void handleRefresh();
    void handleWifiConfig();
    void handleRestart();
    void handleStatus();
    void handleTestFrequency();
    void handleNotFound();
    
    // New image processing functions
    bool handleFileUpload();
    bool processImage(const char* filename, uint8_t** imageData, 
                     uint16_t* width, uint16_t* height, bool colorMode);
    bool convertToBinary(uint8_t* pixels, uint16_t width, uint16_t height, 
                        uint8_t* output, bool colorMode, uint8_t threshold = 128);
    bool resizeImage(uint8_t* input, uint16_t inputWidth, uint16_t inputHeight,
                    uint8_t* output, uint16_t outputWidth, uint16_t outputHeight);
    void applyDithering(uint8_t* pixels, uint16_t width, uint16_t height);
    
    // Helper functions
    bool parseHexString(String hexString, uint8_t* buffer, uint16_t maxLength, uint16_t* actualLength);
    void sendSuccessResponse(String message);
    void sendErrorResponse(String error);
    void sendHtmlResponse(String html, int statusCode = 200);
    void serveStatic(const char* uri, const char* contentType, const char* content);
};

#endif