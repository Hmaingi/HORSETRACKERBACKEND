#include <MKRNB.h>
#include <WiFiNINA.h> // Use WiFiNINA for MKR WiFi 1010 or compatible boards
#include <ArduinoHttpClient.h>

// Cellular settings
NBClient nbClient;
GPRS gprs;
NB nbAccess;

// WiFi settings
WiFiClient wifiClient;
char ssid[] = "your_wifi_ssid";     // Replace with your WiFi SSID
char password[] = "your_wifi_password"; // Replace with your WiFi password

// Server details
char server[] = "your-nodejs-server-url.com"; // Replace with your deployed server URL
int port = 443; // or 80 for HTTP
String path = "/api/data";

// Device credentials
String deviceId = "your_device_id";
String deviceToken = "your_device_token";

// Sensor variables (example)
float heartRate = 0;
float temperature = 0;
float oxygen = 0;
float speed = 0;
float lat = 0;
float lng = 0;

// Client pointer to switch between cellular and WiFi
Client* client = &nbClient;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  
  // Try cellular connection first
  Serial.println("Initializing cellular...");
  bool cellularConnected = false;
  for (int i = 0; i < 5; i++) { // Retry 5 times
    if (nbAccess.begin() == NB_READY && gprs.attachGPRS() == GPRS_READY) {
      Serial.println("Connected to cellular network");
      client = &nbClient; // Use cellular client
      cellularConnected = true;
      break;
    }
    Serial.println("Failed to connect to cellular. Retrying...");
    delay(1000);
  }

  // If cellular fails, try WiFi
  if (!cellularConnected) {
    Serial.println("Attempting WiFi connection...");
    WiFi.begin(ssid, password);
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 10) { // Retry 10 times
      Serial.println("Connecting to WiFi...");
      delay(1000);
      wifiAttempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to WiFi network");
      client = &wifiClient; // Use WiFi client
    } else {
      Serial.println("Failed to connect to WiFi. No connection available.");
      // Optionally, implement a fallback or halt
    }
  }
}

void loop() {
  // Check if connected (cellular or WiFi)
  if ((client == &nbClient && nbAccess.status() == NB_READY && gprs.status() == GPRS_READY) ||
      (client == &wifiClient && WiFi.status() == WL_CONNECTED)) {
    // Read sensors
    readSensors();
    
    // Prepare JSON payload
    String jsonPayload = "{";
    jsonPayload += "\"heartRate\":" + String(heartRate) + ",";
    jsonPayload += "\"temperature\":" + String(temperature) + ",";
    jsonPayload += "\"oxygen\":" + String(oxygen) + ",";
    jsonPayload += "\"speed\":" + String(speed) + ",";
    jsonPayload += "\"gps\":{\"lat\":" + String(lat, 6) + ",\"lng\":" + String(lng, 6) + "}";
    jsonPayload += "}";
    
    // Make HTTP request
    HttpClient httpClient = HttpClient(*client, server, port);
    httpClient.beginRequest();
    httpClient.post(path);
    httpClient.sendHeader("Content-Type", "application/json");
    httpClient.sendHeader("X-Device-ID", deviceId);
    httpClient.sendHeader("X-Device-Token", deviceToken);
    httpClient.sendHeader("Content-Length", jsonPayload.length());
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
  } else {
    Serial.println("No connection available. Skipping transmission.");
  }
  
  // Wait before next transmission
  delay(30000); // Send data every 30 seconds
}

void readSensors() {
  // Implement your actual sensor reading logic here
  // This is just example data
  heartRate = random(30, 100);
  temperature = 37.0 + random(0, 20)/10.0;
  oxygen = 95 + random(0, 5);
  speed = random(0, 20);
  lat = 45.0 + random(0, 100)/1000.0;
  lng = -75.0 + random(0, 100)/1000.0;
}