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
  
  // More efficient way to serve HTML - build it in chunks to avoid memory issues
  _server->setContentLength(CONTENT_LENGTH_UNKNOWN); // We don't know the size in advance
  _server->send(200, "text/html", ""); // Start the response

  // Send HTML in chunks to avoid memory issues
  _server->sendContent("<!DOCTYPE html><html lang=\"en\"><head>");
  _server->sendContent("<meta charset=\"UTF-8\"><title>ESL Blaster</title>");
  _server->sendContent("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  _server->sendContent("<meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\" />");
  _server->sendContent("<meta http-equiv=\"Pragma\" content=\"no-cache\" /><meta http-equiv=\"Expires\" content=\"0\" />");
  
  // Send CSS
  _server->sendContent("<style>");
  _server->sendContent("* { box-sizing: border-box; }");
  _server->sendContent("body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; color: #333; line-height: 1.6; }");
  _server->sendContent("h1, h2 { color: #2c3e50; margin-top: 0; }");
  _server->sendContent("h1 { text-align: center; margin-bottom: 20px; }");
  _server->sendContent(".tab-container { margin-bottom: 20px; }");
  _server->sendContent(".tabs { display: flex; flex-wrap: wrap; border-bottom: 1px solid #ccc; margin-bottom: 0; }");
  _server->sendContent(".tab-button { background-color: #f1f1f1; border: 1px solid #ccc; border-bottom: none; border-radius: 4px 4px 0 0; padding: 10px 15px; margin-right: 5px; margin-bottom: -1px; cursor: pointer; transition: 0.3s; position: relative; top: 1px; }");
  _server->sendContent(".tab-button:hover { background-color: #ddd; }");
  _server->sendContent(".tab-button.active { background-color: #3498db; color: white; border-bottom: 1px solid #3498db; }");
  _server->sendContent(".tab-content { display: none; padding: 20px; border: 1px solid #ccc; border-top: none; border-radius: 0 0 4px 4px; background-color: #fff; }");
  _server->sendContent(".tab-content.active { display: block; }");
  _server->sendContent(".form-group { margin-bottom: 15px; }");
  _server->sendContent("label { display: block; margin-bottom: 5px; font-weight: bold; }");
  _server->sendContent("input[type=text], input[type=number], input[type=password], select, textarea { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; font-size: 16px; }");
  _server->sendContent("button[type=submit], button[type=button] { background-color: #3498db; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-top: 10px; }");
  _server->sendContent("button[type=submit]:hover, button[type=button]:hover { background-color: #2980b9; }");
  _server->sendContent("#status-message { margin-top: 20px; padding: 15px; border-radius: 4px; display: none; }");
  _server->sendContent(".success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }");
  _server->sendContent(".error { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }");
  _server->sendContent(".quick-actions { display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 20px; }");
  _server->sendContent(".quick-actions button { flex: 1; min-width: 150px; }");
  _server->sendContent("</style></head><body>");
  
  // Send body content
  _server->sendContent("<h1>ESL Blaster Control Panel</h1>");
  
  // Quick actions
  _server->sendContent("<div class=\"quick-actions\">");
  _server->sendContent("<button id=\"statusBtn\" type=\"button\">Device Status</button>");
  _server->sendContent("<button id=\"restartBtn\" type=\"button\">Restart Device</button>");
  _server->sendContent("<button id=\"testFreqBtn\" type=\"button\">Test 1.25MHz</button>");
  _server->sendContent("</div>");
  
  // Tab container
  _server->sendContent("<div class=\"tab-container\"><div class=\"tabs\">");
  _server->sendContent("<button class=\"tab-button active\" data-target=\"ImageTab\">Image</button>");
  _server->sendContent("<button class=\"tab-button\" data-target=\"RawTab\">Raw Command</button>");
  _server->sendContent("<button class=\"tab-button\" data-target=\"SegmentTab\">Segments</button>");
  _server->sendContent("<button class=\"tab-button\" data-target=\"PingTab\">Ping/Refresh</button>");
  _server->sendContent("<button class=\"tab-button\" data-target=\"SettingsTab\">WiFi Settings</button>");
  _server->sendContent("<button class=\"tab-button\" data-target=\"AboutTab\">About</button>");
  _server->sendContent("</div>");
  
  // Image tab
  _server->sendContent("<div id=\"ImageTab\" class=\"tab-content active\">");
  _server->sendContent("<h2>Transmit Image to ESL</h2>");
  _server->sendContent("<form id=\"imageForm\" enctype=\"multipart/form-data\">");
  _server->sendContent("<div class=\"form-group\"><label for=\"barcode\">ESL Barcode (17 digits):</label>");
  _server->sendContent("<input type=\"text\" id=\"barcode\" name=\"barcode\" required pattern=\".{17,17}\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"imageFile\">Image File:</label>");
  _server->sendContent("<input type=\"file\" id=\"imageFile\" name=\"imageFile\" accept=\"image/*\" required></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"page\">Page (0-15):</label>");
  _server->sendContent("<input type=\"number\" id=\"page\" name=\"page\" min=\"0\" max=\"15\" value=\"0\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"colorMode\">Color Mode:</label>");
  _server->sendContent("<select id=\"colorMode\" name=\"colorMode\"><option value=\"0\">Black & White</option><option value=\"1\">Color</option></select></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"posX\">X Position:</label>");
  _server->sendContent("<input type=\"number\" id=\"posX\" name=\"posX\" min=\"0\" value=\"0\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"posY\">Y Position:</label>");
  _server->sendContent("<input type=\"number\" id=\"posY\" name=\"posY\" min=\"0\" value=\"0\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"forcePP4\">");
  _server->sendContent("<input type=\"checkbox\" id=\"forcePP4\" name=\"forcePP4\">Force PP4 Protocol</label></div>");
  _server->sendContent("<button type=\"submit\">Transmit Image</button></form></div>");
  
  // Raw Command tab
  _server->sendContent("<div id=\"RawTab\" class=\"tab-content\">");
  _server->sendContent("<h2>Send Raw Command</h2><form id=\"rawForm\">");
  _server->sendContent("<div class=\"form-group\"><label for=\"rawBarcode\">ESL Barcode (17 digits):</label>");
  _server->sendContent("<input type=\"text\" id=\"rawBarcode\" name=\"barcode\" required pattern=\".{17,17}\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"eslType\">ESL Type:</label>");
  _server->sendContent("<select id=\"eslType\" name=\"type\"><option value=\"DM\">Dot Matrix (DM)</option><option value=\"SEG\">Segment (SEG)</option></select></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"hexData\">Hex Data (without first byte and CRC):</label>");
  _server->sendContent("<textarea id=\"hexData\" name=\"hexData\" rows=\"4\" required></textarea></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"repeatCount\">Repeat Count:</label>");
  _server->sendContent("<input type=\"number\" id=\"repeatCount\" name=\"repeatCount\" min=\"1\" value=\"1\"></div>");
  _server->sendContent("<button type=\"submit\">Send Command</button></form></div>");
  
  // Segment tab
  _server->sendContent("<div id=\"SegmentTab\" class=\"tab-content\">");
  _server->sendContent("<h2>Set Segments</h2><form id=\"segmentForm\">");
  _server->sendContent("<div class=\"form-group\"><label for=\"segBarcode\">ESL Barcode (17 digits):</label>");
  _server->sendContent("<input type=\"text\" id=\"segBarcode\" name=\"barcode\" required pattern=\".{17,17}\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"bitmap\">Segment Bitmap (46 hex digits):</label>");
  _server->sendContent("<textarea id=\"bitmap\" name=\"bitmap\" rows=\"4\" required pattern=\"[0-9A-Fa-f]{46}\"></textarea></div>");
  _server->sendContent("<button type=\"submit\">Set Segments</button></form></div>");
  
  // Ping tab
  _server->sendContent("<div id=\"PingTab\" class=\"tab-content\">");
  _server->sendContent("<h2>Ping & Refresh ESLs</h2><form id=\"pingForm\">");
  _server->sendContent("<div class=\"form-group\"><label for=\"pingBarcode\">ESL Barcode (17 digits):</label>");
  _server->sendContent("<input type=\"text\" id=\"pingBarcode\" name=\"barcode\" required pattern=\".{17,17}\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"forcePP4Ping\">");
  _server->sendContent("<input type=\"checkbox\" id=\"forcePP4Ping\" name=\"forcePP4\">Force PP4 Protocol</label></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"repeatCountPing\">Repeat Count:</label>");
  _server->sendContent("<input type=\"number\" id=\"repeatCountPing\" name=\"repeatCount\" min=\"1\" value=\"400\"></div>");
  _server->sendContent("<button type=\"submit\">Send Ping</button></form>");
  _server->sendContent("<h2>Refresh Display</h2><form id=\"refreshForm\">");
  _server->sendContent("<div class=\"form-group\"><label for=\"refreshBarcode\">ESL Barcode (17 digits):</label>");
  _server->sendContent("<input type=\"text\" id=\"refreshBarcode\" name=\"barcode\" required pattern=\".{17,17}\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"forcePP4Refresh\">");
  _server->sendContent("<input type=\"checkbox\" id=\"forcePP4Refresh\" name=\"forcePP4\">Force PP4 Protocol</label></div>");
  _server->sendContent("<button type=\"submit\">Refresh Display</button></form></div>");
  
  // Settings tab
  _server->sendContent("<div id=\"SettingsTab\" class=\"tab-content\">");
  _server->sendContent("<h2>WiFi Settings</h2><form id=\"wifiForm\">");
  _server->sendContent("<div class=\"form-group\"><label for=\"wifiSsid\">WiFi SSID:</label>");
  _server->sendContent("<input type=\"text\" id=\"wifiSsid\" name=\"ssid\" required></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"wifiPassword\">WiFi Password:</label>");
  _server->sendContent("<input type=\"password\" id=\"wifiPassword\" name=\"password\"></div>");
  _server->sendContent("<div class=\"form-group\"><label for=\"apMode\">");
  _server->sendContent("<input type=\"checkbox\" id=\"apMode\" name=\"apMode\">Access Point Mode</label></div>");
  _server->sendContent("<button type=\"submit\">Save Settings</button></form></div>");
  
  // About tab
  _server->sendContent("<div id=\"AboutTab\" class=\"tab-content\">");
  _server->sendContent("<h2>About ESL Blaster</h2>");
  _server->sendContent("<p>ESL Blaster is a device for communicating with electronic shelf labels (ESLs) using infrared signals.</p>");
  _server->sendContent("<p><strong>Hardware Version:</strong> <span id=\"hwVersion\">Loading...</span></p>");
  _server->sendContent("<p><strong>Firmware Version:</strong> <span id=\"fwVersion\">Loading...</span></p>");
  _server->sendContent("<p><strong>Uptime:</strong> <span id=\"uptime\">Loading...</span></p>");
  _server->sendContent("<p><strong>Free Memory:</strong> <span id=\"freeHeap\">Loading...</span></p>");
  _server->sendContent("<p><strong>Build Date:</strong> 2025-03-23</p><p><strong>Last Update:</strong> 2025-03-23 05:47:25 UTC</p>");
  _server->sendContent("<p><strong>System User:</strong> BipBoopImportant</p>");
  _server->sendContent("<h3>Hardware Setup</h3><p>IR Transmitter connected to GPIO4 (D2)</p>");
  _server->sendContent("<p>SSD1306 OLED Shield on I2C (SDA/SCL)</p>");
  _server->sendContent("<h3>Credits</h3><p>Based on work by furrtek (furrtek.org)</p></div>");
  
  _server->sendContent("</div>"); // Close tab container
  
  // Status message div
  _server->sendContent("<div id=\"status-message\"></div>");
  
  // JavaScript - break into smaller sections to avoid memory issues
  _server->sendContent("<script>");
  // Part 1: Initial setup and functions
  _server->sendContent("(function() { console.log('Script loaded');");
  _server->sendContent("document.addEventListener('DOMContentLoaded', function() { console.log('DOM loaded'); initializeApp(); });");
  _server->sendContent("if (document.readyState === 'complete' || document.readyState === 'interactive') {");
  _server->sendContent("  console.log('DOM already loaded'); setTimeout(initializeApp, 1); }");
  
  // Part 2: Main app function
  _server->sendContent("function initializeApp() { try {");
  _server->sendContent("  const tabButtons = document.querySelectorAll('.tab-button');");
  _server->sendContent("  const tabContents = document.querySelectorAll('.tab-content');");
  _server->sendContent("  console.log('Tabs:', tabButtons.length, tabContents.length);");
  
  // Part 3: Tab functionality
  _server->sendContent("  tabButtons.forEach(function(button) {");
  _server->sendContent("    button.addEventListener('click', function() {");
  _server->sendContent("      const target = this.getAttribute('data-target');");
  _server->sendContent("      console.log('Tab:', target);");
  _server->sendContent("      tabButtons.forEach(function(btn) { btn.classList.remove('active'); });");
  _server->sendContent("      tabContents.forEach(function(content) { content.classList.remove('active'); });");
  _server->sendContent("      this.classList.add('active');");
  _server->sendContent("      const targetContent = document.getElementById(target);");
  _server->sendContent("      if (targetContent) { targetContent.classList.add('active');");
  _server->sendContent("        if (target === 'AboutTab') { updateAboutInfo(); }");
  _server->sendContent("      } else { console.error('Missing:', target); }");
  _server->sendContent("    });");
  _server->sendContent("  });");
  
  // Part 4: Status and form functions
  _server->sendContent("  function showStatus(message, isError) {");
  _server->sendContent("    console.log('Status:', message);");
  _server->sendContent("    const statusDiv = document.getElementById('status-message');");
  _server->sendContent("    if (!statusDiv) { console.error('Status div missing'); return; }");
  _server->sendContent("    statusDiv.textContent = message;");
  _server->sendContent("    statusDiv.className = isError ? 'error' : 'success';");
  _server->sendContent("    statusDiv.style.display = 'block';");
  _server->sendContent("    setTimeout(function() { statusDiv.style.display = 'none'; }, 5000);");
  _server->sendContent("  }");
  
  // Part 5: Forms and event handlers
  _server->sendContent("  const forms = {");
  _server->sendContent("    'imageForm': '/transmit-image',");
  _server->sendContent("    'rawForm': '/raw-command',");
  _server->sendContent("    'segmentForm': '/set-segments',");
  _server->sendContent("    'pingForm': '/ping',");
  _server->sendContent("    'refreshForm': '/refresh',");
  _server->sendContent("    'wifiForm': '/wifi-config'");
  _server->sendContent("  };");
  
  // Part 6: Form handlers
  _server->sendContent("  Object.keys(forms).forEach(function(formId) {");
  _server->sendContent("    const form = document.getElementById(formId);");
  _server->sendContent("    if (form) {");
  _server->sendContent("      form.addEventListener('submit', function(e) {");
  _server->sendContent("        e.preventDefault();");
  _server->sendContent("        if (formId === 'imageForm') { showStatus('Uploading...', false); }");
  _server->sendContent("        const formData = new FormData(this);");
  _server->sendContent("        fetch(forms[formId], { method: 'POST', body: formData })");
  _server->sendContent("          .then(function(response) { return response.json(); })");
  _server->sendContent("          .then(function(data) {");
  _server->sendContent("            if (data.success) {");
  _server->sendContent("              showStatus(data.message, false);");
  _server->sendContent("              if (formId === 'wifiForm' && data.success) {");
  _server->sendContent("                showStatus(data.message + ' Restarting...', false);");
  _server->sendContent("                setTimeout(function() { window.location.reload(); }, 5000);");
  _server->sendContent("              }");
  _server->sendContent("            } else { showStatus(data.error || 'Error', true); }");
  _server->sendContent("          })");
  _server->sendContent("          .catch(function(error) {");
  _server->sendContent("            console.error('Error:', error);");
  _server->sendContent("            showStatus('Network error: ' + error.message, true);");
  _server->sendContent("          });");
  _server->sendContent("      });");
  _server->sendContent("    } else { console.error('Form not found:', formId); }");
  _server->sendContent("  });");
  
  // Part 7: Quick action buttons
  _server->sendContent("  const statusBtn = document.getElementById('statusBtn');");
  _server->sendContent("  if (statusBtn) {");
  _server->sendContent("    statusBtn.addEventListener('click', function() {");
  _server->sendContent("      fetch('/status')");
  _server->sendContent("        .then(function(response) { return response.json(); })");
  _server->sendContent("        .then(function(data) {");
  _server->sendContent("          let statusMessage = 'Status:\\n';");
  _server->sendContent("          statusMessage += `WiFi: ${data.wifi_mode}\\n`;");
  _server->sendContent("          statusMessage += `Connected: ${data.connected}\\n`;");
  _server->sendContent("          statusMessage += `IP: ${data.ip}\\n`;");
  _server->sendContent("          statusMessage += `Uptime: ${formatUptime(data.uptime)}\\n`;");
  _server->sendContent("          statusMessage += `Free Heap: ${formatBytes(data.free_heap)}\\n`;");
  _server->sendContent("          statusMessage += `Frames Sent: ${data.frames_sent}\\n`;");
  _server->sendContent("          showStatus(statusMessage, false);");
  _server->sendContent("        })");
  _server->sendContent("        .catch(function(error) {");
  _server->sendContent("          showStatus('Network error: ' + error.message, true);");
  _server->sendContent("        });");
  _server->sendContent("    });");
  _server->sendContent("  }");
  
  // Part 8: Test frequency button
  _server->sendContent("  const testFreqBtn = document.getElementById('testFreqBtn');");
  _server->sendContent("  if (testFreqBtn) {");
  _server->sendContent("    testFreqBtn.addEventListener('click', function() {");
  _server->sendContent("      showStatus('Testing for 5 seconds...', false);");
  _server->sendContent("      fetch('/test-frequency')");
  _server->sendContent("        .then(function(response) { return response.json(); })");
  _server->sendContent("        .then(function(data) {");
  _server->sendContent("          showStatus(data.message, !data.success);");
  _server->sendContent("        })");
  _server->sendContent("        .catch(function(error) {");
  _server->sendContent("          showStatus('Error: ' + error.message, true);");
  _server->sendContent("        });");
  _server->sendContent("    });");
  _server->sendContent("  }");
  
  // Part 9: Restart button
  _server->sendContent("  const restartBtn = document.getElementById('restartBtn');");
  _server->sendContent("  if (restartBtn) {");
  _server->sendContent("    restartBtn.addEventListener('click', function() {");
  _server->sendContent("      if (confirm('Restart device?')) {");
  _server->sendContent("        fetch('/restart', { method: 'POST' })");
  _server->sendContent("          .then(function(response) { return response.json(); })");
  _server->sendContent("          .then(function(data) {");
  _server->sendContent("            showStatus(data.message, !data.success);");
  _server->sendContent("            if (data.success) {");
  _server->sendContent("              setTimeout(function() { window.location.reload(); }, 5000);");
  _server->sendContent("            }");
  _server->sendContent("          })");
  _server->sendContent("          .catch(function(error) {");
  _server->sendContent("            showStatus('Error: ' + error.message, true);");
  _server->sendContent("          });");
  _server->sendContent("      }");
  _server->sendContent("    });");
  _server->sendContent("  }");
  
  // Part 10: About tab info
  _server->sendContent("  function updateAboutInfo() {");
  _server->sendContent("    fetch('/status')");
  _server->sendContent("      .then(function(response) { return response.json(); })");
  _server->sendContent("      .then(function(data) {");
  _server->sendContent("        document.getElementById('hwVersion').textContent = data.hw_version || 'A';");
  _server->sendContent("        document.getElementById('fwVersion').textContent = data.fw_version || '1.0.0';");
  _server->sendContent("        document.getElementById('uptime').textContent = formatUptime(data.uptime);");
  _server->sendContent("        document.getElementById('freeHeap').textContent = formatBytes(data.free_heap);");
  _server->sendContent("      })");
  _server->sendContent("      .catch(function(error) {");
  _server->sendContent("        console.error('Error updating about info:', error);");
  _server->sendContent("      });");
  _server->sendContent("  }");
  
  // Part 11: Initialize and utility functions
  _server->sendContent("  if (document.querySelector('#AboutTab.active')) { updateAboutInfo(); }");
  _server->sendContent("  } catch (e) { console.error('Error:', e); }");
  _server->sendContent("}");
  
  // Part 12: Helper functions
  _server->sendContent("function formatUptime(seconds) {");
  _server->sendContent("  const days = Math.floor(seconds / 86400);");
  _server->sendContent("  seconds %= 86400;");
  _server->sendContent("  const hours = Math.floor(seconds / 3600);");
  _server->sendContent("  seconds %= 3600;");
  _server->sendContent("  const minutes = Math.floor(seconds / 60);");
  _server->sendContent("  seconds %= 60;");
  _server->sendContent("  let result = '';");
  _server->sendContent("  if (days > 0) result += days + ' days, ';");
  _server->sendContent("  return result + hours + ':' + minutes.toString().padStart(2, '0') + ':' + seconds.toString().padStart(2, '0');");
  _server->sendContent("}");
  
  _server->sendContent("function formatBytes(bytes) {");
  _server->sendContent("  if (bytes < 1024) return bytes + ' bytes';");
  _server->sendContent("  else if (bytes < 1048576) return (bytes / 1024).toFixed(2) + ' KB';");
  _server->sendContent("  else return (bytes / 1048576).toFixed(2) + ' MB';");
  _server->sendContent("}");
  
  // End script and HTML
  _server->sendContent("})();</script></body></html>");
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