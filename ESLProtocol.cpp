#include "ESLProtocol.h"

ESLProtocol::ESLProtocol(IRTransmitter* irTransmitter) {
  _irTransmitter = irTransmitter;
}

uint16_t ESLProtocol::calculateCRC16(uint8_t* data, uint16_t length) {
  uint16_t result = 0x8408;
  uint16_t poly = 0x8408;

  for (uint16_t i = 0; i < length; i++) {
    result ^= data[i];
    for (uint8_t bi = 0; bi < 8; bi++) {
      if (result & 1) {
        result >>= 1;
        result ^= poly;
      } else {
        result >>= 1;
      }
    }
  }

  return result;
}

void ESLProtocol::getPLIDFromBarcode(const char* barcode, uint8_t* PLID) {
  // Initialize PLID
  for (int i = 0; i < 4; i++) {
    PLID[i] = 0;
  }
  
  // Parse barcode if valid
  if (strlen(barcode) == 17) {
    // Extract first part: characters 2-6
    char part1[6];
    strncpy(part1, barcode + 2, 5);
    part1[5] = '\0';
    
    // Extract second part: characters 7-11
    char part2[6];
    strncpy(part2, barcode + 7, 5);
    part2[5] = '\0';
    
    // Convert to integers
    uint32_t val1 = atol(part1);
    uint32_t val2 = atol(part2);
    
    // Combined value as per the Python code
    uint32_t id_value = val1 + (val2 << 16);
    
    // Assign bytes to PLID as per the original code
    PLID[0] = (id_value >> 8) & 0xFF;
    PLID[1] = id_value & 0xFF;
    PLID[2] = (id_value >> 24) & 0xFF;
    PLID[3] = (id_value >> 16) & 0xFF;
  }
}

void ESLProtocol::appendWord(uint8_t* buffer, uint16_t offset, uint16_t value) {
  buffer[offset] = (value >> 8) & 0xFF;
  buffer[offset + 1] = value & 0xFF;
}

void ESLProtocol::createPingFrame(uint8_t* PLID, bool pp16, uint16_t repeats, 
                                 uint8_t* frameData, uint8_t* frameSize) {
  // Create a ping frame
  frameData[0] = 0x85;  // Protocol
  frameData[1] = PLID[3];
  frameData[2] = PLID[2];
  frameData[3] = PLID[1];
  frameData[4] = PLID[0];
  frameData[5] = 0x17;  // Command
  frameData[6] = 0x01;
  frameData[7] = 0x00;
  frameData[8] = 0x00;
  frameData[9] = 0x00;
  
  // Fill in the rest with 0x01
  for (int i = 0; i < 22; i++) {
    frameData[10 + i] = 0x01;
  }
  
  // Terminate the frame
  terminateFrame(frameData, 32, pp16, repeats, frameSize);
}

void ESLProtocol::createMCUFrame(uint8_t* PLID, uint8_t cmd, uint8_t* data, 
                                uint16_t dataLength, bool pp16, uint16_t repeats,
                                uint8_t* frameData, uint8_t* frameSize) {
  // Create an MCU frame
  frameData[0] = 0x85;  // Protocol
  frameData[1] = PLID[3];
  frameData[2] = PLID[2];
  frameData[3] = PLID[1];
  frameData[4] = PLID[0];
  frameData[5] = 0x34;
  frameData[6] = 0x00;
  frameData[7] = 0x00;
  frameData[8] = 0x00;
  frameData[9] = cmd;
  
  // Copy data if provided
  if (data != NULL && dataLength > 0) {
    memcpy(&frameData[10], data, dataLength);
  }
  
  // Terminate the frame
  terminateFrame(frameData, 10 + dataLength, pp16, repeats, frameSize);
}

void ESLProtocol::createRawFrame(uint8_t protocol, uint8_t* PLID, uint8_t cmd, 
                                uint8_t* data, uint16_t dataLength, bool pp16, 
                                uint16_t repeats, uint8_t* frameData, uint8_t* frameSize) {
  // Create a raw frame
  frameData[0] = protocol;
  frameData[1] = PLID[3];
  frameData[2] = PLID[2];
  frameData[3] = PLID[1];
  frameData[4] = PLID[0];
  frameData[5] = cmd;
  
  // Copy data if provided
  if (data != NULL && dataLength > 0) {
    memcpy(&frameData[6], data, dataLength);
  }
  
  // Terminate the frame
  terminateFrame(frameData, 6 + dataLength, pp16, repeats, frameSize);
}

