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
  // This is an implementation of run-length encoding similar to the Python script
  uint16_t pixelCount = width * height;
  uint16_t totalPixels = colorMode ? pixelCount * 2 : pixelCount;
  uint16_t outputIdx = 0;
  
  // First pixel value
  uint8_t runPixel = inputData[0];
  outputData[outputIdx++] = runPixel;
  
  uint16_t runCount = 1;
  
  // Process each pixel
  for (uint16_t i = 1; i < totalPixels; i++) {
    if (inputData[i] == runPixel) {
      // Continue the run
      runCount++;
    } else {
      // End of run, encode the count
      // Zero length coding - each bit preceded by a 0, except the first bit
      uint8_t bits[16]; // More than enough for 16-bit integer
      uint8_t bitCount = 0;
      
      // Convert run count to binary
      uint16_t tempCount = runCount;
      while (tempCount) {
        bits[bitCount++] = tempCount & 1;
        tempCount >>= 1;
      }
      
      // Reverse the bits (they were added in reverse order)
      for (uint8_t j = 0; j < bitCount / 2; j++) {
        uint8_t temp = bits[j];
        bits[j] = bits[bitCount - j - 1];
        bits[bitCount - j - 1] = temp;
      }
      
      // Output the zero-length encoded bits
      for (uint8_t j = 1; j < bitCount; j++) {
        outputData[outputIdx++] = 0; // Zero prefix for all bits except first
      }
      
      // Output the actual bits
      for (uint8_t j = 0; j < bitCount; j++) {
        outputData[outputIdx++] = bits[j];
      }
      
      // Start a new run
      runPixel = inputData[i];
      runCount = 1;
    }
  }
  
  // Encode the final run if there is one
  if (runCount > 1) {
    // Zero length coding as above
    uint8_t bits[16];
    uint8_t bitCount = 0;
    
    uint16_t tempCount = runCount;
    while (tempCount) {
      bits[bitCount++] = tempCount & 1;
      tempCount >>= 1;
    }
    
    for (uint8_t j = 0; j < bitCount / 2; j++) {
      uint8_t temp = bits[j];
      bits[j] = bits[bitCount - j - 1];
      bits[bitCount - j - 1] = temp;
    }
    
    for (uint8_t j = 1; j < bitCount; j++) {
      outputData[outputIdx++] = 0;
    }
    
    for (uint8_t j = 0; j < bitCount; j++) {
      outputData[outputIdx++] = bits[j];
    }
  }
  
  // Set final output size
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
    Serial.println("Image pixel count must be a multiple of 8");
    return false;
  }
  
  // Prepare raw and compressed data buffers
  uint8_t* rawPixels = new uint8_t[pixelCount * (colorMode ? 2 : 1)];
  if (!rawPixels) {
    Serial.println("Memory allocation failed for raw pixels");
    return false;
  }
  
  // Convert image data to bit array
  // This is simplified - in a real implementation you'd process the actual image data
  for (uint16_t i = 0; i < pixelCount; i++) {
    // For simplicity, we'll just copy the provided data
    rawPixels[i] = imageData[i];
  }
  
  // If color mode, append a second pass
  if (colorMode) {
    for (uint16_t i = 0; i < pixelCount; i++) {
      // This would be the color extraction logic in a real implementation
      rawPixels[pixelCount + i] = imageData[pixelCount + i];
    }
  }
  
  // Compress the image data
  uint8_t* compressedData = new uint8_t[pixelCount * 2]; // Worst case size
  if (!compressedData) {
    delete[] rawPixels;
    Serial.println("Memory allocation failed for compressed data");
    return false;
  }
  
  uint16_t compressedSize = 0;
  compressImage(rawPixels, width, height, compressedData, &compressedSize, colorMode);
  
  // Decide whether to use compression based on size
  uint8_t* finalData;
  uint16_t finalSize;
  uint8_t compressionType;
  
  if (compressedSize < pixelCount * (colorMode ? 2 : 1)) {
    Serial.printf("Compression ratio: %.1f%% (%d -> %d bytes)\n", 
                 100 - ((compressedSize * 100) / float(pixelCount * (colorMode ? 2 : 1))), 
                 pixelCount * (colorMode ? 2 : 1), compressedSize);
    finalData = compressedData;
    finalSize = compressedSize;
    compressionType = 2; // Zero-length coding
  } else {
    Serial.println("Compression ratio poor, using raw data");
    finalData = rawPixels;
    finalSize = pixelCount * (colorMode ? 2 : 1);
    compressionType = 0; // Raw data
  }
  
  // Calculate frames needed
  const int bytesPerFrame = 20;
  int frameCount = (finalSize + bytesPerFrame - 1) / bytesPerFrame;
  
  // Prepare to send frames
  uint8_t frameData[256];
  uint8_t frameSize;
  
  // 1. Wake-up ping frame
  createPingFrame(PLID, pp16, 400, frameData, &frameSize);
  _irTransmitter->transmitFrame(frameData, frameSize, 400);
  yield();
  
  // 2. Parameters frame
  uint8_t paramData[32] = {0};
  
  // Total byte count
  appendWord(paramData, 0, finalSize / 8);
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
  
  createMCUFrame(PLID, 0x05, paramData, 22, pp16, 1, frameData, &frameSize);
  _irTransmitter->transmitFrame(frameData, frameSize, 1);
  yield();
  
  // 3. Data frames
  for (int fr = 0; fr < frameCount; fr++) {
    uint8_t dataFrameData[24] = {0};
    appendWord(dataFrameData, 0, fr); // Frame number
    
    // Calculate how many bytes to copy for this frame
    int bytesToCopy = min((int)bytesPerFrame, (int)(finalSize - (fr * bytesPerFrame)));
    
    // Copy data
    memcpy(&dataFrameData[2], &finalData[fr * bytesPerFrame], bytesToCopy);
    
    createMCUFrame(PLID, 0x20, dataFrameData, 2 + bytesToCopy, pp16, 1, frameData, &frameSize);
    _irTransmitter->transmitFrame(frameData, frameSize, 1);
    yield();
  }
  
  // 4. Refresh frame
  uint8_t refreshData[22];
  for (int i = 0; i < 22; i++) {
    refreshData[i] = 0x00;
  }
  
  createMCUFrame(PLID, 0x01, refreshData, 22, pp16, 1, frameData, &frameSize);
  _irTransmitter->transmitFrame(frameData, frameSize, 1);
  
  // Clean up
  delete[] rawPixels;
  delete[] compressedData;
  
  return true;
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