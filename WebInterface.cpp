#include "WebInterface.h"
#include "ESLProtocol.h"
#include <ArduinoJson.h>

extern char ssid[32];
extern char password[64];
extern bool apMode;
extern void saveSettings();
extern const char* FW_VERSION;
extern const char* HW_VERSION;
extern unsigned long uptimeStart;
extern unsigned long totalFramesSent;

WebInterface::WebInterface(ESP8266WebServer* server, IRTransmitter* irTransmitter, OLEDInterface* oledInterface) {
  _server = server;
  _irTransmitter = irTransmitter;
  _oledInterface = oledInterface;
  _eslProtocol = new ESLProtocol(irTransmitter);
}

void WebInterface::setupRoutes() {
  _server->on("/", HTTP_GET, [this]() { this->handleRoot(); });
  
  // File upload handling requires special configuration
  _server->on("/transmit-image", HTTP_POST, 
    [this](){ this->sendSuccessResponse("Image upload complete"); },
    [this](){ this->handleTransmitImage(); }
  );
  
  _server->on("/raw-command", HTTP_POST, [this]() { this->handleRawCommand(); });
  _server->on("/set-segments", HTTP_POST, [this]() { this->handleSetSegments(); });
  _server->on("/ping", HTTP_POST, [this]() { this->handlePing(); });
  _server->on("/refresh", HTTP_POST, [this]() { this->handleRefresh(); });
  _server->on("/wifi-config", HTTP_POST, [this]() { this->handleWifiConfig(); });
  _server->on("/restart", HTTP_POST, [this]() { this->handleRestart(); });
  _server->on("/status", HTTP_GET, [this]() { this->handleStatus(); });
  _server->on("/test-frequency", HTTP_GET, [this]() { this->handleTestFrequency(); });
  
  _server->onNotFound([this]() { this->handleNotFound(); });
}