void ESLProtocol::terminateFrame(uint8_t* frame, uint16_t frameLength, bool pp16, 
                                uint16_t repeats, uint8_t* frameSize) {
  // Calculate CRC
  uint16_t crc = calculateCRC16(frame, frameLength);
  
  // If PP16, prepend special header
  if (pp16) {
    // Shift the entire frame to make room for the header
    memmove(frame + 4, frame, frameLength);
    frame[0] = 0x00;
    frame[1] = 0x00;
    frame[2] = 0x00;
    frame[3] = 0x40;
    frameLength += 4;
  }
  
  // Append CRC
  frame[frameLength] = crc & 0xFF;
  frame[frameLength + 1] = (crc >> 8) & 0xFF;
  
  // Set the frame size to be returned
  *frameSize = frameLength + 2;
}

void ESLProtocol::compressImage(uint8_t* inputData, uint16_t width, uint16_t height, 
                               uint8_t* outputData, uint16_t* outputSize, bool colorMode) {
  uint16_t pixelCount = width * height;
  uint16_t totalPixels = colorMode ? pixelCount * 2 : pixelCount;
  uint16_t outputIdx = 0;
  
  // More memory-efficient RLE implementation
  for (uint16_t i = 0; i < totalPixels;) {
    // Get current pixel
    uint8_t runPixel = inputData[i++];
    outputData[outputIdx++] = runPixel;
    
    // Find run length
    uint16_t runCount = 1;
    while (i < totalPixels && inputData[i] == runPixel && runCount < 16383) {
      runCount++;
      i++;
    }
    
    // Encode run length if more than 1
    if (runCount > 1) {
      // Get bit count
      uint8_t bitCount = 0;
      uint16_t temp = runCount;
      
      while (temp) {
        bitCount++;
        temp >>= 1;
      }
      
      // Write zero bits for all but the first bit
      for (uint8_t j = 1; j < bitCount; j++) {
        outputData[outputIdx++] = 0;
      }
      
      // Write the bits from MSB to LSB
      for (int8_t j = bitCount - 1; j >= 0; j--) {
        outputData[outputIdx++] = (runCount >> j) & 1;
      }
    }
    
    // Allow for background tasks
    if (i % 256 == 0) yield();
  }
  
  *outputSize = outputIdx;
}

