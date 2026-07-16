#include <BleKeyboard.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_arduino_version.h>

// =========================
// Station 4
// - empfängt Signale vom Haupt-ESP über ESP-NOW
// - sendet die zugehörigen Tasten per Bluetooth an das iPad
// =========================

BleKeyboard bleKeyboard("Station 4 Trigger", "Maker", 100);

struct Message {
  char key;
};

volatile bool keyReceived = false;
volatile char receivedKey = ' ';

// =========================
// Empfangene Signale verarbeiten
// =========================
bool isStation4GameCommand(char key) {
  switch (key) {
    case '0':  // Neutral-Video
    case '1':  // Positiv-Video
    case '2':  // Negativ-Video
    case 'c':  // Aktion in Sun-Phase
    case 'd':  // Aktion in Rain-Phase
    case 'w':  // Spiel gewonnen
    case 'm':  // Verschimmelt
    case 'v':  // Vertrocknet
    case 'u':  // Timeout Station 4
      return true;

    default:
      return false;
  }
}

char toLowerCommand(char key) {
  if (key >= 'A' && key <= 'Z') {
    return key + ('a' - 'A');
  }

  return key;
}

void sendBluetoothKey(char key) {
  if (bleKeyboard.isConnected()) {
    bleKeyboard.write(key);

    Serial.print("Per Bluetooth an das iPad gesendet: ");
    Serial.println(key);
  } else {
    Serial.print("iPad nicht verbunden. Nicht gesendet: ");
    Serial.println(key);
  }
}

void processCommand(char command) {
  // Das interne Signal y kommt vom zweiten Abstandssensor am Haupt-ESP.
  // Station 4 gibt dafür das für p5.js vorgesehene Signal e aus.
  if (command == 'y' || command == 'Y') {
    Serial.println("e");
    sendBluetoothKey('e');
    return;
  }

  // Alle Buchstaben werden in Kleinbuchstaben umgewandelt.
  // Zahlen bleiben unverändert.
  char normalizedCommand = toLowerCommand(command);

  if (isStation4GameCommand(normalizedCommand)) {
    sendBluetoothKey(normalizedCommand);
    return;
  }

  Serial.print("Unbekanntes Signal ignoriert: ");
  Serial.println(command);
}

// =========================
// ESP-NOW Empfang
// =========================
void handleIncomingData(const uint8_t *incomingData, int len) {
  if (len != sizeof(Message)) {
    return;
  }

  Message msg;
  memcpy(&msg, incomingData, sizeof(msg));

  receivedKey = msg.key;
  keyReceived = true;
}

// Für neuere ESP32-Arduino-Versionen
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(
  const esp_now_recv_info_t *info,
  const uint8_t *incomingData,
  int len
) {
  handleIncomingData(incomingData, len);
}
#else
// Für ältere ESP32-Arduino-Versionen
void onDataRecv(
  const uint8_t *mac,
  const uint8_t *incomingData,
  int len
) {
  handleIncomingData(incomingData, len);
}
#endif

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println();
  Serial.print("MAC-Adresse dieses Station-4-ESP: ");
  Serial.println(WiFi.macAddress());

  bleKeyboard.begin();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW konnte nicht gestartet werden.");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("Station-4-ESP bereit.");
  Serial.println("Erwartete Spielbefehle: 0, 1, 2, c, d, w, m, v, u");
  Serial.println("Internes Abstandssensor-Signal: y -> p5.js-Signal e");
}

// =========================
// Loop
// =========================
void loop() {
  // Signal vom Haupt-ESP empfangen und außerhalb des Callbacks verarbeiten.
  if (keyReceived) {
    char command = receivedKey;
    keyReceived = false;

    char outputCommand = toLowerCommand(command);

    Serial.print("Vom Haupt-ESP empfangen: ");
    Serial.println(outputCommand);

    processCommand(outputCommand);
  }

  // Test über den seriellen Monitor:
  // 0, 1, 2, c, d, w, m, v, u werden direkt getestet.
  // Großbuchstaben werden automatisch klein ausgegeben.
  // y testet das Abstandssensor-Signal und wird als e ausgegeben.
  while (Serial.available() > 0) {
    char command = Serial.read();

    if (command == '\n' || command == '\r' || command == ' ') {
      continue;
    }

    char outputCommand = toLowerCommand(command);

    Serial.print("Testbefehl über Serial empfangen: ");
    Serial.println(outputCommand);

    processCommand(outputCommand);
  }
}