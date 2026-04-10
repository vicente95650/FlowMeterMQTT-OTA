// april 3, 2026 8:30AM modified  the code to implement the on-demand "request/response" JSON status feature.
// will subscribe to multiple MQTT topics. Implemented digital inputs
// april 9 renamed topics to reflect this esp8266 will monitor pond pump instead of irrigation pump. 
// added digital outputs



#include <ESP8266WiFi.h>  // Includes the core library for Wi-Fi connectivity.
#include <PubSubClient.h> // Includes the library for MQTT publishing and subscribing.
#include <LittleFS.h>     // Includes the library for persistent flash memory storage.
#include <ArduinoJson.h>  // Includes the library to parse and format JSON data.

// External Libraries for OTA and Dynamic Config
#include <WiFiManager.h>  // Includes the library for the captive portal setup.
#include <ArduinoOTA.h>   // Includes the library for Over-The-Air firmware updates.

// Sketch identification using character array
const char thisSketch[] = "FlowMeterMQTT_v1.0"; // Defines the version of the sketch as a constant character array.

// Custom parameters (These will be saved to/loaded from LittleFS)
// these assignment will be used (as default values) if not found in LittleFS
//char mqttServer[20] = "Mac-mini-2024.local"; // Sets the default MQTT server hostname (up to 40 characters).
//char mqttServer[20] = "192.168.3.47"; // Commented out alternative for a hardcoded IP address of mac-mini-2024 .
char mqttServer[20] = "192.168.3.62";  // mosquitto broker (a Docker container that is part of IOTStack) running in my Debian VM in HomeLab

int measInterval = 10000; // Sets the default measurement interval to 10 seconds (10,000 ms).
const int mqttPort = 1883; // Sets the standard MQTT port.

//const char publish_topic[] = "from_esp8266_irrigation_pump"; // Defines the topic to publish sensor data to.
//const char subscribe_topic[] = "to_esp8266_irrigation_pump"; // Defines the topic to listen for incoming commands.
const char publish_topic[] = "from_esp8266_pond_pump_pulseCount"; // Defines the topic to publish sensor data to.
const char subscribe_topic[] = "to_esp8266_pond_pump_measInterval"; // Defines the topic to listen for incoming commands.

// Flow rate sensor related variables
const byte sensorPin = 5; // Defines GPIO 5 (D1) as the pin connected to the flow sensor.
volatile int pulseCount = 0; // A volatile variable to store the number of pulses counted by the interrupt.
unsigned long initialTime = 0; // Stores the start time of the current measurement cycle.
char msgmqtt[21]; // Creates an 8-byte character array to hold the formatted payload for publishing.

WiFiClient espClient; // Creates the base TCP network client.
PubSubClient client(espClient); // Initializes the MQTT client using the network client.

// Flag to indicate if WiFiManager changed settings
bool shouldSaveConfig = false; // A flag to track if the user updated parameters via the captive portal.

// defining new topics for on-demand request/response JSON status feature 
//const char control_topic[] = "to_esp8266_irrigation_pump_control"; 
//const char status_topic[] = "status_esp8266_irrigation_pump"; 
const char control_topic[] = "to_esp8266_pond_pump_control"; 
const char status_topic[] = "status_from_esp8266_pond_pump"; 

// Digital Input Pins (Using GPIO 4 and 12 as examples)
const byte inputPin1 = 4;
const byte inputPin2 = 12;

// digital output pins
// will control with topic "to_esp8266_pond_pump_control"
const int outputPin1 = 13; // Labeled D7 on NodeMCU
const int outputPin2 = 14; // Labeled D5 on NodeMCU



// WiFiManager callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("WiFiManager config changed. Should save config."); // Prints a debug message.
  shouldSaveConfig = true; // Sets the flag to true so the main code knows to write to LittleFS.
}

// Interrupt Service Routine (must be in IRAM)
ICACHE_RAM_ATTR void pulseCounter() {
  pulseCount++; // Increment the pulse counter by 1 every time the interrupt fires (falling edge).
}

