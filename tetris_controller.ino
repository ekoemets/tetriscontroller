#include <FS.h>   

#include <ESP8266WiFi.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <Adafruit_ADS1015.h>
#include <PubSubClient.h>



/*
 * ADC pins and directions
 * Pin: 0 - Y axis(vertical movement)
 * * value > 15000 - Down
 * * value < 1000 - Up
 * Pin: 1 - X axis(horizontal movement)
 * * value > 15000 - Right
 * * value < 1000 - Left
 * 
 */

// Configuration panel values
const int configTimeout = 600;
const char * configSSID = "MQTTSetupPanel";
const char * configPassword = "salajane";

//flag for saving data
bool shouldSaveConfig = false;

WiFiClient wifiClient;
Adafruit_ADS1115 ads(0x48);
PubSubClient mqttClient(wifiClient);
char mqtt_server[40];


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


int16_t readADC(int pin) {
  return ads.readADC_SingleEnded(pin);
}


void mountFS() {
  //clean FS, for testing
  //SPIFFS.format();
  
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read  
}

void initWifiManager() {
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManager wifiManager;
  
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);

  if (readADC(0) > 15000) {
    // set configportal timeout
    wifiManager.setConfigPortalTimeout(configTimeout);
    if (!wifiManager.startConfigPortal(configSSID, configPassword)) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.restart();
      delay(5000);
    }
  }
  
  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }


}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  Wire.pins(0, 2);
  Wire.begin(0, 2);
  ads.begin();
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  mountFS();
  initWifiManager();
  mqttClient.setServer(mqtt_server, 1883);

}

boolean reconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


String command = "";
String lastCommand = "";
int counter = 0;

void loop() {
  
  if (!mqttClient.connected()) {
    reconnect();
  }

  mqttClient.loop();
  
  int x = readADC(1);
  int y = readADC(0);
  if(x < 1000){
    command = "Left";
  }
  else if(x > 15000){
    command = "Right";
  }
  else if(y < 1000){
    command = "Up";
  }
  else if(y > 15000){
    command = "Down";
  }
  else{
    command = "";
    lastCommand = "";
  }
  counter++;
  if((!command.equals(lastCommand) && !command.equals("")) || counter >= 500){
    Serial.println((char*) command.c_str());
    lastCommand = command;
    counter = 0;
    mqttClient.publish("Tetris",  (char*) command.c_str());
  }
  delay(50);
}
