#include <TinyGSM.h> // For SIM800L or similar GSM module
#include <ArduinoHttpClient.h>
#include <DallasTemperature.h> // For DS18B20
#include <OneWire.h> // For DS18B20
#include <Wire.h> // For I2C (MPU-6050 and MAX30100)
#include <Adafruit_MPU6050.h> // For MPU-6050
#include <Adafruit_Sensor.h> // For MPU-6050
#include <MAX30105.h> // For MAX30100 (using MAX30105 library, compatible with MAX30100)
#include <TinyGPS++.h> // For NEO-6M GPS
#include <SD.h> // For SD card
#include <SPI.h> // For SD card

// Cellular settings (SIM800L or similar)
#define SerialGSM Serial1 // Use Serial1 for SIM800L (pins RX: 16, TX: 17 on ESP32)
TinyGSM modem(SerialGSM);
TinyGSMClient gsmClient(modem);

// Server details
const char* server = "https://horsetrackerbackend.onrender.com"; // Replace with your deployed server URL
const int port = 3002; // Your server port
const String path = "/api/data";

// Device credentials
const String deviceId = "device123"; // Replace with your device ID
const String deviceToken = "your_device_token"; // Replace if authentication required

// Pin definitions
#define ONE_WIRE_BUS 4 // DS18B20 data pin
#define SD_CS_PIN 5 // SD card chip select pin
#define GPS_RX_PIN 22 // NEO-6M RX pin
#define GPS_TX_PIN 21 // NEO-6M TX pin

// Sensor objects
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
Adafruit_MPU6050 mpu;
MAX30105 max30100; // MAX30100 library (MAX30105-compatible)
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // Use Serial2 for GPS (pins 16, 17 on ESP32)

// Sensor variables
float heartRate = 0;
float temperature = 0;
float oxygen = 0;
float speed = 0;
float lat = 0;
float lng = 0;

// SD card file
File dataFile;

void setup() {
  Serial.begin(115200); // Higher baud rate for ESP32
  while (!Serial);

  // Initialize DS18B20
  ds18b20.begin();

  // Initialize MPU-6050
  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (true);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Initialize MAX30100
  if (!max30100.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30100 not found");
    while (true);
  }
  max30100.setup(); // Default settings

  // Initialize GPS
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Initialize SD card
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card initialization failed");
  } else {
    Serial.println("SD card initialized");
  }

  // Initialize cellular connection
  Serial.println("Initializing cellular...");
  SerialGSM.begin(9600, SERIAL_8N1, 16, 17); // RX: 16, TX: 17 for SIM800L
  bool connected = false;
  for (int i = 0; i < 5; i++) {
    if (modem.restart() && modem.gprsConnect("your_apn", "", "")) { // Replace "your_apn" with your carrier's APN
      Serial.println("Connected to cellular network");
      connected = true;
      break;
    }
    Serial.println("Failed to connect to cellular. Retrying...");
    delay(2000);
  }
  if (!connected) {
    Serial.println("Failed to connect to cellular. Halting.");
    while (true);
  }
}

void loop() {
  // Check cellular connection
  if (modem.isGprsConnected()) {
    // Read sensors
    readSensors();

    // Prepare JSON payload
    String jsonPayload = "{";
    jsonPayload += "\"deviceId\":\"" + deviceId + "\",";
    jsonPayload += "\"heartRate\":" + String(heartRate, 1) + ",";
    jsonPayload += "\"temperature\":" + String(temperature, 1) + ",";
    jsonPayload += "\"oxygen\":" + String(oxygen, 1) + ",";
    jsonPayload += "\"speed\":" + String(speed, 1) + ",";
    jsonPayload += "\"gps\":{\"lat\":" + String(lat, 6) + ",\"lng\":" + String(lng, 6) + "}";
    jsonPayload += "}";

    // Log to SD card
    logToSD(jsonPayload);

    // Send to server
    HttpClient httpClient(gsmClient, server, port);
    httpClient.beginRequest();
    httpClient.post(path);
    httpClient.sendHeader("Content-Type", "application/json");
    httpClient.sendHeader("Content-Length", jsonPayload.length());
    // Uncomment if authentication required
    // httpClient.sendHeader("Authorization", "Bearer " + deviceToken);
    httpClient.beginBody();
    httpClient.print(jsonPayload);
    httpClient.endRequest();

    // Read response
    int statusCode = httpClient.responseStatusCode();
    String response = httpClient.responseBody();
    
    Serial.print("Status code: ");
    Serial.println(statusCode);
    Serial.print("Response: ");
    Serial.println(response);

    // If successful, clear SD card buffer (optional)
    if (statusCode == 200) {
      clearSDBuffer();
    }
  } else {
    Serial.println("Cellular connection lost. Logging to SD card...");
    readSensors();
    String jsonPayload = "{";
    jsonPayload += "\"deviceId\":\"" + deviceId + "\",";
    jsonPayload += "\"heartRate\":" + String(heartRate, 1) + ",";
    jsonPayload += "\"temperature\":" + String(temperature, 1) + ",";
    jsonPayload += "\"oxygen\":" + String(oxygen, 1) + ",";
    jsonPayload += "\"speed\":" + String(speed, 1) + ",";
    jsonPayload += "\"gps\":{\"lat\":" + String(lat, 6) + ",\"lng\":" + String(lng, 6) + "}";
    jsonPayload += "}";
    logToSD(jsonPayload);
    
    // Attempt to reconnect
    if (modem.restart() && modem.gprsConnect("your_apn", "", "")) {
      Serial.println("Reconnected to cellular network");
    }
  }

  // Wait before next transmission
  delay(30000); // Send/log every 30 seconds
}

void readSensors() {
  // DS18B20 Temperature
  ds18b20.requestTemperatures();
  temperature = ds18b20.getTempCByIndex(0);
  if (temperature == DEVICE_DISCONNECTED_C) {
    temperature = 0; // Handle error
    Serial.println("DS18B20 error");
  }

  // MPU-6050 Speed (approximated from accelerometer)
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  speed = sqrt(a.acceleration.x * a.acceleration.x + 
               a.acceleration.y * a.acceleration.y + 
               a.acceleration.z * a.acceleration.z) / 9.81; // Approx. speed in m/s

  // MAX30100 Heart Rate and SpO2
  max30100.update();
  heartRate = max30100.getHeartRate(); // Requires library-specific implementation
  oxygen = max30100.getSpO2(); // Requires library-specific implementation
  if (heartRate == 0 || oxygen == 0) {
    Serial.println("MAX30100 error");
  }

  // NEO-6M GPS
  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read())) {
      if (gps.location.isValid()) {
        lat = gps.location.lat();
        lng = gps.location.lng();
      } else {
        lat = 0;
        lng = 0;
        Serial.println("GPS not fixed");
      }
    }
  }
}

void logToSD(String data) {
  dataFile = SD.open("datalog.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println(data + "," + String(millis()));
    dataFile.close();
    Serial.println("Data logged to SD card");
  } else {
    Serial.println("Error opening SD card file");
  }
}

void clearSDBuffer() {
  if (SD.exists("datalog.txt")) {
    SD.remove("datalog.txt");
    Serial.println("SD buffer cleared");
  }
}