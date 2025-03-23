#include "OLEDInterface.h"

OLEDInterface::OLEDInterface(SSD1306Wire* display) {
  _display = display;
  _lastUpdate = 0;
  _animationFrame = 0;
  _isTransmitting = false;
  _isScrolling = false;
  _scrollPosition = 0;
  _scrollTimer = 0;
}

void OLEDInterface::begin() {
  _display->init();
  _display->flipScreenVertically();
  _display->setFont(ArialMT_Plain_10);
  _display->setTextAlignment(TEXT_ALIGN_CENTER);
}

void OLEDInterface::showSplashScreen(String title, String version) {
  _display->clear();
  _display->setFont(ArialMT_Plain_16);
  _display->drawString(32, 8, title);
  _display->setFont(ArialMT_Plain_10);
  _display->drawString(32, 30, version);
  _display->display();
}

void OLEDInterface::showStatus(String line1, String line2) {
  _statusLine1 = line1;
  _statusLine2 = line2;
  _isTransmitting = false;
  _isScrolling = (line2.length() > 10);
  _scrollPosition = 0;
  _scrollTimer = millis();
  
  _display->clear();
  _display->setFont(ArialMT_Plain_10);
  _display->drawString(32, 10, line1);
  
  if (_isScrolling) {
    _display->drawString(32, 30, line2.substring(0, 10) + "...");
  } else {
    _display->drawString(32, 30, line2);
  }
  
  _display->display();
}

void OLEDInterface::showMainScreen(String status, String details) {
  _isTransmitting = false;
  _statusLine1 = status;
  _statusLine2 = details;
  _isScrolling = (details.length() > 10);
  _scrollPosition = 0;
  _scrollTimer = millis();
  
  _display->clear();
  
  // Draw header
  _display->setFont(ArialMT_Plain_10);
  _display->setTextAlignment(TEXT_ALIGN_LEFT);
  _display->drawString(0, 0, "ESLBlaster");
  
  // Draw horizontal line
  _display->drawHorizontalLine(0, 12, 64);
  
  // Draw status
  _display->setTextAlignment(TEXT_ALIGN_CENTER);
  _display->drawString(32, 16, status);
  
  // Ensure details is not empty
  if (details.length() == 0) {
    details = "No IP Available";
    _statusLine2 = details;
  }
  
  // For long IP addresses or text, prepare for scrolling
  if (_isScrolling) {
    _display->drawString(32, 30, details.substring(0, 10) + "...");
  } else {
    _display->drawString(32, 30, details);
  }
  
  _display->display();
}

void OLEDInterface::showTransmitting(int current, int total, int size, int repeats) {
  _isTransmitting = true;
  _isScrolling = false;
  _txCurrent = current;
  _txTotal = total;
  
  _display->clear();
  
  // Draw header
  _display->setFont(ArialMT_Plain_10);
  _display->setTextAlignment(TEXT_ALIGN_LEFT);
  _display->drawString(0, 0, "Transmitting");
  
  // Draw horizontal line
  _display->drawHorizontalLine(0, 12, 64);
  
  // Draw progress
  _display->setTextAlignment(TEXT_ALIGN_CENTER);
  _display->drawString(32, 16, String(current) + "/" + String(total));
  
  // Progress bar
  int progressWidth = (current * 60) / total;
  _display->drawProgressBar(2, 32, 60, 10, (progressWidth * 100) / 60);
  
  _display->display();
}

void OLEDInterface::showError(String errorMsg) {
  _isTransmitting = false;
  _isScrolling = (errorMsg.length() > 10);
  _scrollPosition = 0;
  _scrollTimer = millis();
  
  _display->clear();
  
  // Draw header
  _display->setFont(ArialMT_Plain_10);
  _display->setTextAlignment(TEXT_ALIGN_LEFT);
  _display->drawString(0, 0, "Error");
  
  // Draw horizontal line
  _display->drawHorizontalLine(0, 12, 64);
  
  // Draw error message
  _display->setTextAlignment(TEXT_ALIGN_CENTER);
  
  if (_isScrolling) {
    _display->drawString(32, 24, errorMsg.substring(0, 10) + "...");
    _statusLine2 = errorMsg;
  } else {
    _display->drawString(32, 24, errorMsg);
  }
  
  _display->display();
}

