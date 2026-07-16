
#include <Wire.h>
#include <math.h>
#include <BleKeyboard.h>
#include <WiFi.h>
#include <esp_now.h>

// =========================
// ESP32 Feather + TCA9548A + 3x MMA8452Q
// + BLE Keyboard fürs Haupt-Tablet
// + ESP-NOW Weitergabe an Station 4
// =========================

// I2C Pins am ESP32 Feather
#define SDA_PIN 23
#define SCL_PIN 22

// Zwei HC-SR04 Abstandssensoren am Haupt-ESP32
#define DIST1_TRIG_PIN 27
#define DIST1_ECHO_PIN 33
#define DIST2_TRIG_PIN 15
#define DIST2_ECHO_PIN 32

// TCA9548A Standardadresse
#define TCA_ADDR 0x70

// MMA8452Q Adresse
// Falls dein Board nicht antwortet: 0x1D testen
#define MMA_ADDR 0x1D

// TCA-Kanäle für deine 3 Sensoren
const uint8_t SENSOR_CHANNELS[3] = {0, 1, 2};
const uint8_t NUM_SENSORS = 3;

// =========================
// ESP-NOW: Station 4
// =========================
// HIER die echte MAC-Adresse vom Station-4-ESP eintragen.
// Beispiel: aus 24:6F:28:AA:BB:CC wird {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC}
uint8_t station4Address[] = {0x08, 0xA6, 0xF7, 0x0C, 0x60, 0xC0};

struct StationMessage {
  char key;
};

bool station4Ready = false;

// Bluetooth-Tastatur des Haupt-ESP32 für das gekoppelte iPad
BleKeyboard keyboard("ESP32 Keyboard");

void addStation4Peer() {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, station4Address, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_is_peer_exist(station4Address)) {
    Serial.println("Station 4 ist bereits als Peer vorhanden.");
    station4Ready = true;
    return;
  }

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("Station 4 wurde als ESP-NOW Peer hinzugefügt.");
    station4Ready = true;
  } else {
    Serial.println("FEHLER: Station 4 konnte nicht als Peer hinzugefügt werden.");
    station4Ready = false;
  }
}

void sendKeyToStation4(char key) {
  if (!station4Ready) {
    Serial.println("Station 4 ist nicht bereit. Sende nicht.");
    return;
  }

  StationMessage msg;
  msg.key = key;

  esp_err_t result = esp_now_send(station4Address, (uint8_t *)&msg, sizeof(msg));

  if (result == ESP_OK) {
    Serial.print("ESP-NOW an Station 4 gesendet: ");
    Serial.println(key);
  } else {
    Serial.print("ESP-NOW Senden an Station 4 fehlgeschlagen. Fehlercode: ");
    Serial.println(result);
  }
}

bool isStation4WebsiteCommand(char command) {
  switch (command) {
    case '0':
    case '1':
    case '2':
    case 'C':
    case 'D':
    case 'W':
    case 'M':
    case 'V':
    case 'U':
      return true;

    default:
      return false;
  }
}

char toUpperCommand(char command) {
  if (command >= 'a' && command <= 'z') {
    return command - ('a' - 'A');
  }

  return command;
}

void handleSerialCommandsFromWebsite() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();

    // Zeilenumbrüche und Leerzeichen von Website / Serial Monitor ignorieren.
    if (cmd == '\n' || cmd == '\r' || cmd == ' ') {
      continue;
    }

    char normalizedCmd = toUpperCommand(cmd);

    Serial.print("USB/Website-Befehl empfangen: ");
    Serial.println(cmd);

    // X gehört ausschließlich zu Station 1.
    // Das Signal wird per BLE-Tastatur an das Tablet / den Aktor von Station 1 gegeben.
    if (normalizedCmd == 'X') {
      if (keyboard.isConnected()) {
        keyboard.write('X');
        Serial.println("Bluetooth Station 1 sendet: X");
      } else {
        Serial.println("Bluetooth nicht verbunden: X wurde nicht gesendet.");
      }

      continue;
    }

    // Diese Befehle gehören ausschließlich zu Station 4.
    // Kleinbuchstaben werden akzeptiert, aber immer als definierter
    // Großbuchstabe an Station 4 gesendet.
    if (isStation4WebsiteCommand(normalizedCmd)) {
      sendKeyToStation4(normalizedCmd);
      continue;
    }

    Serial.print("Unbekannter Website-Befehl: ");
    Serial.println(cmd);
  }
}