void WebInterface::handleRoot() {
  // Set cache control headers to prevent caching
  _server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  _server->sendHeader("Pragma", "no-cache");
  _server->sendHeader("Expires", "0");
  
  String html = F("<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
"  <meta charset=\"UTF-8\">"
"  <title>ESL Blaster</title>"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"  <meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\" />"
"  <meta http-equiv=\"Pragma\" content=\"no-cache\" />"
"  <meta http-equiv=\"Expires\" content=\"0\" />"
"  <style>"
"    * { box-sizing: border-box; }"
"    body {"
"      font-family: Arial, sans-serif;"
"      max-width: 800px;"
"      margin: 0 auto;"
"      padding: 20px;"
"      color: #333;"
"      line-height: 1.6;"
"    }"
"    h1, h2 {"
"      color: #2c3e50;"
"      margin-top: 0;"
"    }"
"    h1 {"
"      text-align: center;"
"      margin-bottom: 20px;"
"    }"
"    .tab-container {"
"      margin-bottom: 20px;"
"    }"
"    .tabs {"
"      display: flex;"
"      flex-wrap: wrap;"
"      border-bottom: 1px solid #ccc;"
"      margin-bottom: 0;"
"    }"
"    .tab-button {"
"      background-color: #f1f1f1;"
"      border: 1px solid #ccc;"
"      border-bottom: none;"
"      border-radius: 4px 4px 0 0;"
"      padding: 10px 15px;"
"      margin-right: 5px;"
"      margin-bottom: -1px;"
"      cursor: pointer;"
"      transition: 0.3s;"
"      position: relative;"
"      top: 1px;"
"    }"
"    .tab-button:hover {"
"      background-color: #ddd;"
"    }"
"    .tab-button.active {"
"      background-color: #3498db;"
"      color: white;"
"      border-bottom: 1px solid #3498db;"
"    }"
"    .tab-content {"
"      display: none;"
"      padding: 20px;"
"      border: 1px solid #ccc;"
"      border-top: none;"
"      border-radius: 0 0 4px 4px;"
"      background-color: #fff;"
"    }"
"    .tab-content.active {"
"      display: block;"
"    }"
"    .form-group {"
"      margin-bottom: 15px;"
"    }"
"    label {"
"      display: block;"
"      margin-bottom: 5px;"
"      font-weight: bold;"
"    }"
"    input[type=text], input[type=number], input[type=password], select, textarea {"
"      width: 100%;"
"      padding: 10px;"
"      border: 1px solid #ddd;"
"      border-radius: 4px;"
"      font-size: 16px;"
"    }"
"    button[type=submit], button[type=button] {"
"      background-color: #3498db;"
"      color: white;"
"      padding: 10px 15px;"
"      border: none;"
"      border-radius: 4px;"
"      cursor: pointer;"
"      font-size: 16px;"
"      margin-top: 10px;"
"    }"
"    button[type=submit]:hover, button[type=button]:hover {"
"      background-color: #2980b9;"
"    }"
"    #status-message {"
"      margin-top: 20px;"
"      padding: 15px;"
"      border-radius: 4px;"
"      display: none;"
"    }"
"    .success {"
"      background-color: #d4edda;"
"      color: #155724;"
"      border: 1px solid #c3e6cb;"
"    }"
"    .error {"
"      background-color: #f8d7da;"
"      color: #721c24;"
"      border: 1px solid #f5c6cb;"
"    }"
"    .quick-actions {"
"      display: flex;"
"      flex-wrap: wrap;"
"      gap: 10px;"
"      margin-bottom: 20px;"
"    }"
"    .quick-actions button {"
"      flex: 1;"
"      min-width: 150px;"
"    }"
"  </style>"
"</head>"
"<body>"
"  <h1>ESL Blaster Control Panel</h1>"
"  "
"  <div class=\"quick-actions\">"
"    <button id=\"statusBtn\" type=\"button\">Device Status</button>"
"    <button id=\"restartBtn\" type=\"button\">Restart Device</button>"
"    <button id=\"testFreqBtn\" type=\"button\">Test 1.25MHz</button>"
"  </div>"
"  "
"  <div class=\"tab-container\">"
"    <div class=\"tabs\">"
"      <button class=\"tab-button active\" data-target=\"ImageTab\">Image</button>"
"      <button class=\"tab-button\" data-target=\"RawTab\">Raw Command</button>"
"      <button class=\"tab-button\" data-target=\"SegmentTab\">Segments</button>"
"      <button class=\"tab-button\" data-target=\"PingTab\">Ping/Refresh</button>"
"      <button class=\"tab-button\" data-target=\"SettingsTab\">WiFi Settings</button>"
"      <button class=\"tab-button\" data-target=\"AboutTab\">About</button>"
"    </div>"
"  "
"    <div id=\"ImageTab\" class=\"tab-content active\">"
"      <h2>Transmit Image to ESL</h2>"
"      <form id=\"imageForm\" enctype=\"multipart/form-data\">"
"        <div class=\"form-group\">"
"          <label for=\"barcode\">ESL Barcode (17 digits):</label>"
"          <input type=\"text\" id=\"barcode\" name=\"barcode\" required pattern=\".{17,17}\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"imageFile\">Image File:</label>"
"          <input type=\"file\" id=\"imageFile\" name=\"imageFile\" accept=\"image/*\" required>"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"page\">Page (0-15):</label>"
"          <input type=\"number\" id=\"page\" name=\"page\" min=\"0\" max=\"15\" value=\"0\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"colorMode\">Color Mode:</label>"
"          <select id=\"colorMode\" name=\"colorMode\">"
"            <option value=\"0\">Black & White</option>"
"            <option value=\"1\">Color</option>"
"          </select>"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"posX\">X Position:</label>"
"          <input type=\"number\" id=\"posX\" name=\"posX\" min=\"0\" value=\"0\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"posY\">Y Position:</label>"
"          <input type=\"number\" id=\"posY\" name=\"posY\" min=\"0\" value=\"0\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"forcePP4\">"
"            <input type=\"checkbox\" id=\"forcePP4\" name=\"forcePP4\">"
"            Force PP4 Protocol"
"          </label>"
"        </div>"
"        "
"        <button type=\"submit\">Transmit Image</button>"
"      </form>"
"    </div>"
"  "
"    <div id=\"RawTab\" class=\"tab-content\">"
"      <h2>Send Raw Command</h2>"
"      <form id=\"rawForm\">"
"        <div class=\"form-group\">"
"          <label for=\"rawBarcode\">ESL Barcode (17 digits):</label>"
"          <input type=\"text\" id=\"rawBarcode\" name=\"barcode\" required pattern=\".{17,17}\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"eslType\">ESL Type:</label>"
"          <select id=\"eslType\" name=\"type\">"
"            <option value=\"DM\">Dot Matrix (DM)</option>"
"            <option value=\"SEG\">Segment (SEG)</option>"
"          </select>"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"hexData\">Hex Data (without first byte and CRC):</label>"
"          <textarea id=\"hexData\" name=\"hexData\" rows=\"4\" required></textarea>"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"repeatCount\">Repeat Count:</label>"
"          <input type=\"number\" id=\"repeatCount\" name=\"repeatCount\" min=\"1\" value=\"1\">"
"        </div>"
"        "
"        <button type=\"submit\">Send Command</button>"
"      </form>"
"    </div>"
"  "
"    <div id=\"SegmentTab\" class=\"tab-content\">"
"      <h2>Set Segments</h2>"
"      <form id=\"segmentForm\">"
"        <div class=\"form-group\">"
"          <label for=\"segBarcode\">ESL Barcode (17 digits):</label>"
"          <input type=\"text\" id=\"segBarcode\" name=\"barcode\" required pattern=\".{17,17}\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"bitmap\">Segment Bitmap (46 hex digits):</label>"
"          <textarea id=\"bitmap\" name=\"bitmap\" rows=\"4\" required pattern=\"[0-9A-Fa-f]{46}\"></textarea>"
"        </div>"
"        "
"        <button type=\"submit\">Set Segments</button>"
"      </form>"
"    </div>"
"  "
"    <div id=\"PingTab\" class=\"tab-content\">"
"      <h2>Ping & Refresh ESLs</h2>"
"      <form id=\"pingForm\">"
"        <div class=\"form-group\">"
"          <label for=\"pingBarcode\">ESL Barcode (17 digits):</label>"
"          <input type=\"text\" id=\"pingBarcode\" name=\"barcode\" required pattern=\".{17,17}\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"forcePP4Ping\">"
"            <input type=\"checkbox\" id=\"forcePP4Ping\" name=\"forcePP4\">"
"            Force PP4 Protocol"
"          </label>"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"repeatCountPing\">Repeat Count:</label>"
"          <input type=\"number\" id=\"repeatCountPing\" name=\"repeatCount\" min=\"1\" value=\"400\">"
"        </div>"
"        "
"        <button type=\"submit\">Send Ping</button>"
"      </form>"
"      "
"      <h2>Refresh Display</h2>"
"      <form id=\"refreshForm\">"
"        <div class=\"form-group\">"
"          <label for=\"refreshBarcode\">ESL Barcode (17 digits):</label>"
"          <input type=\"text\" id=\"refreshBarcode\" name=\"barcode\" required pattern=\".{17,17}\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"forcePP4Refresh\">"
"            <input type=\"checkbox\" id=\"forcePP4Refresh\" name=\"forcePP4\">"
"            Force PP4 Protocol"
"          </label>"
"        </div>"
"        "
"        <button type=\"submit\">Refresh Display</button>"
"      </form>"
"    </div>"
"  "
"    <div id=\"SettingsTab\" class=\"tab-content\">"
"      <h2>WiFi Settings</h2>"
"      <form id=\"wifiForm\">"
"        <div class=\"form-group\">"
"          <label for=\"wifiSsid\">WiFi SSID:</label>"
"          <input type=\"text\" id=\"wifiSsid\" name=\"ssid\" required>"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"wifiPassword\">WiFi Password:</label>"
"          <input type=\"password\" id=\"wifiPassword\" name=\"password\">"
"        </div>"
"        "
"        <div class=\"form-group\">"
"          <label for=\"apMode\">"
"            <input type=\"checkbox\" id=\"apMode\" name=\"apMode\">"
"            Access Point Mode"
"          </label>"
"        </div>"
"        "
"        <button type=\"submit\">Save Settings</button>"
"      </form>"
"    </div>"
"  "
"    <div id=\"AboutTab\" class=\"tab-content\">"
"      <h2>About ESL Blaster</h2>"
"      <p>ESL Blaster is a device for communicating with electronic shelf labels (ESLs) using infrared signals.</p>"
"      <p><strong>Hardware Version:</strong> <span id=\"hwVersion\">Loading...</span></p>"
"      <p><strong>Firmware Version:</strong> <span id=\"fwVersion\">Loading...</span></p>"
"      <p><strong>Uptime:</strong> <span id=\"uptime\">Loading...</span></p>"
"      <p><strong>Free Memory:</strong> <span id=\"freeHeap\">Loading...</span></p>"
"      <p><strong>Build Date:</strong> 2025-03-23</p>"
"      <p><strong>Last Update:</strong> 2025-03-23 05:47:25 UTC</p>"
"      <p><strong>System User:</strong> BipBoopImportant</p>"
"      <h3>Hardware Setup</h3>"
"      <p>IR Transmitter connected to GPIO4 (D2)</p>"
"      <p>SSD1306 OLED Shield on I2C (SDA/SCL)</p>"
"      <h3>Credits</h3>"
"      <p>Based on work by furrtek (furrtek.org)</p>"
"    </div>"
"  </div>"
"  "
"  <div id=\"status-message\"></div>"
"  "
"  <script>"
"    // Immediately invoked function to ensure proper scope"
"    (function() {"
"      console.log('Script loaded');"
"      "
"      // Wait for the DOM to be fully loaded"
"      document.addEventListener('DOMContentLoaded', function() {"
"        console.log('DOM fully loaded');"
"        initializeApp();"
"      });"
"      "
"      // If DOM is already loaded, initialize immediately"
"      if (document.readyState === 'complete' || document.readyState === 'interactive') {"
"        console.log('DOM already loaded, initializing immediately');"
"        setTimeout(initializeApp, 1);"
"      }"
"      "
"      function initializeApp() {"
"        try {"
"          // Tab functionality"
"          const tabButtons = document.querySelectorAll('.tab-button');"
"          const tabContents = document.querySelectorAll('.tab-content');"
"          "
"          console.log('Found tab buttons:', tabButtons.length);"
"          console.log('Found tab contents:', tabContents.length);"
"          "
"          tabButtons.forEach(function(button) {"
"            button.addEventListener('click', function() {"
"              const target = this.getAttribute('data-target');"
"              console.log('Tab clicked:', target);"
"              "
"              // Remove active class from all tabs"
"              tabButtons.forEach(function(btn) {"
"                btn.classList.remove('active');"
"              });"
"              tabContents.forEach(function(content) {"
"                content.classList.remove('active');"
"              });"
"              "
"              // Add active class to current tab"
"              this.classList.add('active');"
"              const targetContent = document.getElementById(target);"
"              if (targetContent) {"
"                targetContent.classList.add('active');"
"              } else {"
"                console.error('Target content not found:', target);"
"              }"
"              "
"              // If About tab is selected, update the information"
"              if (target === 'AboutTab') {"
"                updateAboutInfo();"
"              }"
"            });"
"          });"
"          "
"          // Status message display"
"          function showStatus(message, isError) {"
"            console.log('Status:', message, 'Error:', isError);"
"            const statusDiv = document.getElementById('status-message');"
"            if (!statusDiv) {"
"              console.error('Status message div not found');"
"              return;"
"            }"
"            statusDiv.textContent = message;"
"            statusDiv.className = isError ? 'error' : 'success';"
"            statusDiv.style.display = 'block';"
"            "
"            // Hide after 5 seconds"
"            setTimeout(function() {"
"              statusDiv.style.display = 'none';"
"            }, 5000);"
"          }"
"          "
"          // Form submissions"
"          const forms = {"
"            'imageForm': '/transmit-image',"
"            'rawForm': '/raw-command',"
"            'segmentForm': '/set-segments',"
"            'pingForm': '/ping',"
"            'refreshForm': '/refresh',"
"            'wifiForm': '/wifi-config'"
"          };"
"          "
"          // Process all forms"
"          Object.keys(forms).forEach(function(formId) {"
"            const form = document.getElementById(formId);"
"            if (form) {"
"              console.log('Found form:', formId);"
"              form.addEventListener('submit', function(e) {"
"                e.preventDefault();"
"                console.log('Form submitted:', formId);"
"                "
"                // Special handling for image upload"
"                if (formId === 'imageForm') {"
"                  showStatus('Uploading and processing image...', false);"
"                }"
"                "
"                // Create FormData object"
"                const formData = new FormData(this);"
"                "
"                // Send the form data to the server"
"                fetch(forms[formId], {"
"                  method: 'POST',"
"                  body: formData"
"                })"
"                .then(function(response) {"
"                  return response.json();"
"                })"
"                .then(function(data) {"
"                  if (data.success) {"
"                    showStatus(data.message, false);"
"                    "
"                    // Special handling for WiFi settings"
"                    if (formId === 'wifiForm' && data.success) {"
"                      showStatus(data.message + ' Device will restart...', false);"
"                      setTimeout(function() {"
"                        window.location.reload();"
"                      }, 5000);"
"                    }"
"                  } else {"
"                    showStatus(data.error || 'An error occurred', true);"
"                  }"
"                })"
"                .catch(function(error) {"
"                  console.error('Error:', error);"
"                  showStatus('Network error: ' + error.message, true);"
"                });"
"              });"
"            } else {"
"              console.error('Form not found:', formId);"
"            }"
"          });"
"          "
"          // Quick action buttons"
"          const statusBtn = document.getElementById('statusBtn');"
"          if (statusBtn) {"
"            statusBtn.addEventListener('click', function() {"
"              console.log('Status button clicked');"
"              fetch('/status')"
"                .then(function(response) {"
"                  return response.json();"
"                })"
"                .then(function(data) {"
"                  let statusMessage = 'Status:\\n';"
"                  statusMessage += `WiFi: ${data.wifi_mode}\\n`;"
"                  statusMessage += `Connected: ${data.connected}\\n`;"
"                  statusMessage += `IP: ${data.ip}\\n`;"
"                  statusMessage += `Uptime: ${formatUptime(data.uptime)}\\n`;"
"                  statusMessage += `Free Heap: ${formatBytes(data.free_heap)}\\n`;"
"                  statusMessage += `Frames Sent: ${data.frames_sent}\\n`;"
"                  "
"                  showStatus(statusMessage, false);"
"                })"
"                .catch(function(error) {"
"                  console.error('Error:', error);"
"                  showStatus('Network error: ' + error.message, true);"
"                });"
"            });"
"          } else {"
"            console.error('Status button not found');"
"          }"
"          "
"          const testFreqBtn = document.getElementById('testFreqBtn');"
"          if (testFreqBtn) {"
"            testFreqBtn.addEventListener('click', function() {"
"              console.log('Test frequency button clicked');"
"              showStatus('Testing 1.25MHz signal for 5 seconds...', false);"
"              fetch('/test-frequency')"
"                .then(function(response) {"
"                  return response.json();"
"                })"
"                .then(function(data) {"
"                  showStatus(data.message, !data.success);"
"                })"
"                .catch(function(error) {"
"                  console.error('Error:', error);"
"                  showStatus('Network error: ' + error.message, true);"
"                });"
"            });"
"          } else {"
"            console.error('Test frequency button not found');"
"          }"
"          "
"          const restartBtn = document.getElementById('restartBtn');"
"          if (restartBtn) {"
"            restartBtn.addEventListener('click', function() {"
"              console.log('Restart button clicked');"
"              if (confirm('Are you sure you want to restart the device?')) {"
"                fetch('/restart', { method: 'POST' })"
"                  .then(function(response) {"
"                    return response.json();"
"                  })"
"                  .then(function(data) {"
"                    showStatus(data.message, !data.success);"
"                    "
"                    if (data.success) {"
"                      setTimeout(function() {"
"                        window.location.reload();"
"                      }, 5000);"
"                    }"
"                  })"
"                  .catch(function(error) {"
"                    console.error('Error:', error);"
"                    showStatus('Network error: ' + error.message, true);"
"                  });"
"              }"
"            });"
"          } else {"
"            console.error('Restart button not found');"
"          }"
"          "
"          // About tab information"
"          function updateAboutInfo() {"
"            console.log('Updating About tab info');"
"            fetch('/status')"
"              .then(function(response) {"
"                return response.json();"
"              })"
"              .then(function(data) {"
"                document.getElementById('hwVersion').textContent = data.hw_version || 'A';"
"                document.getElementById('fwVersion').textContent = data.fw_version || '1.0.0';"
"                document.getElementById('uptime').textContent = formatUptime(data.uptime);"
"                document.getElementById('freeHeap').textContent = formatBytes(data.free_heap);"
"              })"
"              .catch(function(error) {"
"                console.error('Error updating about info:', error);"
"                showStatus('Failed to update About info', true);"
"              });"
"          }"
"          "
"          // Initial update of About tab if it's active"
"          if (document.querySelector('#AboutTab.active')) {"
"            updateAboutInfo();"
"          }"
"          "
"          console.log('App initialized successfully');"
"        } catch (e) {"
"          console.error('Error initializing app:', e);"
"        }"
"      }"
"      "
"      // Helper functions"
"      function formatUptime(seconds) {"
"        const days = Math.floor(seconds / 86400);"
"        seconds %= 86400;"
"        const hours = Math.floor(seconds / 3600);"
"        seconds %= 3600;"
"        const minutes = Math.floor(seconds / 60);"
"        seconds %= 60;"
"        "
"        let result = '';"
"        if (days > 0) result += days + ' days, ';"
"        return result + hours + ':' + "
"               minutes.toString().padStart(2, '0') + ':' + "
"               seconds.toString().padStart(2, '0');"
"      }"
"      "
"      function formatBytes(bytes) {"
"        if (bytes < 1024) return bytes + ' bytes';"
"        else if (bytes < 1048576) return (bytes / 1024).toFixed(2) + ' KB';"
"        else return (bytes / 1048576).toFixed(2) + ' MB';"
"      }"
"    })();"
"  </script>"
"</body>"
"</html>");

  sendHtmlResponse(html);
}