bool ESLProtocol::transmitImage(const char* barcodeStr, uint8_t* imageData, 
                               uint16_t width, uint16_t height, uint8_t page, 
                               bool colorMode, uint16_t posX, uint16_t posY,
                               bool forcePP4) {
  // Implementation of img2dm.py functionality
  uint8_t PLID[4];
  getPLIDFromBarcode(barcodeStr, PLID);
  
  bool pp16 = !forcePP4;
  
  // Prepare for image compression
  uint16_t pixelCount = width * height;
  
  // ESLs only accept images with pixel counts multiple of 8
  if (pixelCount & 7) {
    Serial.println(F("Image pixel count must be a multiple of 8"));
    return false;
  }
  
  // Allocate memory once and reuse for all operations
  uint32_t maxSize = pixelCount * (colorMode ? 2 : 1);
  uint8_t* compressedData = nullptr;
  uint8_t* finalData = nullptr;
  bool success = false;
  
  // Start with a modest allocation for compressed data
  compressedData = (uint8_t*)malloc(pixelCount * 1.5); // Typical compression is 50%
  
  if (!compressedData) {
    Serial.println(F("Memory allocation failed for compressed data"));
    return false;
  }
  
  uint16_t compressedSize = 0;
  compressImage(imageData, width, height, compressedData, &compressedSize, colorMode);
  
  // Decide whether to use compression based on size
  uint8_t compressionType;
  
  if (compressedSize < maxSize) {
    Serial.printf("Compression ratio: %.1f%% (%d -> %d bytes)\n", 
                 100 - ((compressedSize * 100.0f) / maxSize), 
                 maxSize, compressedSize);
    finalData = compressedData;
    compressedSize = (compressedSize + 7) & ~7; // Round up to multiple of 8
    compressionType = 2; // Zero-length coding
  } else {
    Serial.println(F("Compression ineffective, using raw data"));
    finalData = imageData;
    compressedSize = maxSize;
    compressionType = 0; // Raw data
  }
  
  // Calculate frames needed
  const int bytesPerFrame = 20;
  int frameCount = (compressedSize + bytesPerFrame - 1) / bytesPerFrame;
  
  // Make all frames and parameters first, then transmit
  // This allows retry without recalculating
  uint8_t pingFrame[64];
  uint8_t paramFrame[64];
  uint8_t* dataFrames = (uint8_t*)malloc(frameCount * 32); // Max 32 bytes per frame
  uint8_t refreshFrame[64];
  uint8_t pingSize, paramSize, refreshSize;
  uint8_t* dataSizes = (uint8_t*)malloc(frameCount);
  
  if (!dataFrames || !dataSizes) {
    Serial.println(F("Memory allocation failed for frame buffers"));
    free(compressedData);
    if (dataFrames) free(dataFrames);
    if (dataSizes) free(dataSizes);
    return false;
  }
  
  // 1. Prepare wake-up ping frame
  createPingFrame(PLID, pp16, 400, pingFrame, &pingSize);
  
  // 2. Prepare parameters frame
  uint8_t paramData[32] = {0};
  
  // Total byte count
  appendWord(paramData, 0, compressedSize / 8);
  paramData[2] = 0x00;              // Unused
  paramData[3] = compressionType;   // Compression type
  paramData[4] = page;              // Page number
  appendWord(paramData, 5, width);  // Width
  appendWord(paramData, 7, height); // Height
  appendWord(paramData, 9, posX);   // X position
  appendWord(paramData, 11, posY);  // Y position
  appendWord(paramData, 13, 0x0000);// Keycode
  paramData[15] = 0x88;             // 0x80 = update, 0x08 = set base page
  appendWord(paramData, 16, 0x0000);// Enabled pages
  paramData[18] = 0x00;
  paramData[19] = 0x00;
  paramData[20] = 0x00;
  paramData[21] = 0x00;
  
  createMCUFrame(PLID, 0x05, paramData, 22, pp16, 1, paramFrame, &paramSize);
  
  // 3. Prepare data frames
  for (int fr = 0; fr < frameCount; fr++) {
    uint8_t dataFrameData[24] = {0};
    appendWord(dataFrameData, 0, fr); // Frame number
    
    // Calculate how many bytes to copy for this frame
    int bytesToCopy = min((int)bytesPerFrame, (int)(compressedSize - (fr * bytesPerFrame)));
    
    // Copy data
    memcpy(&dataFrameData[2], &finalData[fr * bytesPerFrame], bytesToCopy);
    
    createMCUFrame(PLID, 0x20, dataFrameData, 2 + bytesToCopy, pp16, 1, 
                  &dataFrames[fr * 32], &dataSizes[fr]);
  }
  
  // 4. Prepare refresh frame
  uint8_t refreshData[22];
  memset(refreshData, 0, sizeof(refreshData));
  createMCUFrame(PLID, 0x01, refreshData, 22, pp16, 1, refreshFrame, &refreshSize);
  
  // Now transmit with retry capability
  int maxRetries = 3;
  int currentRetry = 0;
  bool transmitSuccess = false;
  
  while (currentRetry < maxRetries && !transmitSuccess) {
    if (currentRetry > 0) {
      Serial.printf("Retry %d of %d...\n", currentRetry, maxRetries);
    }
    
    // 1. Send wake-up ping frame
    _irTransmitter->transmitFrame(pingFrame, pingSize, 400);
    yield();
    
    // 2. Send parameters frame
    _irTransmitter->transmitFrame(paramFrame, paramSize, 1);
    yield();
    
    // 3. Send data frames
    bool dataFramesFailed = false;
    for (int fr = 0; fr < frameCount; fr++) {
      _irTransmitter->transmitFrame(&dataFrames[fr * 32], dataSizes[fr], 1);
      
      // Check for busy state (indicates transmission issue)
      if (_irTransmitter->isBusy()) {
        Serial.printf("Frame %d transmission error\n", fr);
        dataFramesFailed = true;
        break;
      }
      
      yield();
    }
    
    if (dataFramesFailed) {
      currentRetry++;
      delay(100 * currentRetry); // Gradually increase delay between retries
      continue;
    }
    
    // 4. Send refresh frame
    _irTransmitter->transmitFrame(refreshFrame, refreshSize, 1);
    
    // If we made it here, everything transmitted successfully
    transmitSuccess = true;
  }
  
  // Clean up
  free(compressedData);
  free(dataFrames);
  free(dataSizes);
  
  success = transmitSuccess;
  
  if (!success) {
    Serial.println(F("Failed to transmit image after retries"));
  }
  
  return success;
}