// =========================
// MMA8452Q Register
// =========================
#define REG_STATUS        0x00
#define REG_OUT_X_MSB     0x01
#define REG_WHO_AM_I      0x0D
#define REG_XYZ_DATA_CFG  0x0E
#define REG_CTRL_REG1     0x2A
#define REG_CTRL_REG2     0x2B
#define REG_CTRL_REG4     0x2D
#define REG_CTRL_REG5     0x2E

#define WHO_AM_I_EXPECTED 0x2A

// =========================
// Sensor-Zustand
// =========================
struct SensorState {
  float xg = 0.0f;
  float yg = 0.0f;
  float zg = 0.0f;
  float amag = 1.0f;
  float baseline = 1.0f;
  float hp = 0.0f;
  float lastHp = 0.0f;
  float score = 0.0f;
  float peak = 0.0f;
  bool knockActive = false;
  unsigned long knockStartMs = 0;
  unsigned long lastKnockMs = 0;
  bool online = false;
};

SensorState sensors[NUM_SENSORS];

// =========================
// Tuning
// =========================
const float BASELINE_ALPHA = 0.01f;
const float HP_SMOOTH_ALPHA = 0.30f;
const float KNOCK_THRESHOLD_G = 0.18f;   // später anpassen
const unsigned long REFRACTORY_MS = 40;
const unsigned long PEAK_WINDOW_MS = 5;
const unsigned long LOOP_DELAY_MS = 0;

// =========================
// Abstandssensor-Einstellungen
// =========================
const float DETECTION_DISTANCE_CM = 40.0f; // Objekt erkannt bei 40 cm oder näher
const float RELEASE_DISTANCE_CM = 44.0f;   // Sensor wird ab 44 cm wieder freigegeben

// Die Sensoren werden abwechselnd gemessen. Dadurch stören sich ihre
// Ultraschallimpulse weniger gegenseitig.
const unsigned long DISTANCE_SENSOR_GAP_MS = 60;
const unsigned long ECHO_TIMEOUT_US = 10000;

// Sensor 1 darf bereits nach einer sicheren Nahmessung auslösen.
// Sensor 2 bleibt bei zwei aufeinanderfolgenden Nahmessungen.
const uint8_t NEEDED_NEAR_MEASUREMENTS_SENSOR1 = 1;
const uint8_t NEEDED_NEAR_MEASUREMENTS_SENSOR2 = 2;
const uint8_t NEEDED_FAR_MEASUREMENTS = 3;
const uint8_t NEEDED_INVALID_MEASUREMENTS = 5;

struct DistanceSensorState {
  uint8_t trigPin;
  uint8_t echoPin;
  bool objectDetected;
  uint8_t nearCounter;
  uint8_t farCounter;
  uint8_t invalidCounter;
  float lastDistanceCm;
};

const uint8_t NUM_DISTANCE_SENSORS = 2;

DistanceSensorState distanceSensors[NUM_DISTANCE_SENSORS] = {
  {DIST1_TRIG_PIN, DIST1_ECHO_PIN, false, 0, 0, 0, -1.0f},
  {DIST2_TRIG_PIN, DIST2_ECHO_PIN, false, 0, 0, 0, -1.0f}
};

unsigned long lastDistanceMeasurementMs = 0;
uint8_t nextDistanceSensor = 0;

const unsigned long FIGMA_COOLDOWN_MS = 300;
unsigned long lastFigmaTriggerMs = 0;

// =========================
// HC-SR04 Abstandssensoren
// =========================
float readDistanceCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return -1.0f;
  }

  return (duration * 0.0343f) / 2.0f;
}

