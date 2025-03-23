#include "IRTransmitter.h"
#include <user_interface.h>  // For system_update_cpu_freq()
#include <Ticker.h>          // For background processing

IRTransmitter::IRTransmitter(int pin) {
  _irPin = pin;
  _busy = false;
  _pinMask = (1 << pin); // Prepare pin mask for direct port manipulation
}

void IRTransmitter::begin() {
  pinMode(_irPin, OUTPUT);
  digitalWrite(_irPin, LOW);
  
  // Initialize TX queue
  for (int i = 0; i < MAX_TX_QUEUE; i++) {
    txQueue[i].buffer = nullptr;
    txQueue[i].inUse = false;
  }
}

// Improved IR timing patterns
constexpr uint8_t nop10[] = {
  0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};

// New timeout implementation for more accurate delays
class CpuCycleTimer {
  private:
    uint32_t targetCycles;
  
  public:
    void start(uint32_t us) {
      targetCycles = ESP.getCycleCount() + (us * 160); // 160MHz = 160 cycles per µs
    }
    
    bool expired() {
      return ESP.getCycleCount() >= targetCycles;
    }
    
    void waitForExpiry() {
      while (!expired()) {
        // Tight loop - use asm to prevent optimization
        asm volatile ("nop");
      }
    }
};

// Optimized burst generation
void ICACHE_RAM_ATTR IRTransmitter::sendBurst(int durationUs) {
  // Calculate cycle count for duration
  // At 1.25MHz, each full cycle takes 0.8μs
  uint32_t cycles = (durationUs * 1250) / 1000;
  
  // Save current CPU frequency and set to maximum
  uint32_t oldCPUFreq = system_get_cpu_freq();
  system_update_cpu_freq(160); // Set CPU to 160MHz for most precise timing
  
  // Save interrupt state and disable interrupts
  uint32_t savedInterruptState = xt_rsil(15); // Disable all interrupts
  
  // Pre-calculate ON and OFF times for better loop performance
  uint32_t onCycles = 64;  // 0.4µs at 160MHz
  uint32_t offCycles = 64; // 0.4µs at 160MHz
  
  for (uint32_t i = 0; i < cycles; i++) {
    // Set pin HIGH
    GPOS = _pinMask;
    
    // Delay for exactly 0.4μs (half of 1.25MHz cycle) = 64 cycles at 160MHz
    uint32_t startCycle = ESP.getCycleCount();
    while (ESP.getCycleCount() - startCycle < onCycles) {
      asm volatile ("nop");
    }
    
    // Set pin LOW
    GPOC = _pinMask;
    
    // Delay for exactly 0.4μs again
    startCycle = ESP.getCycleCount();
    while (ESP.getCycleCount() - startCycle < offCycles) {
      asm volatile ("nop");
    }
  }
  
  // Restore interrupt state and CPU frequency
  xt_wsr_ps(savedInterruptState);
  system_update_cpu_freq(oldCPUFreq);
}

// More precise pause timing
void ICACHE_RAM_ATTR IRTransmitter::sendPause(uint8_t symbol) {
  // Calculate precise pause times for better timing accuracy
  static const uint32_t pauseTimes[4] = {56, 237, 117, 178};
  uint32_t pauseTime = pauseTimes[symbol & 3];
  
  CpuCycleTimer timer;
  timer.start(pauseTime);
  timer.waitForExpiry();
}

// Transmit a single frame with the specified repeat count
void IRTransmitter::transmitFrame(uint8_t* buffer, uint8_t dataSize, uint16_t repeat) {
  _busy = true;
  
  // Update transmission counter
  extern unsigned long totalFramesSent;
  totalFramesSent++;
  
  uint16_t sym_count = dataSize << 2;  // 1 byte = 4 symbols (2 bits per symbol)
  
  // Pre-calculate symbol masks for faster extraction
  static const uint8_t symbolMasks[4] = {0xC0, 0x30, 0x0C, 0x03};
  static const uint8_t symbolShifts[4] = {6, 4, 2, 0};
  
  // Use a more efficient loop
  for (uint16_t r = 0; r < repeat; r++) {
    for (uint16_t s = 0; s < sym_count; s++) {
      uint8_t byteIndex = s >> 2;     // Divide by 4 to get byte index
      uint8_t symbolIndex = s & 0x03; // Modulo 4 to get position within byte
      uint8_t byte = buffer[byteIndex];
      
      // Extract symbol using pre-calculated masks and shifts
      uint8_t symbol = (byte & symbolMasks[symbolIndex]) >> symbolShifts[symbolIndex];
      
      // Send burst
      sendBurst(39);
      
      // Send symbol pause
      sendPause(symbol);
      
      // Allow the ESP8266 to perform background tasks every 32 symbols
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

// Add TX queue functionality to handle multiple frames
#define MAX_TX_QUEUE 5

struct TxQueueItem {
  uint8_t* buffer;
  uint8_t size;
  uint16_t repeats;
  bool inUse;
};

static TxQueueItem txQueue[MAX_TX_QUEUE];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;
static volatile bool queueProcessing = false;
static Ticker txTicker;

// Process queue in background
void ICACHE_RAM_ATTR IRTransmitter::processTxQueue() {
  if (queueProcessing || queueHead == queueTail) {
    return; // Already processing or queue empty
  }
  
  queueProcessing = true;
  
  // Get next item from queue
  TxQueueItem* item = &txQueue[queueHead];
  
  if (item->inUse && item->buffer != nullptr) {
    // Process this item
    transmitFrame(item->buffer, item->size, item->repeats);
    
    // Free the buffer
    free(item->buffer);
    item->buffer = nullptr;
    item->inUse = false;
  }
  
  // Move head pointer
  queueHead = (queueHead + 1) % MAX_TX_QUEUE;
  
  queueProcessing = false;
  
  // Check if more items in queue
  if (queueHead != queueTail) {
    // Schedule next processing
    txTicker.once_ms(10, std::bind(&IRTransmitter::processTxQueue, this));
  }
}

// Add item to queue
bool IRTransmitter::queueFrame(uint8_t* data, uint8_t size, uint16_t repeats) {
  // Check if queue is full
  uint8_t nextTail = (queueTail + 1) % MAX_TX_QUEUE;
  if (nextTail == queueHead) {
    return false; // Queue full
  }
  
  // Allocate buffer for data
  uint8_t* buffer = (uint8_t*)malloc(size);
  if (!buffer) {
    return false; // Memory allocation failed
  }
  
  // Copy data to buffer
  memcpy(buffer, data, size);
  
  // Add to queue
  txQueue[queueTail].buffer = buffer;
  txQueue[queueTail].size = size;
  txQueue[queueTail].repeats = repeats;
  txQueue[queueTail].inUse = true;
  
  // Update tail pointer
  queueTail = nextTail;
  
  // Start processing if not already running
  if (!queueProcessing) {
    txTicker.once_ms(0, std::bind(&IRTransmitter::processTxQueue, this));
  }
  
  return true;
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