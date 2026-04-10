You have two different ways to change the MQTT server in your ESP8266 Flow Meter project, because your device uses persistent flash memory to store custom settings.  
If you just change the variable in your code and upload it via the OTA network port, it will not work on its own. When the ESP8266 boots, it runs a loadConfig() function that reads the saved config.json file from its flash memory and overwrites your newly hardcoded IP address with the old one.  
Here is a reminder of the two methods you can use:  
**Method 1: The WiFiManager Portal (No code changes or OTA needed)** The easiest way is to use the captive web portal built into your ESP8266.

1. Force the ESP8266 to lose its Wi-Fi connection (for example, by temporarily turning off your router or moving the device out of range).  
2. The ESP8266 will fall back to broadcasting its own Access Point named **"FlowMeterSetup"**.  
3. Connect your phone or computer to this "FlowMeterSetup" Wi-Fi network.  
4. A web portal will automatically open, presenting you with custom input fields. Enter your new MQTT Server address into the text box and hit save. The ESP8266 will save this new address to its flash memory and reboot.

**Method 2: The Hardcoded Way (Code change \+ OTA \+ Flash Clear)** If you want to strictly manage the MQTT server through your C++ code and upload it via the **"FlowMeter-OTA"** network port, you must force the ESP8266 to forget the old settings.

1. Change the char mqttServer variable in your code to the new IP address.  
2. In your setup() function, uncomment the line that formats the flash memory: LittleFS.format();.  
3. Upload this code to the ESP8266 over the air using the **"FlowMeter-OTA"** port. When it reboots, it will wipe the flash memory and be forced to fall back on your new hardcoded variable.  
4. **Important:** You must then comment the // LittleFS.format(); line back out and do one more OTA upload. If you skip this step, the ESP8266 will wipe its memory every time the power cycles\!