void triggerDistanceAction(uint8_t sensorIndex, unsigned long now) {
  DistanceSensorState &sensor = distanceSensors[sensorIndex];

  // Sensor 1 gehört zu Station 1 und meldet z an p5.js.
  // Sensor 2 gehört zu Station 4 und meldet e an p5.js.
  char p5Signal = (sensorIndex == 0) ? 'z' : 'e';

  // Diese einzelne Zeile kann von p5.js / der HTML-Seite erkannt werden.
  Serial.println(p5Signal);

  Serial.print("Abstandssensor ");
  Serial.print(sensorIndex + 1);
  Serial.print(" aktiviert bei ");
  Serial.print(sensor.lastDistanceCm, 1);
  Serial.println(" cm");

  if (sensorIndex == 0) {
    // Abstandssensor 1: z direkt per Bluetooth an das Tablet von Station 1.
    if (keyboard.isConnected()) {
      keyboard.write('z');
      lastFigmaTriggerMs = now;
      Serial.println("Abstandssensor 1 -> Bluetooth Haupt-ESP sendet: z");
    } else {
      Serial.println("Bluetooth Haupt-ESP nicht verbunden: z wurde nicht gesendet.");
    }
  } else {
    // Das bestehende interne Relais-Signal y bleibt erhalten:
    // Station 4 kann daraus das p5.js-Signal e erzeugen.
    sendKeyToStation4('y');
    Serial.println("Abstandssensor 2 -> internes ESP-NOW-Signal an Station 4: y");
    Serial.println("Abstandssensor 2 -> p5.js meldet: e");
  }
}

void updateSingleDistanceSensor(uint8_t sensorIndex, unsigned long now) {
  DistanceSensorState &sensor = distanceSensors[sensorIndex];

  const uint8_t neededNearMeasurements =
    (sensorIndex == 0)
      ? NEEDED_NEAR_MEASUREMENTS_SENSOR1
      : NEEDED_NEAR_MEASUREMENTS_SENSOR2;

  sensor.lastDistanceCm = readDistanceCm(sensor.trigPin, sensor.echoPin);

  if (sensor.lastDistanceCm > 0.0f &&
      sensor.lastDistanceCm <= DETECTION_DISTANCE_CM) {
    if (sensor.nearCounter < neededNearMeasurements) {
      sensor.nearCounter++;
    }
    sensor.farCounter = 0;
    sensor.invalidCounter = 0;

  } else if (sensor.lastDistanceCm >= RELEASE_DISTANCE_CM) {
    if (sensor.farCounter < NEEDED_FAR_MEASUREMENTS) {
      sensor.farCounter++;
    }
    sensor.nearCounter = 0;
    sensor.invalidCounter = 0;

  } else if (sensor.lastDistanceCm < 0.0f) {
    // Ein einzelnes fehlendes Echo soll eine begonnene Naherkennung
    // nicht sofort wieder löschen. Erst mehrere fehlende Messungen
    // gelten als sicher "weit weg".
    if (sensor.invalidCounter < NEEDED_INVALID_MEASUREMENTS) {
      sensor.invalidCounter++;
    }
    sensor.farCounter = 0;

    if (sensor.invalidCounter >= NEEDED_INVALID_MEASUREMENTS) {
      sensor.nearCounter = 0;
    }

  } else {
    // Bereich zwischen 27 und 31 cm: bisherigen Zustand beibehalten.
    sensor.nearCounter = 0;
    sensor.farCounter = 0;
    sensor.invalidCounter = 0;
  }

  if (!sensor.objectDetected &&
      sensor.nearCounter >= neededNearMeasurements) {
    sensor.objectDetected = true;
    sensor.nearCounter = 0;
    triggerDistanceAction(sensorIndex, now);
  }

  if (sensor.objectDetected &&
      (sensor.farCounter >= NEEDED_FAR_MEASUREMENTS ||
       sensor.invalidCounter >= NEEDED_INVALID_MEASUREMENTS)) {
    sensor.objectDetected = false;
    sensor.farCounter = 0;
    sensor.invalidCounter = 0;

    Serial.print("Abstandssensor ");
    Serial.print(sensorIndex + 1);
    Serial.println(" wieder freigegeben.");
  }
}

void updateDistanceSensors(unsigned long now) {
  if (now - lastDistanceMeasurementMs < DISTANCE_SENSOR_GAP_MS) {
    return;
  }

  lastDistanceMeasurementMs = now;

  // Immer nur einen Sensor pro Durchlauf auslösen, danach wechseln.
  updateSingleDistanceSensor(nextDistanceSensor, now);
  nextDistanceSensor = (nextDistanceSensor + 1) % NUM_DISTANCE_SENSORS;
}