// Convert char string to integer
int char2int(char *str) {
  int result = 0; // Initializes the result variable.
  for (int i = 0; str[i] != '\0'; ++i) // Loops until the end of the character array.
    result = result*10 + str[i] - '0'; // Converts the ASCII character to its numeric value and adds it to the result.
  return result; // Returns the final integer.
}

// Save config to LittleFS
void saveConfig() {
  Serial.println("Saving config to LittleFS..."); // Prints a debug message.
  StaticJsonDocument<200> doc; // Creates an empty JSON document with 200 bytes of memory.
  doc["mqtt_server"] = mqttServer; // Adds the current MQTT server address to the JSON.
  doc["meas_interval"] = measInterval; // Adds the current measurement interval to the JSON.

  File configFile = LittleFS.open("/config.json", "w"); // Opens (or creates) the config file in write ("w") mode.
  if (!configFile) { // Checks if the file failed to open.
    Serial.println("Failed to open config file for writing"); // Prints an error if it fails.
    return; // Exits the function early.
  }
  serializeJson(doc, configFile); // Writes the JSON document into the physical file.
  configFile.close(); // Closes the file to free memory.
  Serial.println("Config saved successfully!"); // Prints a success message.
}

// Load config from LittleFS
void loadConfig() {
  Serial.println("Mounting File System..."); // Prints a debug message.
  if (LittleFS.begin()) { // Attempts to mount the flash memory file system.
    if (LittleFS.exists("/config.json")) { // Checks if the config file already exists.
      File configFile = LittleFS.open("/config.json", "r"); // Opens the file in read ("r") mode.
      if (configFile) { // Checks if the file opened successfully.
        StaticJsonDocument<200> doc; // Creates an empty JSON document.
        DeserializationError error = deserializeJson(doc, configFile); // Parses the file's contents into the JSON object.
        if (!error) { // Checks if parsing was successful.
          Serial.println("Loaded custom config from LittleFS."); // Prints a success message.
          strcpy(mqttServer, doc["mqtt_server"]); // Copies the saved server address into the global variable.
          measInterval = doc["meas_interval"]; // Copies the saved interval into the global variable.
        }
        configFile.close(); // Closes the file.
      }
    }
  } else {
    Serial.println("Failed to mount FS"); // Prints an error if the filesystem could not be mounted.
  }
}