bool ESLProtocol::transmitRawCommand(const char* barcodeStr, const char* typeStr, 
                                   uint8_t* frameData, uint16_t dataSize, uint16_t repeatCount) {
  // Implementation of rawcmd.py functionality
  uint8_t PLID[4];
  getPLIDFromBarcode(barcodeStr, PLID);
  
  uint8_t protocol = (strcmp(typeStr, "DM") == 0) ? 0x85 : 0x84;
  uint8_t cmd = frameData[0];
  
  uint8_t completeFrame[256];
  uint8_t frameSize;
  
  createRawFrame(protocol, PLID, cmd, &frameData[1], dataSize - 1, false, repeatCount, completeFrame, &frameSize);
  _irTransmitter->transmitFrame(completeFrame, frameSize, repeatCount);
  
  return true;
}

bool ESLProtocol::setSegments(const char* barcodeStr, uint8_t* bitmap) {
  // Implementation of setsegs.py functionality
  uint8_t PLID[4];
  getPLIDFromBarcode(barcodeStr, PLID);
  
  // Create payload for segment ESL
  uint8_t payload[40] = {0};
  payload[0] = 0xBA;
  payload[1] = 0x00;
  payload[2] = 0x00;
  payload[3] = 0x00;
  
  // Copy bitmap (23 bytes)
  memcpy(&payload[4], bitmap, 23);
  
  // Calculate segment bitmap CRC
  uint16_t segcrc = calculateCRC16(bitmap, 23);
  payload[27] = segcrc & 0xFF;
  payload[28] = (segcrc >> 8) & 0xFF;
  
  // Page number, duration and other fields
  payload[29] = 0x00;
  payload[30] = 0x00;
  payload[31] = 0x09;
  payload[32] = 0x00;
  payload[33] = 0x10;
  payload[34] = 0x00;
  payload[35] = 0x31;
  
  uint8_t completeFrame[256];
  uint8_t frameSize;
  
  createRawFrame(0x84, PLID, payload[0], &payload[1], 35, false, 100, completeFrame, &frameSize);
  _irTransmitter->transmitFrame(completeFrame, frameSize, 100);
  
  return true;
}

bool ESLProtocol::makePingFrame(const char* barcodeStr, bool pp16, uint16_t repeats) {
  uint8_t PLID[4];
  getPLIDFromBarcode(barcodeStr, PLID);
  
  uint8_t frameData[256];
  uint8_t frameSize;
  
  createPingFrame(PLID, pp16, repeats, frameData, &frameSize);
  _irTransmitter->transmitFrame(frameData, frameSize, repeats);
  
  return true;
}

bool ESLProtocol::makeRefreshFrame(const char* barcodeStr, bool pp16) {
  uint8_t PLID[4];
  getPLIDFromBarcode(barcodeStr, PLID);
  
  uint8_t frameData[256];
  uint8_t frameSize;
  
  uint8_t refreshData[22];
  for (int i = 0; i < 22; i++) {
    refreshData[i] = 0x00;
  }
  
  createMCUFrame(PLID, 0x01, refreshData, 22, pp16, 1, frameData, &frameSize);
  _irTransmitter->transmitFrame(frameData, frameSize, 1);
  
  return true;
}