// =========================
// TCA9548A Kanal wählen
// =========================
void tcaSelect(uint8_t channel) {
  if (channel > 7) return;

  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

// Optional: alle Kanäle deaktivieren
void tcaDisableAll() {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();
}

// =========================
// I2C Basisfunktionen
// =========================
bool writeRegister8(uint8_t devAddr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(devAddr);
  Wire.write(reg);
  Wire.write(value);
  return (Wire.endTransmission() == 0);
}

bool readRegister8(uint8_t devAddr, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(devAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)devAddr, 1) != 1) return false;
  value = Wire.read();
  return true;
}

bool readRegisters(uint8_t devAddr, uint8_t startReg, uint8_t *buffer, size_t len) {
  Wire.beginTransmission(devAddr);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) return false;

  size_t got = Wire.requestFrom((int)devAddr, (int)len);
  if (got != len) return false;

  for (size_t i = 0; i < len; i++) {
    buffer[i] = Wire.read();
  }
  return true;
}

// =========================
// MMA8452Q Funktionen
// =========================
bool mmaStandby() {
  uint8_t ctrl1;
  if (!readRegister8(MMA_ADDR, REG_CTRL_REG1, ctrl1)) return false;
  ctrl1 &= ~(0x01); // ACTIVE = 0
  return writeRegister8(MMA_ADDR, REG_CTRL_REG1, ctrl1);
}

bool mmaActive() {
  uint8_t ctrl1;
  if (!readRegister8(MMA_ADDR, REG_CTRL_REG1, ctrl1)) return false;
  ctrl1 |= 0x01; // ACTIVE = 1
  return writeRegister8(MMA_ADDR, REG_CTRL_REG1, ctrl1);
}

bool initMMA8452OnSelectedChannel() {
  uint8_t who;
  if (!readRegister8(MMA_ADDR, REG_WHO_AM_I, who)) return false;
  if (who != WHO_AM_I_EXPECTED) return false;

  if (!mmaStandby()) return false;

  // ±2g für hohe Empfindlichkeit
  if (!writeRegister8(MMA_ADDR, REG_XYZ_DATA_CFG, 0x00)) return false;

  // 100 Hz Datenrate, volle 12 Bit, noch Standby
  // DR bits = 011 -> 100 Hz
  if (!writeRegister8(MMA_ADDR, REG_CTRL_REG1, 0x18)) return false;

  if (!writeRegister8(MMA_ADDR, REG_CTRL_REG2, 0x00)) return false;
  if (!writeRegister8(MMA_ADDR, REG_CTRL_REG4, 0x00)) return false;
  if (!writeRegister8(MMA_ADDR, REG_CTRL_REG5, 0x00)) return false;

  if (!mmaActive()) return false;

  delay(10);
  return true;
}

bool readAccelG(float &xg, float &yg, float &zg) {
  uint8_t raw[6];
  if (!readRegisters(MMA_ADDR, REG_OUT_X_MSB, raw, 6)) return false;

  // 12-bit signed, left-aligned
  int16_t x = ((int16_t)((raw[0] << 8) | raw[1])) >> 4;
  int16_t y = ((int16_t)((raw[2] << 8) | raw[3])) >> 4;
  int16_t z = ((int16_t)((raw[4] << 8) | raw[5])) >> 4;

  // Sign extension
  if (x & 0x0800) x |= 0xF000;
  if (y & 0x0800) y |= 0xF000;
  if (z & 0x0800) z |= 0xF000;

  // ±2g => 1024 counts/g
  xg = x / 1024.0f;
  yg = y / 1024.0f;
  zg = z / 1024.0f;

  return true;
}

// =========================
// Scanner für TCA-Kanäle
// =========================
void scanTCAChannels() {
  Serial.println("Scanne TCA-Kanaele 0..2 nach MMA8452Q ...");

  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    uint8_t ch = SENSOR_CHANNELS[i];
    tcaSelect(ch);

    Serial.print("Kanal ");
    Serial.print(ch);
    Serial.print(": ");

    Wire.beginTransmission(MMA_ADDR);
    if (Wire.endTransmission() == 0) {
      Serial.print("Geraet auf 0x");
      Serial.print(MMA_ADDR, HEX);

      uint8_t who;
      if (readRegister8(MMA_ADDR, REG_WHO_AM_I, who)) {
        Serial.print("  WHO_AM_I=0x");
        Serial.println(who, HEX);
      } else {
        Serial.println("  WHO_AM_I konnte nicht gelesen werden");
      }
    } else {
      Serial.println("kein Geraet gefunden");
    }
  }

  tcaDisableAll();
}

