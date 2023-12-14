#include <algorithm>
#include <vector>

#include <Arduino.h>
#include <WiFi.h>
#include <Firebase.h>
#include <addons/TokenHelper.h>

#define FIREBASE_API_KEY ""
#define FIREBASE_DB_URL ""

struct _NetworkConfig {
  const char *ssid;
  const char *password;
} networks[] = {
  { .ssid = "Hermes-IoT", .password = "HermesIOT" },
  { .ssid = "WiFi-IoT-123", .password = "TUDOMINUSCULO" },
};

struct _FirebaseConfig {
  const char *dbUrl;
  const char *path;
  String deviceId;
  const char *apiKey;
  const char *email;
  const char *password;
  FirebaseConfig config;
  FirebaseAuth auth;
} firebaseConfig = {
  .dbUrl = FIREBASE_DB_URL,
  .path = "/dispositivos",
  .deviceId = "",
  .apiKey = FIREBASE_API_KEY,
  .email = "",
  .password = ""
};

struct _Calibration {
  float low;
  float high;
} calibration = {
  .low = 0,
  .high = 1
};

float frandom(float low, float high) {
  return low + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (high - low)));
}

bool setupWifi();
bool setupFirebase();
void readCalibration();
void publishReadings();
bool connectionRound();

void setup() {
  Serial.begin(115200);
  while (!Serial);
  setupWifi();
  setupFirebase();
}

void loop() {
  if (setupWifi()) {
    setupFirebase();
  }

  try {
    readCalibration();
    publishReadings();
  } catch (const std::exception& e) {
    Serial.printf("Exception: %s\n", e.what());
  }

  delay(1000);
}

void readCalibration() {
  Serial.println("Reading calibration");

  char calibrationPath[256];
  sprintf(calibrationPath, "%s/%s/calibragem", firebaseConfig.path, firebaseConfig.deviceId.c_str());

  FirebaseData data;
  if (Firebase.RTDB.getJSON(&data, calibrationPath)) {
    Serial.printf(
      "Path = %s; Payload = %s\n",
      data.dataPath().c_str(),
      data.payload().c_str()
    );
  } else {
    Serial.printf(
      "Failed to read calibration. Reason: %s. Path = %s\n",
      data.errorReason().c_str(),
      calibrationPath
    );
    return;
  }

  FirebaseJson json = data.jsonObject();
  FirebaseJsonData d;
  if (json.get(d, "baixo")) {
    calibration.low = d.to<float>();
  } else {
    Serial.println("Failed to read calibration low");
  }

  if (json.get(d, "alto")) {
    calibration.high = d.to<float>();
  } else {
    Serial.println("Failed to read calibration high");
  }

  Serial.printf("Calibration: %.2f - %.2f\n", calibration.low, calibration.high);
}

void publishReadings() {
  Serial.println("Publishing readings");

  char readingsPath[256];
  sprintf(readingsPath, "%s/%s/umidade", firebaseConfig.path, firebaseConfig.deviceId.c_str());

  FirebaseData obj;
  float reading = frandom(calibration.low, calibration.high);
  if (Firebase.RTDB.setFloat(&obj, readingsPath, reading)) {
    Serial.printf("Path = %s; Reading: %.2f\n", obj.dataPath().c_str(), reading);
  } else {
    Serial.printf("Failed to publish reading. Reason: %s\n", reading, obj.errorReason().c_str());
  }
}

bool setupFirebase() {
  Serial.println("Setting up Firebase");

  firebaseConfig.deviceId = WiFi.macAddress();

  auto& config = firebaseConfig.config;
  auto& auth = firebaseConfig.auth;

  config.database_url = firebaseConfig.dbUrl;
  config.api_key = firebaseConfig.apiKey;
  config.timeout.serverResponse = 1000;

  config.token_status_callback = tokenStatusCallback;
  if (Firebase.signUp(&config, &auth, firebaseConfig.email, firebaseConfig.password)) {
    Serial.println("Sign up succeeded");
  } else {
    Serial.println("Sign up failed");
    return false;
  }

  Firebase.begin(&config, &auth);
  return true;
}

bool setupWifi() {
  if (WiFi.isConnected()) return false;

  Serial.println("Setting up WiFi");

  WiFi.mode(WIFI_STA);

  Serial.println("Connecting to WiFi");
  while (!connectionRound()) {
    Serial.println("Retrying connection");
  }
  Serial.println("Connected to WiFi");

  return true;
}

bool connectionRound() {
  const char* networkTypes[] = {
    "Open",
    "WEP",
    "WPA PSK",
    "WPA2 PSK",
    "WPA/WPA2 PSK",
    "WPA2 Enterprise",
    "WPA3 PSK",
    "WPA2/WPA3 PSK",
    "WAPI PSK",
    "Err"
  };

  Serial.println("Scanning networks");
  WiFi.scanNetworks();

  struct Candidate {
    int rssi;
    String ssid;
    String password;
  };
  std::vector<Candidate> candidates;

  Serial.println("Candidates:");
  for (int i = 0; i < WiFi.scanComplete(); i++) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    wifi_auth_mode_t authMode = WiFi.encryptionType(i);
    String bssid = WiFi.BSSIDstr(i);

    Serial.printf("- %s (%d) (%s)\n", ssid.c_str(), rssi, networkTypes[authMode]);

    for (auto& network : networks) {
      if (ssid == network.ssid) {
        candidates.push_back({ rssi, network.ssid, network.password });
      }
    }
  }

  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.rssi > b.rssi;
  });

  for (auto& candidate : candidates) {
    Serial.printf("Trying %s\n", candidate.ssid.c_str());
    WiFi.begin(candidate.ssid.c_str(), candidate.password.c_str());

    int i;
    for (i = 0; i < 20; i++) {
      auto status = WiFi.status();
      if (status == WL_CONNECT_FAILED || status == WL_CONNECTED) {
        break;
      }
      delay(500);
    }

    if (i == 20 || WiFi.status() == WL_CONNECT_FAILED) {
      Serial.printf("Failed to connect to %s\n", candidate.ssid.c_str());
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Connected to %s\n", candidate.ssid.c_str());
      return true;
    }
  }

  return false;
}
