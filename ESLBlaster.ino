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

// Stats tracking
unsigned long lastActivityTime = 0;
unsigned long totalFramesSent = 0;
unsigned long uptimeStart = 0;
unsigned long lastIpCheck = 0;

// Function prototypes
void loadSettings();
void saveSettings();
void setupWiFi();
void startAPMode();
void handleSerialCommands();

void setup() {
  // Initialize serial
  Serial.begin(115200);
  Serial.println("\nESLBlaster Starting...");
  
  // Set CPU to maximum frequency for best timing accuracy
  system_update_cpu_freq(160);
  
  // Initialize EEPROM
  EEPROM.begin(512);
  
  // Load settings
  loadSettings();
  
  // Initialize IR transmitter
  irTransmitter.begin();
  
  // Initialize OLED
  oledInterface.begin();
  oledInterface.showSplashScreen("ESLBlaster", FW_VERSION);
  delay(1500);
  
  // Initialize file system for image uploads
  if (!LittleFS.begin()) {
    Serial.println("Failed to initialize LittleFS");
    oledInterface.showError("FS Init Failed");
    delay(2000);
  } else {
    Serial.println("File system initialized");
  }
  
  // Set up WiFi
  setupWiFi();
  
  // Initialize web server routes
  webInterface.setupRoutes();
  
  // Start the server
  server.begin();
  Serial.println("HTTP server started");
  
  // Initialize stats
  uptimeStart = millis();
  
  // Ready to use - verify we have a valid IP before showing ready
  delay(1000); // Extra delay to ensure network is stable
  
  IPAddress currentIP;
  String ipString;
  
  if (WiFi.getMode() == WIFI_STA) {
    currentIP = WiFi.localIP();
    ipString = currentIP.toString();
    // Double-check that we have a valid IP
    if (currentIP[0] == 0) {
      ipString = "No IP - Check WiFi";
    }
  } else {
    currentIP = WiFi.softAPIP();
    ipString = "AP: " + currentIP.toString();
  }
  
  oledInterface.showMainScreen("Ready", ipString);

  // Serial protocol indicator
  Serial.println("ESLBlaster" + String(HW_VERSION) + "1");
}

void loop() {
  // Handle incoming web requests
  server.handleClient();
  
  // Handle serial commands
  handleSerialCommands();
  
  // Update OLED display
  oledInterface.update();
  
  // Periodically check and update IP address if it changes
  if (millis() - lastIpCheck > 10000) { // Check every 10 seconds
    lastIpCheck = millis();
    
    IPAddress currentIP;
    String ipString;
    
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      currentIP = WiFi.localIP();
      ipString = currentIP.toString();
      // Only update if we have a valid IP and not already showing it
      if (currentIP[0] != 0) {
        oledInterface.showMainScreen("Ready", ipString);
      }
    }
  }
  
  // Reconnect WiFi if disconnected (only if in STA mode)
  static unsigned long lastWifiCheck = 0;
  if (WiFi.getMode() == WIFI_STA && millis() - lastWifiCheck > 30000) { // Every 30 seconds
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected. Reconnecting...");
      oledInterface.showStatus("WiFi", "Reconnecting...");
      WiFi.reconnect();
      
      // Wait up to 10 seconds for reconnection
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        oledInterface.showMainScreen("Ready", WiFi.localIP().toString());
      } else {
        oledInterface.showStatus("WiFi Failed", "Check Settings");
      }
    }
  }
  
  // Perform any background tasks
  yield();
}

void loadSettings() {
  // Read settings from EEPROM
  if (EEPROM.read(0) == 0xAA) {
    // Valid settings marker found
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
  } else {
    // No valid settings, use defaults
    strcpy(ssid, "YOUR_WIFI_SSID");
    strcpy(password, "YOUR_WIFI_PASSWORD");
    apMode = false;
  }
}

void saveSettings() {
  // Write settings to EEPROM
  EEPROM.write(0, 0xAA);  // Valid settings marker
  
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
  
  EEPROM.commit();
}

void setupWiFi() {
  // Always start in station mode first (unless explicitly set to AP mode)
  if (apMode) {
    startAPMode();
  } else {
    // Try to connect to WiFi
    oledInterface.showStatus("Connecting", "to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    int wifiAttempts = 0;
    bool connected = false;
    
    // Wait longer for connection (30 attempts * 500ms = 15 seconds max)
    while (wifiAttempts < 30) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      delay(500);
      Serial.print(".");
      wifiAttempts++;
    }
    
    if (connected) {
      // Successfully connected
      Serial.println("\nWiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      
      // Make sure IP is valid before showing it
      IPAddress ip = WiFi.localIP();
      if (ip[0] != 0) {
        oledInterface.showStatus("WiFi Connected", ip.toString());
      } else {
        oledInterface.showStatus("WiFi Connected", "IP Pending");
        delay(2000); // Wait a bit more for DHCP
        oledInterface.showStatus("WiFi Connected", WiFi.localIP().toString());
      }
    } else {
      // Connection failed, switch to AP mode
      Serial.println("\nWiFi connection failed");
      startAPMode();
    }
  }
}

void startAPMode() {
  oledInterface.showStatus("Starting", "AP Mode");
  
  // Disconnect from any existing connections first
  WiFi.disconnect();
  delay(100);
  
  // Set up access point mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESLBlaster", "password");
  
  // Wait a moment for AP to initialize
  delay(500);
  
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(apIP);
  oledInterface.showStatus("AP Active", apIP.toString());
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
              saveSettings();
              
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
        saveSettings();
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