// =========================
// Baseline-Kalibrierung
// =========================
void calibrateBaselines() {
  Serial.println("Kalibriere Baselines in Ruhe...");
  const int samples = 120;

  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    sensors[i].baseline = 1.0f;
    sensors[i].hp = 0.0f;
    sensors[i].lastHp = 0.0f;
    sensors[i].score = 0.0f;
    sensors[i].peak = 0.0f;
  }

  for (int n = 0; n < samples; n++) {
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
      if (!sensors[i].online) continue;

      tcaSelect(SENSOR_CHANNELS[i]);

      float xg, yg, zg;
      if (!readAccelG(xg, yg, zg)) continue;

      float amag = sqrtf(xg * xg + yg * yg + zg * zg);

      if (n == 0) {
        sensors[i].baseline = amag;
      } else {
        sensors[i].baseline =
          (1.0f - BASELINE_ALPHA) * sensors[i].baseline +
          BASELINE_ALPHA * amag;
      }
    }
    delay(10);
  }

  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    Serial.print("Sensor ");
    Serial.print(i);
    Serial.print(" baseline = ");
    Serial.println(sensors[i].baseline, 4);
  }

  tcaDisableAll();
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32 Feather + TCA9548A + 3x MMA8452Q + BLE + ESP-NOW");
  Serial.println("Start...");

  // Pins beider Abstandssensoren vorbereiten
  for (uint8_t i = 0; i < NUM_DISTANCE_SENSORS; i++) {
    pinMode(distanceSensors[i].trigPin, OUTPUT);
    pinMode(distanceSensors[i].echoPin, INPUT);
    digitalWrite(distanceSensors[i].trigPin, LOW);
  }

  // WICHTIG: Erst I2C und die Bewegungssensoren starten.
  // BLE + WiFi/ESP-NOW kommen danach, damit die Sensorerkennung nicht gestört wird.
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);   // stabiler als 400 kHz bei längeren Kabeln / Ausstellungseinbau
  Wire.setTimeOut(50);
  delay(200);

  // Erst prüfen, ob der TCA da ist
  Wire.beginTransmission(TCA_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.println("TCA9548A gefunden.");
  } else {
    Serial.println("TCA9548A NICHT gefunden! Verkabelung, SDA/SCL-Pins und richtigen ESP pruefen.");
    while (1) {
      delay(1000);
    }
  }

  scanTCAChannels();

  // Sensoren initialisieren
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    tcaSelect(SENSOR_CHANNELS[i]);

    Serial.print("Init Sensor ");
    Serial.print(i);
    Serial.print(" auf TCA-Kanal ");
    Serial.print(SENSOR_CHANNELS[i]);
    Serial.print(" ... ");

    if (initMMA8452OnSelectedChannel()) {
      sensors[i].online = true;
      Serial.println("OK");
    } else {
      sensors[i].online = false;
      Serial.println("FEHLER");
    }
  }

  tcaDisableAll();
  calibrateBaselines();

  // Danach erst WiFi Station Mode für ESP-NOW starten.
  // Das ist keine normale WLAN-Verbindung und braucht keinen Router.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.print("MAC-Adresse Haupt-ESP: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW konnte nicht gestartet werden");
  } else {
    Serial.println("ESP-NOW gestartet");
    addStation4Peer();
  }

  // Bluetooth-Tastatur für das Tablet am Haupt-ESP zuletzt starten
  keyboard.begin();

  Serial.println();
  Serial.println("Starte Klopf- und Abstandserkennung...");
  Serial.println("Ausgabe: S0 S1 S2 -> erkannter Bewegungssensor");
  Serial.println("HC-SR04 Nr. 1: TRIG=27, ECHO=33");
  Serial.println("HC-SR04 Nr. 2: TRIG=15, ECHO=32");
  Serial.println("Abstandssensor 1: z per Bluetooth an Station 1 und per USB-Serial");
  Serial.println("Abstandssensor 2: internes y per ESP-NOW an Station 4; e per USB-Serial an p5.js");
  Serial.println("Website -> Station 1: X");
  Serial.println("Website -> Station 4: 0, 1, 2, C, D, W, M, V, U");
  Serial.println();
}