void WebInterface::handleTransmitImage() {
  if (!_server->hasArg("barcode")) {
    sendErrorResponse("Missing barcode parameter");
    return;
  }
  
  // Get parameters from form
  String barcode = _server->arg("barcode");
  uint8_t page = _server->hasArg("page") ? _server->arg("page").toInt() : 0;
  bool colorMode = _server->hasArg("colorMode") && _server->arg("colorMode") == "1";
  uint16_t posX = _server->hasArg("posX") ? _server->arg("posX").toInt() : 0;
  uint16_t posY = _server->hasArg("posY") ? _server->arg("posY").toInt() : 0;
  bool forcePP4 = _server->hasArg("forcePP4");
  
  // Handle file upload
  if (!handleFileUpload()) {
    sendErrorResponse("File upload failed");
    return;
  }
  
  _oledInterface->showStatus("Processing", "Image...");
  
  // Process the uploaded image
  uint8_t* imageData = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
  
  // Process image from uploaded file
  if (!processImage("/temp_image.bin", &imageData, &width, &height, colorMode)) {
    sendErrorResponse("Failed to process image");
    return;
  }
  
  _oledInterface->showStatus("Transmitting", "Image to ESL");
  
  // Send the image data to ESL
  bool success = _eslProtocol->transmitImage(
    barcode.c_str(), 
    imageData, 
    width, 
    height, 
    page, 
    colorMode, 
    posX, 
    posY, 
    forcePP4
  );
  
  // Clean up memory
  delete[] imageData;
  
  // Clean up temporary file
  LittleFS.remove("/temp_image.bin");
  
  if (success) {
    sendSuccessResponse("Image transmitted successfully");
  } else {
    sendErrorResponse("Failed to transmit image");
  }
}