// Callback function for handling subscribed messages - which now arrive on multiple topics.
// this new version uses strcmp (string compare) to figure out which topic the message arrived on, and then processes the payload accordingly.
void callback(char* topic, byte* payload, unsigned int length) {
  String MQTT_DATA = ""; 
  for (int i=0; i < length; i++) { 
    MQTT_DATA += (char)payload[i]; 
  }
  
  Serial.print("Message arrived on topic: "); 
  Serial.println(topic); 
  
  // ROUTE 1: If the message is on the interval update topic
  if (strcmp(topic, subscribe_topic) == 0) {
    int stringLength = MQTT_DATA.length()+1; 
    char arrayChars[stringLength]; 
    MQTT_DATA.toCharArray(arrayChars,stringLength); 
    
    // RESTORED ECHO: Publishes the received command back to the original topic
    // I should probably remove it even though I seldom change the measurement interval
    client.publish(publish_topic, arrayChars); 
    
    measInterval = char2int(arrayChars); 
    Serial.print("New measurement interval: "); 
    Serial.println(measInterval); 
    saveConfig(); // Save the new interval to flash memory
  }
  
  // ROUTE 2: If the message is on the new control topic
  else if (strcmp(topic, control_topic) == 0) {
    
    // 1. Check if Node-RED is asking for a status update
    if (MQTT_DATA == "GET_STATUS") {
      
      // Create a JSON document (Using the correct ArduinoJson v7 syntax)
      JsonDocument statusDoc; 
      
      // Populate the JSON document. Note that I don't do much processing here but rather at the node-red end
      statusDoc["ip"] = WiFi.localIP().toString();
      statusDoc["version"] = thisSketch;
      statusDoc["meas_interval"] = measInterval;
      statusDoc["pulse_count"] = pulseCount; 
      statusDoc["input_1"] = digitalRead(inputPin1); 
      statusDoc["input_2"] = digitalRead(inputPin2); 

      // --- NEW LINE: Add the mqttServer variable to the JSON payload ---
      // had to increase mqtt packet size limit to accomodate this
      statusDoc["mqtt_server"] = mqttServer; 

      // --- NEW LINE: Report Wi-Fi Signal Strength ---
      statusDoc["wifi_rssi"] = WiFi.RSSI(); 


      // CRITICAL FIX: Create a character array buffer with a size of 256 bytes
      char statusBuffer[512]; 
      
      // Convert JSON object into the character array and publish
      serializeJson(statusDoc, statusBuffer); 
      client.publish(status_topic, statusBuffer); 
      
      Serial.print("Published Status JSON: ");
      Serial.println(statusBuffer);
    }
    
    // You can easily add more commands later for your digital outputs!
    // else if (MQTT_DATA == "TURN_PUMP_ON") { ... }
    // 2. Check for Digital Output 1 Commands
        else if (MQTT_DATA == "OUT1_ON") {
            digitalWrite(outputPin1, HIGH);
            Serial.println("Output 1 turned ON");
        } 
        else if (MQTT_DATA == "OUT1_OFF") {
            digitalWrite(outputPin1, LOW);
            Serial.println("Output 1 turned OFF");
        }
        
        // 3. Check for Digital Output 2 Commands
        else if (MQTT_DATA == "OUT2_ON") {
            digitalWrite(outputPin2, HIGH);
            Serial.println("Output 2 turned ON");
        } 
        else if (MQTT_DATA == "OUT2_OFF") {
            digitalWrite(outputPin2, LOW);
            Serial.println("Output 2 turned OFF");
        }
  }
}

