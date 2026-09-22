/*
    ESP-NOW LED Master Controller
    
    This device sends LED control commands via ESP-NOW to the LED slave device.
    Commands are sent through Serial interface and forwarded via ESP-NOW.
*/

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

/* Definitions */
#define ESPNOW_WIFI_CHANNEL 1
#define MAX_COMMAND_LENGTH 240

/* LED Command Structure */
typedef struct {
  char command[MAX_COMMAND_LENGTH];
  uint8_t checksum;
} LEDCommand;

/* Global Variables */
// Broadcast MAC address - sends to all listening devices
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// You can also use a specific MAC address if you know the slave's MAC
// uint8_t slaveAddress[] = {0x24, 0x62, 0xAB, 0xD2, 0x34, 0x56};

/* Callback Functions */
// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("✓ Command sent successfully");
  } else {
    Serial.println("✗ Command send failed");
  }
}

// Callback when data is received (for acknowledgments)
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  Serial.printf("Received response: %.*s\n", len, incomingData);
}

/* Send LED Command Function */
bool sendLEDCommand(const String& cmd) {
  LEDCommand ledCmd;
  memset(&ledCmd, 0, sizeof(ledCmd));
  
  // Copy command (truncate if too long)
  strncpy(ledCmd.command, cmd.c_str(), MAX_COMMAND_LENGTH - 1);
  ledCmd.command[MAX_COMMAND_LENGTH - 1] = '\0';
  
  // Calculate simple checksum
  ledCmd.checksum = 0;
  for (int i = 0; i < strlen(ledCmd.command); i++) {
    ledCmd.checksum ^= ledCmd.command[i];
  }
  
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &ledCmd, sizeof(ledCmd));
  
  if (result == ESP_OK) {
    return true;
  } else {
    Serial.println("Error sending command: " + String(result));
    return false;
  }
}

/* Setup */
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register for a callback function that will be called when data is sent
  esp_now_register_send_cb(OnDataSent);
  
  // Register for a callback function that will be called when data is received
  esp_now_register_recv_cb(OnDataRecv);

  // Register peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;  
  peerInfo.encrypt = false;

  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("ESP-NOW LED Master Controller");
  Serial.println("============================");
  Serial.println("MAC Address: " + WiFi.macAddress());

  Serial.println("\nSetup complete!");
  Serial.println("\n=== LED Control Commands ===");
  Serial.println("Enter any LED command (will be forwarded via ESP-NOW):");
  Serial.println("Examples:");
  Serial.println("  BRIGHT=100");
  Serial.println("  ANIM=3");
  Serial.println("  SPEED=50");
  Serial.println("  CLEAR");
  Serial.println("  FILL");
  Serial.println("  WORD=255,0,0");
  Serial.println("  MANUAL");
  Serial.println("  STATUS");
  Serial.println("============================\n");
}

/* Main Loop */
void loop() {
  // Check for serial commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      Serial.println("Sending command: " + command);
      
      // Send command via ESP-NOW
      if (!sendLEDCommand(command)) {
        Serial.println("Failed to send command!");
      }
    }
  }
  
  delay(10); // Small delay to prevent watchdog issues
}

/* Alternative function to send commands programmatically */
void sendProgrammaticCommand(const String& cmd) {
  Serial.println("Programmatic command: " + cmd);
  sendLEDCommand(cmd);
}

/* Example of automated command sequences */
void runDemoSequence() {
  Serial.println("Running demo sequence...");
  
  sendLEDCommand("CLEAR");
  delay(1000);
  
  sendLEDCommand("BRIGHT=100");
  delay(500);
  
  sendLEDCommand("ANIM=3");
  delay(500);
  
  sendLEDCommand("PLAY");
  delay(5000);
  
  sendLEDCommand("ANIM=0");
  delay(3000);
  
  sendLEDCommand("WORD=255,0,0");
  delay(2000);
  
  sendLEDCommand("CLEAR");
  Serial.println("Demo sequence complete!");
}