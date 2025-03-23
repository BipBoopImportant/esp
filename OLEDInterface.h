#ifndef OLED_INTERFACE_H
#define OLED_INTERFACE_H

#include <Arduino.h>
#include "SSD1306Wire.h"

class OLEDInterface {
  public:
    OLEDInterface(SSD1306Wire* display);
    void begin();
    void showSplashScreen(String title, String version);
    void showStatus(String line1, String line2);
    void showMainScreen(String status, String details);
    void showTransmitting(int current, int total, int size, int repeats);
    void showError(String errorMsg);
    void update();
    
  private:
    SSD1306Wire* _display;
    unsigned long _lastUpdate;
    int _animationFrame;
    String _statusLine1;
    String _statusLine2;
    bool _isTransmitting;
    bool _isScrolling;
    int _txCurrent;
    int _txTotal;
    int _scrollPosition;
    unsigned long _scrollTimer;
};

#endif