#include "IRTransmitter.h"
#include <user_interface.h>  // For system_update_cpu_freq()

IRTransmitter::IRTransmitter(int pin) {
  _irPin = pin;
  _busy = false;
  _pinMask = (1 << pin); // Prepare pin mask for direct port manipulation
}

void IRTransmitter::begin() {
  pinMode(_irPin, OUTPUT);
  digitalWrite(_irPin, LOW);
}

// Optimized high-precision burst generation for 1.25MHz signal
// Using ICACHE_RAM_ATTR to ensure code runs from RAM for consistent timing
void ICACHE_RAM_ATTR IRTransmitter::sendBurst(int durationUs) {
  // Calculate cycle count for duration
  // At 1.25MHz, each full cycle takes 0.8μs
  uint32_t cycles = (durationUs * 1250) / 1000;
  
  // Save current CPU frequency and set to maximum
  uint32_t oldCPUFreq = system_get_cpu_freq();
  system_update_cpu_freq(160); // Set CPU to 160MHz for most precise timing
  
  // Save interrupt state and disable interrupts
  uint32_t savedInterruptState = xt_rsil(15); // Disable all interrupts
  
  // Each cycle is 0.8μs (1/1.25MHz)
  // At 160MHz, each machine cycle is 6.25ns
  // So we need 128 cycles per IR cycle (0.8μs / 6.25ns)
  // With 50% duty cycle, that's 64 cycles each for HIGH and LOW
  
  for (uint32_t i = 0; i < cycles; i++) {
    // Set pin HIGH
    GPOS = _pinMask;
    
    // Delay for exactly 0.4μs (half of 1.25MHz cycle) = 64 cycles at 160MHz
    // Use inline assembly for precise timing
    __asm__ __volatile__(
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop" // 54 nops + overhead ≈ 64 cycles
    );
    
    // Set pin LOW
    GPOC = _pinMask;
    
    // Delay for exactly 0.4μs again
    __asm__ __volatile__(
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop" // 54 nops + overhead ≈ 64 cycles
    );
  }
  
  // Restore interrupt state and CPU frequency
  xt_wsr_ps(savedInterruptState);
  system_update_cpu_freq(oldCPUFreq);
}

// Send pause with different durations based on symbol value
// Using ICACHE_RAM_ATTR to ensure code runs from RAM for consistent timing
void ICACHE_RAM_ATTR IRTransmitter::sendPause(uint8_t symbol) {
  uint32_t pauseTime;
  
  switch(symbol & 3) {
    case 0:
      pauseTime = 56;
      break;
    case 1:
      pauseTime = 237;
      break;
    case 2:
      pauseTime = 117;
      break;
    case 3:
      pauseTime = 178;
      break;
  }
  
  // More precise delay than delayMicroseconds for critical timing
  uint32_t start = ESP.getCycleCount();
  uint32_t target = start + (pauseTime * 160); // Convert μs to cycles at 160MHz
  
  while (ESP.getCycleCount() < target) {
    // Wait precisely
    __asm__ __volatile__("nop"); // Prevent optimization from removing the loop
  }
}

// Transmit a single frame with the specified repeat count
void IRTransmitter::transmitFrame(uint8_t* buffer, uint8_t dataSize, uint16_t repeat) {
  _busy = true;
  
  uint16_t sym_count = dataSize << 2;  // 1 byte = 4 symbols (2 bits per symbol)
  
  for (uint16_t r = 0; r < repeat; r++) {
    for (uint16_t s = 0; s < sym_count; s++) {
      uint8_t byte = buffer[s >> 2];  // Load new byte every 4 symbols
      uint8_t symbol = (byte >> (6 - ((s & 3) << 1))) & 3;  // Extract 2-bit symbol
      
      // Send burst
      sendBurst(39);
      
      // Send symbol pause
      sendPause(symbol);
      
      // Allow the ESP8266 to perform background tasks every 32 symbols
      // This helps maintain WiFi connection during long transmissions
      if ((s & 0x1F) == 0) {
        yield();
      }
    }
    
    // Final burst
    sendBurst(39);
    
    // Inter-frame delay
    delayMicroseconds(2000);
    
    // Allow ESP8266 to handle background tasks between frames
    yield();
  }
  
  _busy = false;
}

// Transmit multiple frames
void IRTransmitter::transmitFrames(uint8_t** frames, uint8_t* sizes, uint16_t* repeats, uint8_t frameCount) {
  for (uint8_t i = 0; i < frameCount; i++) {
    transmitFrame(frames[i], sizes[i], repeats[i]);
    yield();  // Allow ESP8266 to handle background tasks
  }
}

bool IRTransmitter::isBusy() {
  return _busy;
}

// Test function for verifying 1.25MHz frequency
void IRTransmitter::testFrequency() {
  Serial.println("Generating 1.25MHz test signal for 5 seconds");
  
  // Set pin directly
  pinMode(_irPin, OUTPUT);
  
  // Set to maximum CPU frequency
  uint32_t oldCPUFreq = system_get_cpu_freq();
  system_update_cpu_freq(160);
  
  // Disable interrupts for clean signal
  uint32_t savedInterruptState = xt_rsil(15);
  
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    // Direct port manipulation
    GPOS = _pinMask;
    __asm__ __volatile__(
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop"
    );
    
    GPOC = _pinMask;
    __asm__ __volatile__(
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop"
    );
  }
  
  // Restore interrupts
  xt_wsr_ps(savedInterruptState);
  system_update_cpu_freq(oldCPUFreq);
  
  Serial.println("Test complete");
}