bool WebInterface::handleFileUpload() {
  HTTPUpload& upload = _server->upload();
  static File fsUploadFile;
  
  if (upload.status == UPLOAD_FILE_START) {
    // Initialize file system if not already initialized
    if (!LittleFS.begin()) {
      Serial.println("Failed to initialize LittleFS");
      return false;
    }
    
    // Open the file for writing
    fsUploadFile = LittleFS.open("/temp_image.bin", "w");
    if (!fsUploadFile) {
      Serial.println("Failed to open file for writing");
      return false;
    }
    
    Serial.print("Upload started: ");
    Serial.println(upload.filename);
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write the received bytes to the file
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
    Serial.print(".");
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    // Close the file
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    Serial.print("Upload complete: ");
    Serial.print(upload.totalSize);
    Serial.println(" bytes");
    return true;
  }
  
  return true;
}

bool WebInterface::processImage(const char* filename, uint8_t** imageData, 
                              uint16_t* width, uint16_t* height, bool colorMode) {
  // Open the uploaded file
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return false;
  }
  
  // Check file size
  size_t fileSize = file.size();
  if (fileSize < 54) { // Minimum size for a BMP header
    Serial.println("File too small to be a valid image");
    file.close();
    return false;
  }
  
  // Read BMP header
  uint8_t header[54];
  file.read(header, 54);
  
  // Check if it's a valid BMP
  if (header[0] != 'B' || header[1] != 'M') {
    Serial.println("Not a valid BMP file");
    file.close();
    return false;
  }
  
  // Extract image dimensions
  *width = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
  *height = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
  
  // Extract bits per pixel
  uint16_t bpp = header[28] | (header[29] << 8);
  
  // Calculate row size and padding
  uint32_t rowSize = ((*width * bpp + 31) / 32) * 4;
  
  // Allocate memory for the image
  uint32_t pixelDataSize = *width * *height;
  uint8_t* pixels = new uint8_t[pixelDataSize];
  if (!pixels) {
    Serial.println("Failed to allocate memory for image");
    file.close();
    return false;
  }
  
  // Read pixel data
  if (bpp == 24) { // True color image
    uint8_t* row = new uint8_t[rowSize];
    if (!row) {
      delete[] pixels;
      file.close();
      return false;
    }
    
    // BMP stores rows bottom-to-top, so we need to flip it
    for (int y = *height - 1; y >= 0; y--) {
      file.read(row, rowSize);
      
      // Convert RGB to grayscale
      for (int x = 0; x < *width; x++) {
        uint8_t b = row[x * 3];
        uint8_t g = row[x * 3 + 1];
        uint8_t r = row[x * 3 + 2];
        
        // Weighted conversion to grayscale
        pixels[y * *width + x] = (r * 77 + g * 150 + b * 29) >> 8;
      }
    }
    
    delete[] row;
  } 
  else if (bpp == 8) { // Grayscale or palette
    uint8_t* row = new uint8_t[rowSize];
    if (!row) {
      delete[] pixels;
      file.close();
      return false;
    }
    
    // Skip color palette for 8-bit images
    if (header[0x0A] > 54) {
      file.seek(header[0x0A], SeekSet);
    }
    
    // BMP stores rows bottom-to-top, so we need to flip it
    for (int y = *height - 1; y >= 0; y--) {
      file.read(row, rowSize);
      
      for (int x = 0; x < *width; x++) {
        pixels[y * *width + x] = row[x];
      }
    }
    
    delete[] row;
  } 
  else {
    Serial.print("Unsupported bits per pixel: ");
    Serial.println(bpp);
    delete[] pixels;
    file.close();
    return false;
  }
  
  file.close();
  
  // Apply dithering
  applyDithering(pixels, *width, *height);
  
  // Allocate memory for the output binary image
  *imageData = new uint8_t[(pixelDataSize + 7) / 8 * (colorMode ? 2 : 1)];
  if (!*imageData) {
    delete[] pixels;
    Serial.println("Failed to allocate memory for binary image");
    return false;
  }
  
  // Convert to binary
  if (!convertToBinary(pixels, *width, *height, *imageData, colorMode)) {
    delete[] pixels;
    delete[] *imageData;
    return false;
  }
  
  // Clean up
  delete[] pixels;
  
  return true;
}