void OLEDInterface::update() {
  unsigned long currentTime = millis();
  bool needsUpdate = false;
  
  // Update animations for transmitting state
  if (_isTransmitting && currentTime - _lastUpdate > 200) {
    _lastUpdate = currentTime;
    _animationFrame = (_animationFrame + 1) % 4;
    
    // Update only the animation dots instead of full refresh
    _display->setColor(BLACK);
    _display->fillRect(59, 0, 5, 10); // Clear dot area
    _display->setColor(WHITE);
    
    _display->setTextAlignment(TEXT_ALIGN_RIGHT);
    String dots;
    for (int i = 0; i < _animationFrame + 1; i++) {
      dots += '.';
    }
    _display->drawString(64, 0, dots);
    needsUpdate = true;
  }
  
  // Handle scrolling for long text
  if (_isScrolling && !_isTransmitting && currentTime - _scrollTimer > 500) {
    _scrollTimer = currentTime;
    
    _scrollPosition = (_scrollPosition + 1) % _statusLine2.length();
    
    // Optimize the scrolling by only updating the affected area
    _display->setColor(BLACK);
    _display->fillRect(0, 30, 64, 10); // Clear text area
    _display->setColor(WHITE);
    
    _display->setTextAlignment(TEXT_ALIGN_CENTER);
    
    // Optimize string handling for scrolling
    int endPos = _scrollPosition + 10;
    String scrollText;
    
    if (endPos > _statusLine2.length()) {
      // Use reserve to prevent memory fragmentation
      scrollText.reserve(_statusLine2.length() + 10);
      scrollText = _statusLine2.substring(_scrollPosition);
      scrollText += ' ';
      scrollText += _statusLine2.substring(0, endPos - _statusLine2.length());
    } else {
      scrollText = _statusLine2.substring(_scrollPosition, endPos);
    }
    
    _display->drawString(32, 30, scrollText);
    needsUpdate = true;
  }
  
  // Only call display() when needed
  if (needsUpdate) {
    _display->display();
  }
}

// Add new method for showing progress percentage
void OLEDInterface::showProgress(const String& title, int percent) {
  _display->clear();
  
  // Draw header
  _display->setFont(ArialMT_Plain_10);
  _display->setTextAlignment(TEXT_ALIGN_LEFT);
  _display->drawString(0, 0, title);
  
  // Draw horizontal line
  _display->drawHorizontalLine(0, 12, 64);
  
  // Draw percentage
  _display->setTextAlignment(TEXT_ALIGN_CENTER);
  _display->drawString(32, 16, String(percent) + "%");
  
  // Progress bar
  _display->drawProgressBar(2, 32, 60, 10, percent);
  
  _display->display();
}

// Add a method to draw battery level
void OLEDInterface::showBatteryLevel(int level) {
  _display->setColor(BLACK);
  _display->fillRect(0, 0, 16, 8); // Clear battery area
  _display->setColor(WHITE);
  
  // Draw battery outline
  _display->drawRect(0, 0, 12, 8);
  _display->fillRect(12, 2, 2, 4); // Battery terminal
  
  // Draw battery level
  int fillWidth = map(level, 0, 100, 0, 10);
  if (fillWidth > 0) {
    _display->fillRect(1, 1, fillWidth, 6);
  }
  
  _display->display();
}

// Add method to show network signal strength
void OLEDInterface::showWiFiSignal(int rssi) {
  int signalBars = 0;
  
  // Convert RSSI to bars (0-4)
  if (rssi >= -50) signalBars = 4;
  else if (rssi >= -60) signalBars = 3;
  else if (rssi >= -70) signalBars = 2;
  else if (rssi >= -80) signalBars = 1;
  else signalBars = 0;
  
  _display->setColor(BLACK);
  _display->fillRect(48, 0, 10, 8); // Clear WiFi area
  _display->setColor(WHITE);
  
  for (int i = 0; i < signalBars; i++) {
    _display->fillRect(48 + (i*2), 6 - (i*2), 2, 2 + (i*2));
  }
  
  _display->display();
}