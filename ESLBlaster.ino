/*
 * ESLBlaster - ESP8266 Implementation (Production Version)
 * Based on original work by furrtek (furrtek.org)
 * 
 * Hardware:
 * - ESP8266 D1 Mini
 * - ThingPulse OLED SSD1306 Shield (64x48)
 * - IR LED with transistor driver on D2 (GPIO4)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <user_interface.h> // For system_update_cpu_freq()
#include <FS.h>         // For SPIFFS file system
#include <LittleFS.h>   // For LittleFS file system
#include "IRTransmitter.h"
#include "WebInterface.h"
#include "OLEDInterface.h"
#include "ESLProtocol.h"
#include <Ticker.h>
#include <ESP8266WiFiMulti.h>
#include <coredecls.h>                  // crc32()
#include <PolledTimeout.h>
#include <ESP8266HTTPUpdateServer.h>   // OTA updates via web

// Wi-Fi settings (will be loaded from EEPROM)
char ssid[32] = "YOUR_WIFI_SSID";
char password[64] = "YOUR_WIFI_PASSWORD";
bool apMode = false;

// Version information
const char* FW_VERSION = "1.0.0";
const char* HW_VERSION = "A";

// Objects initialization
ESP8266WebServer server(80);
SSD1306Wire display(0x3c, SDA, SCL, GEOMETRY_64_48); // 0x3c is the I2C address
IRTransmitter irTransmitter(4); // D2 (GPIO4)
OLEDInterface oledInterface(&display);
WebInterface webInterface(&server, &irTransmitter, &oledInterface);
ESLProtocol eslProtocol(&irTransmitter);
ESP8266WiFiMulti wifiMulti;
ESP8266HTTPUpdateServer httpUpdater;
Ticker watchdogTicker;
Ticker autoSaveTimer;
unsigned int watchdogCounter = 0;
const unsigned int WATCHDOG_TIMEOUT = 30; // 30 second timeout
uint32_t configCrc = 0;
bool configDirty = false;

// Stats tracking
unsigned long lastActivityTime = 0;
unsigned long totalFramesSent = 0;
unsigned long uptimeStart = 0;
unsigned long lastIpCheck = 0;
String lastIpAddress;

// Function prototypes
void loadSettings();
void saveSettings();
void setupWiFi();
void startAPMode();
void handleSerialCommands();
void setupWatchdog();
void incrementWatchdog();
void resetWatchdog();
void manageWiFiConnection();
void resetToDefaultSettings();
uint32_t calculateSettingsCrc();
void checkAndSaveConfig();
void saveMacAddress();
String getDisplayAddress();

void setup() {
  // Increase serial buffer size for better performance
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  Serial.println(F("\nESLBlaster Starting..."));
  
  // Initialize watchdog early
  setupWatchdog();
  
  // Set CPU to maximum frequency
  system_update_cpu_freq(160);
  
  // Initialize EEPROM
  EEPROM.begin(512);
  
  // Load settings with CRC check
  loadSettings();
  
  // Initialize IR transmitter
  irTransmitter.begin();
  
  // Initialize OLED
  oledInterface.begin();
  oledInterface.showSplashScreen("ESLBlaster", FW_VERSION);
  delay(500); // Reduced initial delay
  
  // Initialize file system for image uploads
  if (!LittleFS.begin()) {
    Serial.println(F("Failed to initialize LittleFS"));
    oledInterface.showError("FS Init Failed");
    delay(1000);
    
    // Format the file system if it failed to mount
    Serial.println(F("Formatting file system..."));
    LittleFS.format();
    if (LittleFS.begin()) {
      Serial.println(F("File system formatted and mounted"));
      oledInterface.showStatus("FS Formatted", "Successfully");
    } else {
      Serial.println(F("File system format failed"));
      oledInterface.showError("FS Format Failed");
    }
  } else {
    Serial.println(F("File system initialized"));
    
    // Check and clean temp files
    if (LittleFS.exists("/temp_image.bin")) {
      LittleFS.remove("/temp_image.bin");
    }
  }
  
  // Set up WiFi with improved connection logic
  setupWiFi();
  
  // Initialize web server routes
  webInterface.setupRoutes();
  
  // Setup OTA updates
  httpUpdater.setup(&server, "/update", "admin", "eslblaster");
  
  // Start the server
  server.begin();
  Serial.println(F("HTTP server started"));
  
  // Setup auto-save for configuration changes
  autoSaveTimer.attach(300, checkAndSaveConfig); // Check every 5 minutes
  
  // Initialize stats
  uptimeStart = millis();
  
  // Ready to use - verify we have a valid IP before showing ready
  String ipString = getDisplayAddress();
  oledInterface.showMainScreen("Ready", ipString);
  
  // Serial protocol indicator
  Serial.printf("ESLBlaster%s1 (v%s)\n", HW_VERSION, FW_VERSION);
  
  // Reset watchdog counter
  resetWatchdog();
}

void loop() {
  // Feed the watchdog to prevent reset
  resetWatchdog();
  
  // Handle incoming web requests
  server.handleClient();
  
  // Handle serial commands
  handleSerialCommands();
  
  // Update OLED display
  oledInterface.update();
  
  // Periodically check and update IP address if it changes
  static unsigned long lastIpCheck = 0;
  if (millis() - lastIpCheck > 10000) { // Check every 10 seconds
    lastIpCheck = millis();
    
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      String newIp = WiFi.localIP().toString();
      if (newIp != lastIpAddress && newIp != "0.0.0.0") {
        lastIpAddress = newIp;
        oledInterface.showMainScreen("Ready", newIp);
      }
    }
  }
  
  // Improved WiFi management
  manageWiFiConnection();
  
  // Handle configuration saving if needed
  if (configDirty) {
    saveSettings();
    configDirty = false;
  }
  
  // Yield to avoid WDT resets
  yield();
}

void setupWatchdog() {
  watchdogTicker.attach(1, incrementWatchdog);
}

void incrementWatchdog() {
  watchdogCounter++;
  if (watchdogCounter > WATCHDOG_TIMEOUT) {
    Serial.println(F("Watchdog timeout - restarting"));
    ESP.restart();
  }
}

void resetWatchdog() {
  watchdogCounter = 0;
}

void setupWiFi() {
  if (apMode) {
    startAPMode();
  } else {
    // Try to connect to WiFi with multi support
    oledInterface.showStatus("Connecting", "to WiFi...");
    
    // Clear connection info
    wifiMulti.cleanAPlist();
    
    // Configure WiFi
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    
    // Add the configured AP
    wifiMulti.addAP(ssid, password);
    
    // Try to connect with timeout
    Serial.println(F("Connecting to WiFi..."));
    
    using esp8266::polledTimeout::oneShotMs;
    oneShotMs timeout(15000); // 15 second timeout
    
    while (!timeout && wifiMulti.run() != WL_CONNECTED) {
      delay(100);
      Serial.print(".");
      yield();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      // Successfully connected
      Serial.println(F("\nWiFi connected"));
      Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
      lastIpAddress = WiFi.localIP().toString();
      
      // Save MAC to EEPROM if it's not there
      saveMacAddress();
      
      oledInterface.showStatus("WiFi Connected", lastIpAddress);
    } else {
      // Connection failed, switch to AP mode
      Serial.println(F("\nWiFi connection failed"));
      startAPMode();
    }
  }
}

void manageWiFiConnection() {
  static unsigned long lastWifiCheck = 0;
  
  // Only check every 30 seconds in STA mode
  if (WiFi.getMode() == WIFI_STA && millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi disconnected. Reconnecting..."));
      oledInterface.showStatus("WiFi", "Reconnecting...");
      
      // Try to reconnect using wifiMulti
      using esp8266::polledTimeout::oneShotMs;
      oneShotMs timeout(10000); // 10 second timeout
      
      while (!timeout && wifiMulti.run() != WL_CONNECTED) {
        delay(100);
        yield();
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        lastIpAddress = WiFi.localIP().toString();
        oledInterface.showMainScreen("Ready", lastIpAddress);
      } else {
        oledInterface.showStatus("WiFi Failed", "Check Settings");
      }
    }
  }
}

void loadSettings() {
  uint32_t storedCrc = EEPROM.get(508, configCrc);
  
  if (EEPROM.read(0) == 0xAA) {
    // Read settings
    int i = 0;
    for (i = 0; i < 32; i++) {
      ssid[i] = EEPROM.read(i + 1);
      if (ssid[i] == 0) break;
    }
    
    for (i = 0; i < 64; i++) {
      password[i] = EEPROM.read(i + 33);
      if (password[i] == 0) break;
    }
    
    apMode = EEPROM.read(97) == 1;
    
    // Calculate CRC32 of the loaded settings
    uint32_t calculatedCrc = calculateSettingsCrc();
    
    if (calculatedCrc != storedCrc) {
      Serial.println(F("Settings CRC mismatch, using defaults"));
      resetToDefaultSettings();
    }
  } else {
    // No valid settings, use defaults
    resetToDefaultSettings();
  }
}

void resetToDefaultSettings() {
  strcpy(ssid, "ESLBlaster");
  strcpy(password, "password");
  apMode = true; // Default to AP mode for new devices
  configDirty = true;
}

void saveSettings() {
  // Write marker
  EEPROM.write(0, 0xAA);
  
  // Write SSID
  for (int i = 0; i < 32; i++) {
    EEPROM.write(i + 1, ssid[i]);
    if (ssid[i] == 0) break;
  }
  
  // Write password
  for (int i = 0; i < 64; i++) {
    EEPROM.write(i + 33, password[i]);
    if (password[i] == 0) break;
  }
  
  // Write AP mode flag
  EEPROM.write(97, apMode ? 1 : 0);
  
  // Calculate and store CRC
  configCrc = calculateSettingsCrc();
  EEPROM.put(508, configCrc);
  
  // Commit changes
  EEPROM.commit();
  Serial.println(F("Settings saved to EEPROM"));
}

uint32_t calculateSettingsCrc() {
  // Create a buffer with all settings
  uint8_t buffer[98]; // 97 bytes of data + marker
  buffer[0] = 0xAA;
  
  // Copy SSID
  for (int i = 0; i < 32; i++) {
    buffer[i + 1] = ssid[i];
  }
  
  // Copy password
  for (int i = 0; i < 64; i++) {
    buffer[i + 33] = password[i];
  }
  
  // Copy AP mode
  buffer[97] = apMode ? 1 : 0;
  
  // Calculate CRC32
  return crc32(buffer, sizeof(buffer));
}

void checkAndSaveConfig() {
  if (configDirty) {
    saveSettings();
    configDirty = false;
  }
}

void saveMacAddress() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  
  bool macChanged = false;
  for (int i = 0; i < 6; i++) {
    if (EEPROM.read(98 + i) != mac[i]) {
      EEPROM.write(98 + i, mac[i]);
      macChanged = true;
    }
  }
  
  if (macChanged) {
    EEPROM.commit();
    Serial.print(F("MAC Address saved: "));
    for (int i = 0; i < 6; i++) {
      Serial.print(mac[i], HEX);
      if (i < 5) Serial.print(":");
    }
    Serial.println();
  }
}

String getDisplayAddress() {
  if (WiFi.getMode() == WIFI_STA) {
    IPAddress ip = WiFi.localIP();
    lastIpAddress = ip.toString();
    if (ip[0] == 0) {
      return "No IP - Check WiFi";
    }
    return lastIpAddress;
  } else {
    IPAddress ip = WiFi.softAPIP();
    lastIpAddress = "AP: " + ip.toString();
    return lastIpAddress;
  }
}

void handleSerialCommands() {
  if (Serial.available()) {
    char cmd = Serial.read();
    String ipString; // Moved outside the switch to avoid initialization crossing
    
    switch (cmd) {
      case '?':
        // Device identification
        Serial.println("ESLBlaster" + String(HW_VERSION) + "1");
        break;
        
      case 'L':  // Load data (original protocol)
        if (Serial.available() >= 4) {
          uint8_t dataSize = Serial.read();
          uint8_t delay = Serial.read();
          uint8_t repeatsLow = Serial.read();
          uint8_t repeatsHigh = Serial.read();
          uint16_t repeats = repeatsLow | (repeatsHigh << 8);
          bool pp16 = repeats & 0x8000;
          repeats &= 0x7FFF;
          
          uint8_t buffer[256];
          int bytesRead = 0;
          
          // Read data with timeout
          unsigned long startTime = millis();
          while (bytesRead < dataSize && (millis() - startTime) < 5000) {
            if (Serial.available()) {
              buffer[bytesRead++] = Serial.read();
              startTime = millis();  // Reset timeout on successful read
            }
            yield();  // Allow ESP8266 to handle background tasks
          }
          
          if (bytesRead == dataSize) {
            // Data fully received
            oledInterface.showTransmitting(1, 1, dataSize, repeats);
            irTransmitter.transmitFrame(buffer, dataSize, repeats);
            Serial.write('K');  // Acknowledge successful transmission
            totalFramesSent++;
            
            // Update status display
            ipString = WiFi.getMode() == WIFI_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
            oledInterface.showMainScreen("Ready", ipString);
          } else {
            // Timeout
            Serial.write('E');  // Error
          }
        }
        break;
        
      case 'W':  // Set WiFi credentials
        {
          // Format: W:SSID:PASSWORD
          // Wait for data with timeout
          String serialData = "";
          unsigned long startTime = millis();
          while ((millis() - startTime) < 5000) {
            if (Serial.available()) {
              char c = Serial.read();
              if (c == '\n') break;
              serialData += c;
              startTime = millis();  // Reset timeout on successful read
            }
            yield();
          }
          
          if (serialData.length() > 0) {
            int separatorPos = serialData.indexOf(':');
            if (separatorPos > 0 && separatorPos < serialData.length() - 1) {
              String newSSID = serialData.substring(0, separatorPos);
              String newPassword = serialData.substring(separatorPos + 1);
              
              // Save to EEPROM
              strncpy(ssid, newSSID.c_str(), 31);
              ssid[31] = '\0';
              strncpy(password, newPassword.c_str(), 63);
              password[63] = '\0';
              apMode = false;
              configDirty = true;
              
              Serial.write('K');  // Acknowledge
              delay(500);
              ESP.restart();  // Restart to apply new settings
            } else {
              Serial.write('E');  // Error
            }
          } else {
            Serial.write('E');  // Error
          }
        }
        break;
        
      case 'A':  // Toggle AP mode
        apMode = !apMode;
        configDirty = true;
        Serial.write('K');  // Acknowledge
        delay(500);
        ESP.restart();  // Restart to apply new settings
        break;
        
      case 'R':  // Restart device
        Serial.write('K');  // Acknowledge
        delay(500);
        ESP.restart();
        break;
        
      case 'S':  // Status
        {
          String status = "Status:\n";
          status += "WiFi: " + String(WiFi.getMode() == WIFI_STA ? "Station" : "AP") + "\n";
          status += "Connected: " + String(WiFi.status() == WL_CONNECTED ? "Yes" : "No") + "\n";
          status += "IP: " + (WiFi.getMode() == WIFI_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\n";
          status += "Uptime: " + String(millis() / 1000) + "s\n";
          status += "Frames sent: " + String(totalFramesSent) + "\n";
          status += "Free heap: " + String(ESP.getFreeHeap()) + "\n";
          Serial.println(status);
        }
        break;
        
      case 'T':  // Test frequency
        oledInterface.showStatus("Testing", "1.25MHz signal");
        irTransmitter.testFrequency();
        Serial.write('K');  // Acknowledge
        // Return to ready state
        ipString = WiFi.getMode() == WIFI_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        oledInterface.showMainScreen("Ready", ipString);
        break;
        
      default:
        // Unknown command
        break;
    }
  }
}