bool WebInterface::convertToBinary(uint8_t* pixels, uint16_t width, uint16_t height, 
                                 uint8_t* output, bool colorMode, uint8_t threshold) {
  uint32_t pixelCount = width * height;
  uint32_t byteCount = (pixelCount + 7) / 8;
  
  // Clear output buffer
  memset(output, 0, byteCount * (colorMode ? 2 : 1));
  
  // Process each pixel
  for (uint32_t i = 0; i < pixelCount; i++) {
    uint32_t byteIndex = i / 8;
    uint8_t bitIndex = 7 - (i % 8); // MSB first
    
    // Set the bit if pixel value is below threshold (black)
    if (pixels[i] < threshold) {
      output[byteIndex] |= (1 << bitIndex);
    }
    
    // For color mode (currently just a placeholder - in real implementation 
    // this would process actual color data)
    if (colorMode) {
      // In this simplified implementation, we'll just copy the same data
      // In a real system, you'd extract the color information here
      output[byteCount + byteIndex] |= (pixels[i] < 192) ? (1 << bitIndex) : 0;
    }
  }
  
  return true;
}

void WebInterface::applyDithering(uint8_t* pixels, uint16_t width, uint16_t height) {
  // Floyd-Steinberg dithering algorithm
  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      uint32_t idx = y * width + x;
      uint8_t oldPixel = pixels[idx];
      uint8_t newPixel = (oldPixel < 128) ? 0 : 255;
      pixels[idx] = newPixel;
      
      int16_t error = oldPixel - newPixel;
      
      // Distribute error to neighboring pixels
      if (x < width - 1) {
        pixels[idx + 1] = constrain(pixels[idx + 1] + error * 7 / 16, 0, 255);
      }
      
      if (y < height - 1) {
        if (x > 0) {
          pixels[idx + width - 1] = constrain(pixels[idx + width - 1] + error * 3 / 16, 0, 255);
        }
        
        pixels[idx + width] = constrain(pixels[idx + width] + error * 5 / 16, 0, 255);
        
        if (x < width - 1) {
          pixels[idx + width + 1] = constrain(pixels[idx + width + 1] + error * 1 / 16, 0, 255);
        }
      }
    }
  }
}

