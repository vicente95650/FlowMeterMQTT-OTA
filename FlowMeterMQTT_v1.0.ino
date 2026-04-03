#include <ESP8266WiFi.h>  // WIFI library
#include <PubSubClient.h> // MQTT library
#include <LittleFS.h>     // For permanent storage
#include <ArduinoJson.h>  // External library for JSON formatting

// External Libraries for OTA and Dynamic Config
#include <WiFiManager.h>  
#include <ArduinoOTA.h>   

// Sketch identification using character array
const char thisSketch[] = "FlowMeterMQTT_v1.2"; 

// Custom parameters (These will be saved to/loaded from LittleFS)
char mqttServer[40] = "Mac-mini-2024.local";
//char mqttServer[40] = "192.168.3.32";
int measInterval = 10000; // default time interval in milliseconds

const int mqttPort = 1883;
const char publish_topic[] = "from_esp8266_irrigation_pump";
const char subscribe_topic[] = "to_esp8266_irrigation_pump";

// Flow rate sensor related variables
const byte sensorPin = 5; // labeled D1
volatile int pulseCount = 0; // updated in ISR
unsigned long initialTime = 0;

char msgmqtt[8]; // an array of characters for publishing

WiFiClient espClient;
PubSubClient client(espClient);

// Flag to indicate if WiFiManager changed settings
bool shouldSaveConfig = false;

// WiFiManager callback notifying us of the need to save config
void saveConfigCallback () {
    Serial.println("WiFiManager config changed. Should save config.");
    shouldSaveConfig = true;
}

// Interrupt Service Routine (must be in IRAM)
ICACHE_RAM_ATTR void pulseCounter() {
    pulseCount++; // Increment the pulse counter once per interrupt
}

// Convert char string to integer
int char2int(char *str) {
  int result = 0; 
  for (int i = 0; str[i] != '\0'; ++i)
    result = result*10 + str[i] - '0';
  return result;
}

// Save config to LittleFS
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

// Load config from LittleFS
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

// Callback function for handling subscribed messages
void callback(char* topic, byte* payload, unsigned int length) {
    String MQTT_DATA = "";  
    for (int i=0; i < length; i++) {
        MQTT_DATA += (char)payload[i]; 
    } 
    
    Serial.print("received data on topic: ");
    Serial.println(topic);
    
    int stringLength = MQTT_DATA.length()+1;
    char arrayChars[stringLength];  
    MQTT_DATA.toCharArray(arrayChars,stringLength);  
    
    client.publish(publish_topic, arrayChars);  
    
    // Update interval dynamically over MQTT
    measInterval = char2int(arrayChars); 
    Serial.print("new measurement interval:  ");
    Serial.println(measInterval);

    // Save the new MQTT-provided interval permanently!
    saveConfig(); 
}

// Connect to mqtt server and reconnect
void reconnectmqttserver() {
    while (!client.connected()) {
        Serial.print("Attempting to connect to MQTT broker ...");
        Serial.print(mqttServer); 
        Serial.println(" ...");

        String clientId = "ESP8266ClientIrrigationPump-";  
        clientId += String(random(0xffff), HEX);  
        
        if (client.connect(clientId.c_str())) {
            Serial.println("connected!");
            client.subscribe(subscribe_topic);  
            
            char publish_payload[] = "the esp8266 mqtt client is connected to mqtt broker";
            client.publish(publish_topic,publish_payload);
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

    // Initialize flow sensor pin and attach interrupt
    pinMode(sensorPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(sensorPin), pulseCounter, FALLING);

    // Set the MQTT callback
    client.setCallback(callback); 

// format 
// LittleFS.format();

    // --- Load saved variables from Flash Memory ---
    loadConfig();

    // --- Dynamic WiFi & Parameters via WiFiManager ---
    WiFiManager wifiManager;
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    
    // 1. Custom text box for the MQTT Server
    WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqttServer, 40);
    wifiManager.addParameter(&custom_mqtt_server);

    // 2. Read-only text box to display the Sketch Version
    WiFiManagerParameter custom_version("version", "Current Code Version", thisSketch, 40, "readonly");
    wifiManager.addParameter(&custom_version);

    // 3. Custom text box to set the Initial Measurement Interval
    // We must convert the loaded integer to a char array for WiFiManager
    char intervalStr[9];
    sprintf(intervalStr, "%d", measInterval);
    WiFiManagerParameter custom_interval("interval", "Measurement Interval (ms)", intervalStr, 6);
    wifiManager.addParameter(&custom_interval);

    // Connect or broadcast AP named "FlowMeterSetup"
    wifiManager.autoConnect("FlowMeterSetup");

    // Retrieve values from the web portal
    strcpy(mqttServer, custom_mqtt_server.getValue());
    measInterval = char2int((char*)custom_interval.getValue()); 

    // Save the custom parameters to FS if they were changed in the portal
    if (shouldSaveConfig) {
        saveConfig();
    }

    // Configure MQTT client
    client.setServer(mqttServer, mqttPort); 
    
    // --- Initialize Over-The-Air (OTA) Updates ---
    ArduinoOTA.setHostname("FlowMeter-OTA");
    ArduinoOTA.begin();
    
    Serial.println("Connected to Wifi and OTA Ready");
}

void loop() {
    // Listen for OTA update requests 
    ArduinoOTA.handle();

    // Check if connected and reconnect if necessary
    if (!client.connected()) {
        reconnectmqttserver();
    }
    client.loop(); 

    // Check if measurement interval has completed.
    unsigned long now = millis();  
    if( (now - initialTime) > measInterval ) {  
        initialTime = now;  
        
        // Publish pulseCount to MQTT broker
        snprintf (msgmqtt, 50, "%d ",pulseCount); 
        client.publish(publish_topic, msgmqtt); 
        
        // Reset the count in preparation for new meas. interval
        pulseCount = 0;  
    }
}