// Connect to mqtt server and reconnect
void reconnectmqttserver() {
  while (!client.connected()) {
    
    // 1. Keep OTA alive even if MQTT or Wi-Fi is failing!
    ArduinoOTA.handle();

    Serial.print("Attempting to connect to MQTT broker ...");
    Serial.print(mqttServer);
    Serial.println(" ...");

    // 2. Use a unique, hardcoded Client ID to prevent ALL conflicts
    String clientId = "ESP8266-FlowMeter-Unique-1"; 
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");
      client.subscribe(subscribe_topic); 
            client.subscribe(control_topic); 

      
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
  Serial.begin(9600); // Starts the serial monitor at 9600 baud.
  delay(1000); // Waits 1 second for the serial connection to stabilize.
  
  Serial.print("running sketch: "); // Prints a debug message.
  Serial.println(thisSketch); // Prints the current version of the code.



// Set digital outputs to a safe default state on boot
  digitalWrite(outputPin1, LOW);
  digitalWrite(outputPin2, LOW);

  // Initialize flow sensor pin and attach interrupt
  pinMode(sensorPin, INPUT_PULLUP); // Sets the sensor pin as an input with the internal pull-up resistor enabled.
  attachInterrupt(digitalPinToInterrupt(sensorPin), pulseCounter, FALLING); // Attaches the pulseCounter interrupt to trigger when the pin goes from HIGH to LOW.

// initialize input pins 
pinMode(inputPin1, INPUT_PULLUP);
pinMode(inputPin2, INPUT_PULLUP); 


  // Set the MQTT callback
  client.setCallback(callback); // Tells the MQTT client which function to run when a message arrives.

////////////////////////////////////////////////////////////////////////////////////////////////////
  // format file system in flash memory 
  // uncomment to wipe the flash memory so esp8266 will use the value defined here in code for mqtt server.
  // comment out immediately and upload once more or flash memory will be wiped out repeatedly on reboot.
  // LittleFS.format(); 
///////////////////////////////////////////////////////////////////////////////////////////////////

  // --- Load saved variables from Flash Memory ---
  loadConfig(); // Reads the config.json file to apply custom parameters before connecting to Wi-Fi.

  // --- Dynamic WiFi & Parameters via WiFiManager ---
  WiFiManager wifiManager; // Creates the WiFiManager object.
  wifiManager.setSaveConfigCallback(saveConfigCallback); // Tells WiFiManager which function to call if settings are changed.
  
  // 1. Custom text box for the MQTT Server
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqttServer, 40); // Creates an input field for the broker address.
  wifiManager.addParameter(&custom_mqtt_server); // Adds the field to the captive portal.

  // 2. Read-only text box to display the Sketch Version
  WiFiManagerParameter custom_version("version", "Current Code Version", thisSketch, 40, "readonly"); // Creates a read-only field showing the version.
  wifiManager.addParameter(&custom_version); // Adds the field to the captive portal.

  // 3. Custom text box to set the Initial Measurement Interval
  // We must convert the loaded integer to a char array for WiFiManager
  char intervalStr[22]; // Creates a character array to hold the interval as text.
  sprintf(intervalStr, "%d", measInterval); // Converts the integer interval into the character array.
  WiFiManagerParameter custom_interval("interval", "Measurement Interval (ms)", intervalStr, 6); // Creates an input field for the interval.
  wifiManager.addParameter(&custom_interval); // Adds the field to the captive portal.

  // Connect or broadcast AP named "FlowMeterSetup"
  wifiManager.autoConnect("FlowMeterSetup"); // Connects to saved Wi-Fi, or broadcasts the setup network if it fails.

  // Retrieve values from the web portal
  strcpy(mqttServer, custom_mqtt_server.getValue()); // Copies the user-entered server address into the global variable.
  measInterval = char2int((char*)custom_interval.getValue()); // Copies and converts the user-entered interval into the global variable.

  // Save the custom parameters to FS if they were changed in the portal
  if (shouldSaveConfig) { // Checks if the save flag was triggered.
      saveConfig(); // Calls the function to write the new settings to flash memory.
  }

  // Configure MQTT client
  client.setServer(mqttServer, mqttPort); // Tells the MQTT library the IP/hostname and port to use.

   // --- NEW LINE: Increase the MQTT packet size limit to 512 bytes ---
  client.setBufferSize(512); 
  
  // --- Initialize Over-The-Air (OTA) Updates ---
  ArduinoOTA.setHostname("FlowMeter-OTA"); // Sets the network hostname for wireless flashing.
  ArduinoOTA.begin(); // Starts the OTA service.
  
  Serial.println("Connected to Wifi and OTA Ready"); // Prints a final success message for setup.
}

void loop() {
  // Listen for OTA update requests
  ArduinoOTA.handle(); // Continuously checks if you are trying to push new firmware over Wi-Fi.

  // Check if connected and reconnect if necessary
  if (!client.connected()) { // Checks if the MQTT connection has dropped.
      reconnectmqttserver(); // Calls the function to re-establish the connection.
  }
  client.loop(); // Keeps the MQTT client actively listening and processing incoming/outgoing messages.

  // Check if measurement interval has completed.
  unsigned long now = millis(); // Gets the current uptime of the device in milliseconds.
  if( (now - initialTime) > measInterval ) { // Checks if the difference between now and the start time is greater than your target interval.
      initialTime = now; // Resets the start time to 'now' for the next cycle.
      
      // Publish pulseCount to MQTT broker
      snprintf (msgmqtt, 50, "%d ",pulseCount); // Converts the integer pulseCount into the msgmqtt character array.
      client.publish(publish_topic, msgmqtt); // Publishes the counted pulses to your MQTT broker.
      
      // Reset the count in preparation for new meas. interval
      pulseCount = 0; // Resets the counter back to zero for the next measuring period.
  }
}