bool WebInterface::resizeImage(uint8_t* input, uint16_t inputWidth, uint16_t inputHeight,
                             uint8_t* output, uint16_t outputWidth, uint16_t outputHeight) {
  // Simple nearest-neighbor resizing
  float xRatio = (float)inputWidth / outputWidth;
  float yRatio = (float)inputHeight / outputHeight;
  
  for (uint16_t y = 0; y < outputHeight; y++) {
    for (uint16_t x = 0; x < outputWidth; x++) {
      uint16_t srcX = (uint16_t)(x * xRatio);
      uint16_t srcY = (uint16_t)(y * yRatio);
      
      output[y * outputWidth + x] = input[srcY * inputWidth + srcX];
    }
  }
  
  return true;
}

bool WebInterface::parseHexString(String hexString, uint8_t* buffer, uint16_t maxLength, uint16_t* actualLength) {
  // Trim whitespace and normalize format
  hexString.trim();
  hexString.replace(" ", "");
  hexString.replace("\n", "");
  hexString.replace("\r", "");
  hexString.replace(",", "");
  hexString.replace("0x", "");
  hexString.replace("0X", "");
  
  // Check if the string has an even number of characters
  if (hexString.length() % 2 != 0) {
    return false;
  }
  
  // Check if the string is too long
  if (hexString.length() / 2 > maxLength) {
    return false;
  }
  
  *actualLength = hexString.length() / 2;
  
  // Convert each pair of hex characters to a byte
  for (uint16_t i = 0; i < *actualLength; i++) {
    char highNibble = hexString.charAt(i * 2);
    char lowNibble = hexString.charAt(i * 2 + 1);
    
    // Convert high nibble
    uint8_t high;
    if (highNibble >= '0' && highNibble <= '9') {
      high = highNibble - '0';
    } else if (highNibble >= 'a' && highNibble <= 'f') {
      high = highNibble - 'a' + 10;
    } else if (highNibble >= 'A' && highNibble <= 'F') {
      high = highNibble - 'A' + 10;
    } else {
      return false; // Invalid character
    }
    
    // Convert low nibble
    uint8_t low;
    if (lowNibble >= '0' && lowNibble <= '9') {
      low = lowNibble - '0';
    } else if (lowNibble >= 'a' && lowNibble <= 'f') {
      low = lowNibble - 'a' + 10;
    } else if (lowNibble >= 'A' && lowNibble <= 'F') {
      low = lowNibble - 'A' + 10;
    } else {
      return false; // Invalid character
    }
    
    // Combine high and low nibbles
    buffer[i] = (high << 4) | low;
  }
  
  return true;
}

