Here is a functional specification document based on the provided code, our conversation history, and the source materials.

# Functional Specification: ESP8266 Flow Meter & MQTT Node

## 1\. Overview

The purpose of this software is to monitor a water flow rate sensor (or irrigation pump power) using an ESP8266 microcontroller and report the accumulated pulse counts over a configurable time interval to an MQTT broker 1, 2\. The system features a captive web portal for initial network and parameter configuration, persistent memory storage for custom settings, and Over-The-Air (OTA) update capabilities to allow wireless firmware deployment 3, 4\.

## 2\. Hardware Requirements

* **Microcontroller:** ESP8266 2\.  
* **Sensor:** SENSTREE G1" Male Thread Brass Water Flow Sensor (Hall effect) 5\.  
* **Pin Mapping:** The sensor data line is connected to GPIO 5 (labeled D1 on NodeMCU-style boards). This pin was selected to ensure the ESP8266 is not inadvertently pulled low during the boot process, which would cause a boot failure (a known issue when using GPIO 2\) 6, 7\. The sensor requires 5V but will be supplied with 3.3V from the ESP8266, which is powered via USB 8\.

## 3\. Software Dependencies & Libraries

* **ESP8266WiFi:** For base Wi-Fi connectivity 1\.  
* **PubSubClient:** For MQTT publishing and subscribing capabilities 9\.  
* **WiFiManager:** For creating a captive web portal to configure Wi-Fi credentials and custom parameters at runtime 4\.  
* **ArduinoOTA:** For uploading sketches over the local network without a physical USB connection 3\.  
* **LittleFS:** A filesystem wrapper for the ESP8266 to enable reading and writing files to the device's internal flash memory 10\.  
* **ArduinoJson:** For efficient JSON serialization and deserialization of the configuration parameters saved in the filesystem 11, 12\.

## 4\. Core Functional Requirements

### 4.1 Flow Measurement & Pulse Counting

* The system shall use an internal hardware interrupt attached to the sensor pin to detect pulses 7\.  
* The interrupt service routine (pulseCounter()) must trigger on a FALLING edge and increment a volatile integer variable (pulseCount) 6, 13\.  
* The interrupt function must be loaded into RAM using the ICACHE\_RAM\_ATTR attribute to prevent the ESP8266 from crashing during flash operations 13\.  
* The system shall count the accumulated pulses over a configurable time period, defined by the measInterval variable (defaulting to 10,000 milliseconds) 6\.  
* At the end of each interval, the system shall process the accumulated count and reset the pulseCount to zero for the next cycle 1, 13\.

### 4.2 MQTT Communication

* **Publishing:** At the conclusion of each measurement interval, the system shall convert the integer pulse count into a character array and publish it to the MQTT broker on the topic from\_esp8266\_irrigation\_pump 9, 13\.  
* **Subscribing:** The system shall subscribe to the MQTT topic to\_esp8266\_irrigation\_pump 9, 14\.  
* **Dynamic Interval Updates:** When a message is received on the subscribed topic, a callback function shall parse the payload, convert the character array to an integer using a custom char2int function, and dynamically update the measInterval variable 6, 15\.  
* **Connection Resilience:** If the connection to the MQTT broker drops, the system shall attempt to reconnect every 5 seconds and automatically re-subscribe to the required topic upon a successful connection 16\.

## 5\. Configuration & Provisioning Requirements

### 5.1 Captive Web Portal (WiFiManager)

* If the device cannot connect to a known Wi-Fi network, it shall broadcast its own Access Point named **"FlowMeterSetup"**.  
* The user shall be presented with a captive web portal to input standard Wi-Fi credentials (SSID and password) 4\.  
* The portal shall contain custom input fields for:  
* **MQTT Server:** Allows the user to input the IP address or hostname of the MQTT broker.  
* **Measurement Interval:** Allows the user to set the default startup interval for pulse reporting.  
* The portal shall display a read-only text box indicating the **Current Code Version** (e.g., FlowMeterMQTT\_v1.0).  
* Wi-Fi credentials shall be saved automatically by the ESP8266 core memory manager.

### 5.2 Persistent Storage (LittleFS & ArduinoJson)

* The system shall use the LittleFS library to mount the internal flash memory 17, 18\.  
* Custom parameters entered via the WiFiManager portal (MQTT Server Address and Measurement Interval) shall be bundled into a JSON document using the ArduinoJson library 12\.  
* This JSON document shall be written to a file named config.json in the LittleFS filesystem.  
* Upon device boot, the system must execute a loadConfig() function to mount the filesystem, read config.json, deserialize the JSON data, and apply the saved custom parameters *before* initializing the WiFiManager 12\.  
* If the user pushes a new measurement interval over MQTT, the system shall immediately apply the new interval *and* call the saveConfig() function to overwrite config.json, ensuring the MQTT-assigned value persists across reboots.

## 6\. Maintenance & Deployment Requirements

### 6.1 Over-The-Air (OTA) Firmware Updates

* The system must run the ArduinoOTA.handle() function continuously in its main loop 3\.  
* The device shall expose a network port to the local network via mDNS, advertising the hostname **"FlowMeter-OTA"** 19\.  
* The developer shall be able to compile and push new firmware directly from the Arduino IDE over the Wi-Fi network to the ESP8266, completely eliminating the need for a physical USB connection 3\.

