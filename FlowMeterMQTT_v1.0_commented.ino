// FlowMeterMQTT_v1.0
//
// This is a commented copy of the original sketch. The behavior is intended
// to stay the same; the extra comments are here to explain structure,
// dependencies, runtime flow, and the purpose of each major block.
//
// Revision notes from the original file:
// - April 3, 2026: added on-demand request/response JSON status support.
// - April 3, 2026: subscribed to multiple MQTT topics and added digital inputs.
// - April 9, 2026: renamed topics from irrigation pump to pond pump.
// - April 9, 2026: added digital outputs.
// - April 10, 2026: simplified topic names to be more generic and reusable.

#include <ESP8266WiFi.h>   // Core ESP8266 Wi-Fi support.
#include <PubSubClient.h>  // MQTT client library.
#include <LittleFS.h>      // Flash filesystem for persistent configuration.
#include <ArduinoJson.h>   // JSON parse/serialize support.

// Extra libraries used for dynamic configuration and OTA firmware updates.
#include <WiFiManager.h>
#include <ArduinoOTA.h>

// Human-readable sketch identifier used in logs and status responses.
const char thisSketch[] = "FlowMeterMQTT_v1.0";

// MQTT server default.
// If `/config.json` exists in LittleFS, that saved value overrides this one.
//
// Previous options kept in the original sketch:
// char mqttServer[20] = "Mac-mini-2024.local";
// char mqttServer[20] = "192.168.3.47";
char mqttServer[20] = "192.168.3.62";

// Default measurement interval in milliseconds.
// This is also user-configurable and persisted in LittleFS.
int measInterval = 10000;

// Standard MQTT port.
const int mqttPort = 1883;

// Main MQTT topics.
// `publish_topic` sends pulse counts.
// `subscribe_topic` receives interval updates.
const char publish_topic[] = "pond_pump_pulseCount";
const char subscribe_topic[] = "pond_pump_measInterval";

// Flow sensor input configuration.
const byte sensorPin = 5;            // GPIO5 / D1 on many ESP8266 boards.
volatile int pulseCount = 0;         // Updated inside interrupt service routine.
unsigned long initialTime = 0;       // Marks start of current measurement window.
char msgmqtt[21];                    // Buffer for publishing pulse count as text.

// Networking objects.
WiFiClient espClient;
PubSubClient client(espClient);

// Set to true when WiFiManager portal changes configuration values.
bool shouldSaveConfig = false;

// Additional MQTT topics for command/control and JSON status reporting.
const char control_topic[] = "pond_pump_control";
const char status_topic[] = "pond_pump_status";

// Digital inputs reported in the status JSON.
const byte inputPin1 = 4;
const byte inputPin2 = 12;

// Digital outputs controlled through commands received on `control_topic`.
const int outputPin1 = 13;  // D7 on NodeMCU.
const int outputPin2 = 14;  // D5 on NodeMCU.

// Called by WiFiManager when settings in the captive portal are changed.
// The actual write to LittleFS is deferred until setup continues.
void saveConfigCallback() {
  Serial.println("WiFiManager config changed. Should save config.");
  shouldSaveConfig = true;
}

// Interrupt service routine for the flow meter pulse signal.
// Each falling edge increments the pulse counter.
ICACHE_RAM_ATTR void pulseCounter() {
  pulseCount++;
}

// Simple numeric conversion from a null-terminated char array to int.
// This assumes the string contains only decimal digits.
int char2int(char *str) {
  int result = 0;
  for (int i = 0; str[i] != '\0'; ++i) {
    result = result * 10 + str[i] - '0';
  }
  return result;
}

// Persist current MQTT server and measurement interval to LittleFS.
void saveConfig() {
  Serial.println("Saving config to LittleFS...");

  StaticJsonDocument<200> doc;
  doc["mqtt_server"] = mqttServer;
  doc["meas_interval"] = measInterval;

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  serializeJson(doc, configFile);
  configFile.close();
  Serial.println("Config saved successfully!");
}