void WebInterface::handleRawCommand() {
  if (!_server->hasArg("barcode") || !_server->hasArg("type") || 
      !_server->hasArg("hexData") || !_server->hasArg("repeatCount")) {
    sendErrorResponse("Missing required parameters");
    return;
  }
  
  String barcode = _server->arg("barcode");
  String type = _server->arg("type");
  String hexData = _server->arg("hexData");
  int repeatCount = _server->arg("repeatCount").toInt();
  
  // Parse the hex data
  uint8_t buffer[256];
  uint16_t dataSize;
  
  if (!parseHexString(hexData, buffer, 256, &dataSize)) {
    sendErrorResponse("Invalid hex data format");
    return;
  }
  
  _oledInterface->showStatus("Transmitting", "Raw Command");
  
  // Send the raw command
  bool success = _eslProtocol->transmitRawCommand(barcode.c_str(), type.c_str(), buffer, dataSize, repeatCount);
  
  if (success) {
    sendSuccessResponse("Raw command transmitted successfully");
  } else {
    sendErrorResponse("Failed to transmit command");
  }
}

void WebInterface::handleSetSegments() {
  if (!_server->hasArg("barcode") || !_server->hasArg("bitmap")) {
    sendErrorResponse("Missing required parameters");
    return;
  }
  
  String barcode = _server->arg("barcode");
  String bitmapHex = _server->arg("bitmap");
  
  // Check bitmap length
  if (bitmapHex.length() != 46) {
    sendErrorResponse("Bitmap must be exactly 46 hex digits");
    return;
  }
  
  // Parse the hex bitmap
  uint8_t bitmap[23];
  uint16_t parsedSize;
  
  if (!parseHexString(bitmapHex, bitmap, 23, &parsedSize) || parsedSize != 23) {
    sendErrorResponse("Invalid hex bitmap format");
    return;
  }
  
  _oledInterface->showStatus("Transmitting", "Segment Data");
  
  // Send the segments data
  bool success = _eslProtocol->setSegments(barcode.c_str(), bitmap);
  
  if (success) {
    sendSuccessResponse("Segments updated successfully");
  } else {
    sendErrorResponse("Failed to update segments");
  }
}

