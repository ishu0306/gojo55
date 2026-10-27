/*
  AI Smart Fitness Tracker - ESP32 firmware
  Compatible with the uploaded Web Telemetry dashboard's BLE UUIDs.

  Hardware assumed:
    - ESP32 DevKit (ESP32-WROOM-32 or similar)
    - MPU6050 I2C motion sensor

  Not installed / not used:
    - MAX30102: HR and SpO2 are sent as N/A
    - DHT22: temperature and humidity are sent as N/A
    - MQ-135: air quality is sent as N/A
    - Battery voltage monitor: battery percentage is sent as N/A

  BLE CSV notification format expected by the dashboard:
    HR,SpO2,Activity,Temp,Humidity,AQI,Battery,ChargingState

  This is a basic motion-threshold demo, not a trained AI model or medical device.
*/

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Matches the UUIDs in your uploaded HTML dashboard.
#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHARACTERISTIC_UUID "19b10001-e8f2-537e-4f6c-d104768a1214"

// Default ESP32 I2C pins: SDA = GPIO 21, SCL = GPIO 22.
constexpr int I2C_SDA = 21;
constexpr int I2C_SCL = 22;

Adafruit_MPU6050 mpu;
BLECharacteristic *telemetryCharacteristic = nullptr;
bool bleConnected = false;
bool mpuReady = false;

unsigned long lastSampleMs = 0;
unsigned long lastNotifyMs = 0;
constexpr unsigned long SAMPLE_INTERVAL_MS = 50;   // 20 samples/second
constexpr unsigned long NOTIFY_INTERVAL_MS = 1000; // Send dashboard data once/second

float previousMagnitude = 0.0f;
float motionChangeAverage = 0.0f;
int motionSamples = 0;
String currentActivity = "Starting";

class FitnessServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    Serial.println("Dashboard connected over BLE.");
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    Serial.println("Dashboard disconnected. Restarting BLE advertising...");
    BLEDevice::startAdvertising();
  }
};

String classifyActivity(float magnitude, float motionChange) {
  // Simple demonstration thresholds. Calibrate them with real collected data.
  if (motionChange < 0.12f) {
    return "Resting";
  }
  if (motionChange < 0.65f) {
    return "Walking";
  }
  return "Running";
}

void setupBLE() {
  BLEDevice::init("AI_Fitness_Band");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new FitnessServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);
  telemetryCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  telemetryCharacteristic->addDescriptor(new BLE2902());
  telemetryCharacteristic->setValue("N/A,N/A,Starting,N/A,N/A,N/A,N/A,Unknown");
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE ready. Device name: AI_Fitness_Band");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (mpu.begin()) {
    mpuReady = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 detected.");
  } else {
    Serial.println("ERROR: MPU6050 not detected. Check VCC, GND, SDA and SCL.");
  }

  setupBLE();
  Serial.println("Fitness tracker started.");
}

void loop() {
  const unsigned long now = millis();

  if (mpuReady && now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;

    sensors_event_t accel, gyro, temp;
    mpu.getEvent(&accel, &gyro, &temp);

    // Acceleration magnitude (m/s^2); gravity is included.
    const float ax = accel.acceleration.x;
    const float ay = accel.acceleration.y;
    const float az = accel.acceleration.z;
    const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
    const float change = fabsf(magnitude - previousMagnitude);
    previousMagnitude = magnitude;

    // Smooth the change value to reduce sudden misclassification.
    motionChangeAverage = (motionChangeAverage * 0.8f) + (change * 0.2f);
    motionSamples++;
    if (motionSamples >= 5) {
      currentActivity = classifyActivity(magnitude, motionChangeAverage);
      motionSamples = 0;
    }
  }

  if (now - lastNotifyMs >= NOTIFY_INTERVAL_MS) {
    lastNotifyMs = now;

    if (!mpuReady) {
      currentActivity = "MPU_Error";
    }

    // Keep the eight CSV fields in the exact order expected by the HTML.
    // Missing sensors are marked N/A rather than inventing readings.
    String payload = "N/A,N/A," + currentActivity + ",N/A,N/A,N/A,N/A,Unknown";

    Serial.println(payload);
    if (bleConnected && telemetryCharacteristic != nullptr) {
      telemetryCharacteristic->setValue(payload.c_str());
      telemetryCharacteristic->notify();
    }
  }
}
