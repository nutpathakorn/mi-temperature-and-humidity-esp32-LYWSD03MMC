#include <BLEDevice.h>
#include <SimpleTimer.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define LYWSD03MMC_ADDR "a4:c1:38:0e:56:0b"

BLEClient* pClient;

const char*   WIFI_SSID       = "PF VIP";
const char*   WIFI_PASSWORD   = "PFadmin3001";

const char*   MQTT_HOST       = "203.146.157.219";
const int     MQTT_PORT       = 1883;
const char*   MQTT_CLIENTID   = "miflora-client";
const char*   MQTT_USERNAME   = "4nfam";
const char*   MQTT_PASSWORD   = "as31as13a6s7";
const String  MQTT_BASE_TOPIC = "4nfam/5/mi_temp_humi"; 
const int     MQTT_RETRY_WAIT = 5000;

float temp;
float humi;
float batt;
float volt;

// sleep between to runs in seconds 30*60=30min
#define SLEEP_DURATION 30 * 5
// emergency hibernate countdown in seconds
#define EMERGENCY_HIBERNATE 3 * 60
// how often should the battery be read - in run count
#define BATTERY_INTERVAL 6
// how often should a device be retried in a run when something fails
#define RETRY 3

TaskHandle_t hibernateTaskHandle = NULL;

bool connectionSuccessful = false;
String baseTopic = MQTT_BASE_TOPIC + "/" + LYWSD03MMC_ADDR + "/";

WiFiClient espClient;
PubSubClient client(espClient);

// ESP32 MAC address
char macAddr[18];

// The remote service we wish to connect to.
static BLEUUID serviceUUID("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");
// The characteristic of the remote service we are interested in.ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6
static BLEUUID    charUUID("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6");
//static BLEUUID    charUUID("ebe0ccc4-7a0a-4b0c-8a1a-6ff2997da3a6");

//////////////////////////////////////////////////// WIFI /////////////////////////////////

void connectWifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("");
  
  byte ar[6];
  WiFi.macAddress(ar);
  sprintf(macAddr,"%02X:%02X:%02X:%02X:%02X:%02X",ar[0],ar[1],ar[2],ar[3],ar[4],ar[5]);
}

void disconnectWifi() {
  WiFi.disconnect(true);
  Serial.println("WiFi disonnected");
}

//////////////////////////////////////////////////// END WIFI /////////////////////////////////

//////////////////////////////////////////////////// MQTT ////////////////////////////////////
void connectMqtt() {
  Serial.println("Connecting to MQTT...");
  client.setServer(MQTT_HOST, MQTT_PORT);

  while (!client.connected()) {
    if (!client.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.print("MQTT connection failed:");
      Serial.print(client.state());
      Serial.println("Retrying...");
      delay(MQTT_RETRY_WAIT);
    }
  }

  Serial.println("MQTT connected");
  Serial.println("");
}

void disconnectMqtt() {
  client.disconnect();
  Serial.println("MQTT disconnected");
}

//////////////////////////////////////////////////// END MQTT ////////////////////////////////////

void hibernate() {
  
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION * 1000000ll);
  Serial.println("Going to sleep now.");
  delay(100);
  esp_deep_sleep_start();
  
}

void delayedHibernate(void *parameter) {
  delay(EMERGENCY_HIBERNATE * 1000); // delay for five minutes
  Serial.println("Something got stuck, entering emergency hibernate...");
  hibernate();
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
      Serial.println("Connected");
    }

    void onDisconnect(BLEClient* pclient) {
      Serial.println("Disconnected");
      if (!connectionSuccessful) {
        Serial.println("RESTART");
        ESP.restart();
      }
    }
};

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  //char *pData,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  //float temp;
  //float humi;
  Serial.print("Notify callback for characteristic ");
  Serial.println(pBLERemoteCharacteristic->getUUID().toString().c_str());
  
  temp = (pData[0] | (pData[1] << 8)) * 0.01; //little endian 
  humi = pData[2];
  volt = ((pData[4] * 256) + pData[3]) / 1000.0;
  batt = (volt - 2.1) * 100.0;
  Serial.printf("temp = %.1f , humidity = %.1f , volt = %.1f , batt = %.1f \n", temp, humi, volt, batt);
  
  pClient->disconnect();
}

void registerNotification() {

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
  }
  Serial.println(" - Found our characteristic");
  
  pRemoteCharacteristic->registerForNotify(notifyCallback);

  
}

void setup() {
  Serial.begin(115200);



  // create a hibernate task in case something gets stuck
  xTaskCreate(delayedHibernate, "hibernate", 4096, NULL, 1, &hibernateTaskHandle);

  Serial.println("Initialize BLE-MI-T&H client...");
  delay(500);
  BLEDevice::init("ESP32");
  createBleClientWithCallbacks();
  
  delay(1000);
  connectSensor();
  registerNotification();

  delay(15000);
  
  connectWifi();
  connectMqtt();

  // MSG to MQTT
  char buffer1[64];
  char buffer2[64];
  char buffer3[64];
  char buffer4[64];
  snprintf(buffer1, 64, "%2.1f", temp);
  if (client.publish((baseTopic + "temperature").c_str(), buffer1)) {
      Serial.println("   >> Published temp");
  }
  snprintf(buffer2, 64, "%2.1f", humi);
  if (client.publish((baseTopic + "humidity").c_str(), buffer2)) {
      Serial.println("   >> Published humi");
  }
  snprintf(buffer3, 64, "%2.1f", volt);
  if (client.publish((baseTopic + "voltage").c_str(), buffer3)) {
      Serial.println("   >> Published voltage");
  }
  snprintf(buffer4, 64, "%2.1f", batt);
  if (client.publish((baseTopic + "battery").c_str(), buffer4)) {
      Serial.println("   >> Published battery");
  }
  
  delay(15000);
  // disconnect wifi and mqtt
  disconnectWifi();
  disconnectMqtt();
  delay(10000);
  // delete emergency hibernate task
  
  vTaskDelete(hibernateTaskHandle);
  // go to sleep now 
  hibernate();
  
}

void loop() {
  // do nothing
  delay(10000);
}

void createBleClientWithCallbacks() {
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
}

void connectSensor() {
  pClient->connect(htSensorAddress);
  connectionSuccessful = true;
}