void WebInterface::handlePing() {
  if (!_server->hasArg("barcode")) {
    sendErrorResponse("Missing barcode parameter");
    return;
  }
  
  String barcode = _server->arg("barcode");
  bool forcePP4 = _server->hasArg("forcePP4");
  int repeatCount = _server->hasArg("repeatCount") ? _server->arg("repeatCount").toInt() : 400;
  
  _oledInterface->showStatus("Transmitting", "Ping");
  
  bool success = _eslProtocol->makePingFrame(barcode.c_str(), !forcePP4, repeatCount);
  
  if (success) {
    sendSuccessResponse("Ping transmitted successfully");
  } else {
    sendErrorResponse("Failed to transmit ping");
  }
}

void WebInterface::handleRefresh() {
  if (!_server->hasArg("barcode")) {
    sendErrorResponse("Missing barcode parameter");
    return;
  }
  
  String barcode = _server->arg("barcode");
  bool forcePP4 = _server->hasArg("forcePP4");
  
  _oledInterface->showStatus("Transmitting", "Refresh");
  
  bool success = _eslProtocol->makeRefreshFrame(barcode.c_str(), !forcePP4);
  
  if (success) {
    sendSuccessResponse("Refresh command transmitted successfully");
  } else {
    sendErrorResponse("Failed to transmit refresh command");
  }
}

void WebInterface::handleWifiConfig() {
  if (!_server->hasArg("ssid")) {
    sendErrorResponse("Missing SSID parameter");
    return;
  }
  
  String newSSID = _server->arg("ssid");
  String newPassword = _server->hasArg("password") ? _server->arg("password") : "";
  bool newApMode = _server->hasArg("apMode");
  
  // Update settings
  strncpy(ssid, newSSID.c_str(), 31);
  ssid[31] = '\0';
  
  strncpy(password, newPassword.c_str(), 63);
  password[63] = '\0';
  
  apMode = newApMode;
  
  // Save to EEPROM
  saveSettings();
  
  // Return success before restarting
  sendSuccessResponse("WiFi settings updated");
  
  // Schedule a restart after sending the response
  delay(1000);
  ESP.restart();
}

void WebInterface::handleRestart() {
  sendSuccessResponse("Restarting device...");
  delay(1000);
  ESP.restart();
}

void WebInterface::handleStatus() {
  // Create a JSON response with the current status
  DynamicJsonDocument doc(512);
  
  doc["wifi_mode"] = WiFi.getMode() == WIFI_STA ? "Station" : "Access Point";
  doc["connected"] = WiFi.status() == WL_CONNECTED ? "Yes" : "No";
  doc["ip"] = WiFi.getMode() == WIFI_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["uptime"] = (millis() - uptimeStart) / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["frames_sent"] = totalFramesSent;
  doc["cpu_freq"] = ESP.getCpuFreqMHz();
  doc["busy"] = _irTransmitter->isBusy();
  doc["hw_version"] = HW_VERSION;
  doc["fw_version"] = FW_VERSION;
  doc["build_date"] = "2025-03-23";
  doc["last_update"] = "2025-03-23 05:47:25";
  doc["system_user"] = "BipBoopImportant";
  
  String response;
  serializeJson(doc, response);
  
  // Add cache control headers
  _server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  _server->sendHeader("Pragma", "no-cache");
  _server->sendHeader("Expires", "0");
  _server->send(200, "application/json", response);
}

void WebInterface::handleTestFrequency() {
  _oledInterface->showStatus("Testing", "1.25MHz signal");
  
  // Run the test
  _irTransmitter->testFrequency();
  
  // Update display
  String ipString = WiFi.getMode() == WIFI_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  _oledInterface->showMainScreen("Ready", ipString);
  
  // Send response
  DynamicJsonDocument doc(256);
  doc["success"] = true;
  doc["message"] = "1.25MHz test completed successfully";
  
  String response;
  serializeJson(doc, response);
  
  _server->send(200, "application/json", response);
}

void WebInterface::handleNotFound() {
  _server->send(404, "text/plain", "Not Found");
}

void WebInterface::sendSuccessResponse(String message) {
  DynamicJsonDocument doc(256);
  doc["success"] = true;
  doc["message"] = message;
  
  String response;
  serializeJson(doc, response);
  
  _server->send(200, "application/json", response);
}

void WebInterface::sendErrorResponse(String error) {
  DynamicJsonDocument doc(256);
  doc["success"] = false;
  doc["error"] = error;
  
  String response;
  serializeJson(doc, response);
  
  _server->send(400, "application/json", response);
}

void WebInterface::sendHtmlResponse(String html, int statusCode) {
  // Add cache control headers to prevent caching
  _server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  _server->sendHeader("Pragma", "no-cache");
  _server->sendHeader("Expires", "0");
  _server->send(statusCode, "text/html", html);
}

void WebInterface::serveStatic(const char* uri, const char* contentType, const char* content) {
  _server->on(uri, HTTP_GET, [this, contentType, content]() {
    _server->send(200, contentType, content);
  });
}