// =========================
// Loop
// =========================
void loop() {
  // Befehle von deiner HTML-Website über USB empfangen
  // 0, 1, 2, C, D, W, M, V und U -> Station 4
  // X -> Station 1 per Bluetooth
  handleSerialCommandsFromWebsite();

  unsigned long now = millis();

  int bestSensor = -1;
  float bestPeak = 0.0f;
  bool anyFinishedKnock = false;

  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    if (!sensors[i].online) continue;

    tcaSelect(SENSOR_CHANNELS[i]);

    float xg, yg, zg;
    if (!readAccelG(xg, yg, zg)) {
      Serial.print("Read error sensor ");
      Serial.println(i);
      continue;
    }

    sensors[i].xg = xg;
    sensors[i].yg = yg;
    sensors[i].zg = zg;

    float amag = sqrtf(xg * xg + yg * yg + zg * zg);
    sensors[i].amag = amag;

    if (!sensors[i].knockActive) {
      sensors[i].baseline =
        (1.0f - BASELINE_ALPHA) * sensors[i].baseline +
        BASELINE_ALPHA * amag;
    }

    float hpRaw = amag - sensors[i].baseline;

    sensors[i].hp =
      (1.0f - HP_SMOOTH_ALPHA) * sensors[i].hp +
      HP_SMOOTH_ALPHA * hpRaw;

    float jerk = sensors[i].hp - sensors[i].lastHp;
    sensors[i].lastHp = sensors[i].hp;
    sensors[i].score = fabsf(jerk);

    bool aboveThreshold = sensors[i].score > KNOCK_THRESHOLD_G;
    bool outsideRefractory = (now - sensors[i].lastKnockMs) > REFRACTORY_MS;

    if (!sensors[i].knockActive && aboveThreshold && outsideRefractory) {
      sensors[i].knockActive = true;
      sensors[i].knockStartMs = now;
      sensors[i].peak = sensors[i].score;
    }

    if (sensors[i].knockActive) {
      if (sensors[i].score > sensors[i].peak) {
        sensors[i].peak = sensors[i].score;
      }

      if ((now - sensors[i].knockStartMs) >= PEAK_WINDOW_MS) {
        sensors[i].knockActive = false;
        sensors[i].lastKnockMs = now;
        anyFinishedKnock = true;

        if (sensors[i].peak > bestPeak) {
          bestPeak = sensors[i].peak;
          bestSensor = i;
        }
      }
    }
  }

  tcaDisableAll();

  // Beide Abstandssensoren abwechselnd auslesen.
  // Sensor 1 meldet z an p5.js.
  // Sensor 2 meldet e an p5.js und nutzt intern weiterhin y zu Station 4.
  updateDistanceSensors(now);

  if (anyFinishedKnock && bestSensor >= 0) {
    if (keyboard.isConnected() && (now - lastFigmaTriggerMs > FIGMA_COOLDOWN_MS)) {
      char sentKey = '-';

      if (bestSensor == 0) {
        sentKey = 'a';
      } else if (bestSensor == 1) {
        sentKey = 'b';
      } else if (bestSensor == 2) {
        sentKey = 'c';
      }

      // zuerst Signal über USB-C / Serial an die HTML-Seite senden
      if (bestSensor == 0) {
        Serial.println("SOUND_A");
      } else if (bestSensor == 1) {
        Serial.println("SOUND_B");
      } else if (bestSensor == 2) {
        Serial.println("SOUND_C");
      }

      // danach Bluetooth-Taste an das Tablet senden, das mit dem Haupt-ESP verbunden ist
      keyboard.write(sentKey);

      lastFigmaTriggerMs = now;

      Serial.print("Sensor ");
      Serial.print(bestSensor);
      Serial.print(" aktiviert -> Bluetooth sendet: ");
      Serial.println(sentKey);
    }
  }

  delay(LOOP_DELAY_MS);
}