// Load persisted config from LittleFS if the file exists.
// If mounting fails or the file is absent, the sketch continues with defaults.
void loadConfig() {
  Serial.println("Mounting File System...");

  if (LittleFS.begin()) {
    if (LittleFS.exists("/config.json")) {
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, configFile);
        if (!error) {
          Serial.println("Loaded custom config from LittleFS.");
          strcpy(mqttServer, doc["mqtt_server"]);
          measInterval = doc["meas_interval"];
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("Failed to mount FS");
  }
}

// MQTT callback for all subscribed topics.
//
// Route 1:
// - `subscribe_topic` updates the measurement interval.
//
// Route 2:
// - `control_topic` accepts command strings such as:
//   - GET_STATUS
//   - OUT1_ON / OUT1_OFF
//   - OUT2_ON / OUT2_OFF
void callback(char* topic, byte* payload, unsigned int length) {
  String MQTT_DATA = "";
  for (int i = 0; i < length; i++) {
    MQTT_DATA += (char)payload[i];
  }

  Serial.print("Message arrived on topic: ");
  Serial.println(topic);

  // Interval update path.
  if (strcmp(topic, subscribe_topic) == 0) {
    int stringLength = MQTT_DATA.length() + 1;
    char arrayChars[stringLength];
    MQTT_DATA.toCharArray(arrayChars, stringLength);

    measInterval = char2int(arrayChars);
    Serial.print("esp8266 just received a new measurement interval: ");
    Serial.println(measInterval);

    // Persist the new interval so it survives reboot.
    saveConfig();
  }

  // Command/control path.
  else if (strcmp(topic, control_topic) == 0) {

    // Build and publish a JSON snapshot of current runtime state.
    if (MQTT_DATA == "GET_STATUS") {
      JsonDocument statusDoc;

      statusDoc["ip"] = WiFi.localIP().toString();
      statusDoc["version"] = thisSketch;
      statusDoc["meas_interval"] = measInterval;
      statusDoc["pulse_count"] = pulseCount;
      statusDoc["input_1"] = digitalRead(inputPin1);
      statusDoc["input_2"] = digitalRead(inputPin2);
      statusDoc["mqtt_server"] = mqttServer;
      statusDoc["wifi_rssi"] = WiFi.RSSI();

      // Packet buffer increased later in setup to handle this payload size.
      char statusBuffer[512];
      serializeJson(statusDoc, statusBuffer);
      client.publish(status_topic, statusBuffer);

      Serial.print("Published Status JSON: ");
      Serial.println(statusBuffer);
    }

    // Output control commands.
    else if (MQTT_DATA == "OUT1_ON") {
      digitalWrite(outputPin1, HIGH);
      Serial.println("Output 1 turned ON");
    }
    else if (MQTT_DATA == "OUT1_OFF") {
      digitalWrite(outputPin1, LOW);
      Serial.println("Output 1 turned OFF");
    }
    else if (MQTT_DATA == "OUT2_ON") {
      digitalWrite(outputPin2, HIGH);
      Serial.println("Output 2 turned ON");
    }
    else if (MQTT_DATA == "OUT2_OFF") {
      digitalWrite(outputPin2, LOW);
      Serial.println("Output 2 turned OFF");
    }

    // Additional commands could be added here later.
  }
}

// Connect to the MQTT broker and keep retrying until connected.
// OTA handling is kept alive during retries so firmware uploads still work.
void reconnectmqttserver() {
  while (!client.connected()) {
    ArduinoOTA.handle();

    Serial.print("Attempting to connect to MQTT broker ...");
    Serial.print(mqttServer);
    Serial.println(" ...");

    // Fixed client ID chosen to avoid accidental conflicts from changing IDs.
    String clientId = "ESP8266-FlowMeter-Unique-1";

    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");

      // Subscribe to both the interval-update topic and the control topic.
      client.subscribe(subscribe_topic);
      client.subscribe(control_topic);

      char publish_payload[] = "the esp8266 mqtt client is connected to mqtt broker";
      client.publish(publish_topic, publish_payload);
    }
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.print("running sketch: ");
  Serial.println(thisSketch);

  // Set outputs to a known safe state before enabling any control logic.
  digitalWrite(outputPin1, LOW);
  digitalWrite(outputPin2, LOW);

  // Configure the flow sensor input and start counting pulses on falling edges.
  pinMode(sensorPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(sensorPin), pulseCounter, FALLING);

  // Configure digital inputs used for status reporting.
  pinMode(inputPin1, INPUT_PULLUP);
  pinMode(inputPin2, INPUT_PULLUP);

  // Register MQTT message callback.
  client.setCallback(callback);

  ////////////////////////////////////////////////////////////////////////////////////////////////////
  // Use only when intentionally wiping saved flash settings.
  // Uncomment, upload once, then comment it back out and upload again.
  // Otherwise config is erased on every reboot.
  // LittleFS.format();
  ////////////////////////////////////////////////////////////////////////////////////////////////////

  // Load previously saved values before starting WiFiManager.
  loadConfig();

  // Create WiFiManager portal and extra fields for runtime configuration.
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Editable MQTT server field.
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqttServer, 40);
  wifiManager.addParameter(&custom_mqtt_server);

  // Read-only sketch version field for quick identification in the portal.
  WiFiManagerParameter custom_version("version", "Current Code Version", thisSketch, 40, "readonly");
  wifiManager.addParameter(&custom_version);

  // Editable initial measurement interval field.
  char intervalStr[22];
  sprintf(intervalStr, "%d", measInterval);
  WiFiManagerParameter custom_interval("interval", "Measurement Interval (ms)", intervalStr, 6);
  wifiManager.addParameter(&custom_interval);

  // Connect using saved Wi-Fi or start the "FlowMeterSetup" captive portal.
  wifiManager.autoConnect("FlowMeterSetup");

  // Read values back out of the portal after connection/setup.
  strcpy(mqttServer, custom_mqtt_server.getValue());
  measInterval = char2int((char*)custom_interval.getValue());

  // Persist updated values if WiFiManager reports they changed.
  if (shouldSaveConfig) {
    saveConfig();
  }

  // Final MQTT client configuration.
  client.setServer(mqttServer, mqttPort);

  // The status JSON can be relatively large, so increase MQTT packet size.
  client.setBufferSize(512);

  // Enable OTA so the sketch can be updated wirelessly later.
  ArduinoOTA.setHostname("FlowMeter-OTA");
  ArduinoOTA.begin();

  Serial.println("Connected to Wifi and OTA Ready");
}

void loop() {
  // OTA must be serviced regularly.
  ArduinoOTA.handle();

  // Reconnect MQTT if needed, then service PubSubClient continuously.
  if (!client.connected()) {
    reconnectmqttserver();
  }
  client.loop();

  // Publish the pulse count whenever the configured interval expires.
  unsigned long now = millis();
  if ((now - initialTime) > measInterval) {
    initialTime = now;

    // Convert current pulse count to text and publish it.
    snprintf(msgmqtt, sizeof(msgmqtt), "%d ", pulseCount);
    client.publish(publish_topic, msgmqtt);

    // Reset for the next measurement window.
    pulseCount = 0;
  }
}
