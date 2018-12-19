/*
 * boilerBack scetch
 * 
 */

#include <ArduinoJson.h>
#include <Ethernet2.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EmonLib.h>
#include <RBD_Timer.h>

#define RESET_UPTIME_TIME 30 * 24 * 60 * 60 * 1000 // reset after 30 days uptime 
#define REST_SERVICE_URL "192.168.1.180"
#define REST_SERVICE_PORT 3010
char settingsServiceUri[] = "settings/boilerBack";
char intervalLogServiceUri[] = "/intervalLog/boilerBack";

bool isInitialized = false;

double Irms1;
double Irms2;
double Irms3;

// settings
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
EthernetServer httpServer(40250);
EthernetClient httpClient;

EnergyMonitor emon1;
EnergyMonitor emon2;
EnergyMonitor emon3;

#define PIN_ONE_WIRE_BUS 9
#define DS18_CONVERSION_TIME 750 / (1 << (12 - ds18Precision))
unsigned short ds18DeviceCount;
bool isDS18ParasitePowerModeOn;
uint8_t ds18Precision = 9;
OneWire ds18wireBus(PIN_ONE_WIRE_BUS);
DallasTemperature ds18Sensors(&ds18wireBus);

#define PIN_FLOW_SENSOR 2
#define PIN_INTERRUPT_FLOW_SENSOR 0
#define FLOW_SENSOR_CALIBRATION_FACTOR 5
volatile int flowPulseCount;

// time
unsigned long currentTime;
unsigned long flowSensorLastTime;
// settings intervals
unsigned int intervalLogServicePeriod = 10000;
// timers
RBD::Timer intervalLogServiceTimer;
RBD::Timer ds18ConversionTimer;

// TODO optimize variables data length
void setup() {
  Serial.begin(9600);
  Ethernet.begin(mac);
  while (!Serial) continue;

  pinMode( A1, INPUT );
  pinMode( A2, INPUT );
  pinMode( A3, INPUT );

  emon1.current(1, 8.4);
  emon2.current(2, 8.4);
  emon3.current(3, 8.4);

  pinMode(PIN_FLOW_SENSOR, INPUT);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, RISING);
  sei();

  httpServer.begin();

  ds18Sensors.begin();
  ds18DeviceCount = ds18Sensors.getDeviceCount();
  isDS18ParasitePowerModeOn = ds18Sensors.isParasitePowerMode();
  ds18Sensors.setWaitForConversion(false);

  getSettings();
}
  // TODO create function for request temp and save last request time

void getSettings() {
  String responseText = doRequest(settingsServiceUri, "");
  // TODO parse settings and fill values to variables
  //intervalLogServicePeriod = 10000;
  //settingsServiceUri 
  //intervalLogServiceUri
  //ds18Precision
  ds18Sensors.requestTemperatures();
  intervalLogServiceTimer.setTimeout(intervalLogServicePeriod);
  intervalLogServiceTimer.restart();
  ds18ConversionTimer.setTimeout(DS18_CONVERSION_TIME);
  ds18ConversionTimer.restart();
  isInitialized = true;
}

void loop() {
  currentTime = millis();
  resetWhen30Days();

  if (isInitialized) {
    realTimeService(); 
    intrevalLogService();
  }
}

void realTimeService() {

  EthernetClient reqClient = httpServer.available();
  if (!reqClient) return;

  while (reqClient.available()) reqClient.read();

  String data = createDataJsonString();

  reqClient.println("HTTP/1.1 200 OK");
  reqClient.println("Content-Type: application/json");
  reqClient.println("Content-Length: " + data.length());
  reqClient.println();
  reqClient.print(data);

  reqClient.stop();
}

void intrevalLogService() {
  if (intervalLogServiceTimer.getInverseValue() <= DS18_CONVERSION_TIME) {
    ds18RequestTemperatures();
  }

  if (intervalLogServiceTimer.onRestart()) {
    String data = createDataJsonString();

    String response = doRequest(intervalLogServiceUri, data);
    Serial.println(response);
  }
}

void ds18RequestTemperatures () {
  if (ds18ConversionTimer.onRestart()) {
    ds18Sensors.requestTemperatures();
  }
}

void flowSensorPulseCounter () {
   flowPulseCount++;
}

String createDataJsonString() {
  String output;
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& jsonDataObject = jsonBuffer.createObject();
  
  jsonDataObject["deviceId"] ="boilerBack";
  jsonDataObject["version"] ="0.1";

  jsonDataObject["flowBoilerBack"] = getFlowData();  
  
  JsonArray& transDataArray = jsonDataObject.createNestedArray("trans");
  transDataArray.add(emon1.calcIrms(1480));
  transDataArray.add(emon2.calcIrms(1480));
  transDataArray.add(emon3.calcIrms(1480));

  JsonObject& ds18JsonDataObject = jsonDataObject.createNestedObject("ds18");
  for (uint8_t index = 0; index < ds18DeviceCount; index++) {
    DeviceAddress deviceAddress;
    ds18Sensors.getAddress(deviceAddress, index);
    ds18JsonDataObject.set( dsAddressToString(deviceAddress), ds18Sensors.getTempC(deviceAddress));
  }

  jsonDataObject.printTo(Serial);
  Serial.println();
  jsonDataObject.printTo(output);
  return output;
}

// SENSORS 
int getFlowData() {
  static int flowSensorPulsesPerSecond;

  if (intervalLogServiceTimer.isActive()) { // just return previous value if there is call not for intervalLogService
    return flowSensorPulsesPerSecond;
  }

  flowSensorPulsesPerSecond = (millis() - flowSensorLastTime) / 1000 * flowPulseCount;
  flowSensorLastTime = millis();
  flowPulseCount = 0;

  return flowSensorPulsesPerSecond;
}

// UTILS
void resetWhen30Days () {
  if (millis() > (RESET_UPTIME_TIME)) {
    // do reset
  }
}

String doRequest(char reqUri, String reqData) {
  String responseText;

  if (httpClient.connect(REST_SERVICE_URL, REST_SERVICE_PORT)) {  //starts client connection, checks for connection
    Serial.println("connected");

    if (reqData.length()) { // do post request
      httpClient.println((char) "POST" + reqUri + "HTTP/1.1");
      //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
      httpClient.println("Content-Type: application/json;");
      httpClient.println("Content-Length: " + reqData.length());
      httpClient.println();
      httpClient.print(reqData);
    } else { // do get request
      httpClient.println( (char) "GET" +  reqUri + "HTTP/1.1");
      //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
      httpClient.println("Connection: close");  //close 1.1 persistent connection  
      httpClient.println(); //end of get request
    }
  } else {
    Serial.println("connection failed"); //error message if no client connect
    Serial.println();
  }

  while (httpClient.connected() && !httpClient.available()) {
    delay(1);
  } //waits for data
  while (httpClient.connected() || httpClient.available()) { //connected or data available
    responseText += httpClient.read(); //places captured byte in readString
  }

  return responseText;
}

String dsAddressToString(DeviceAddress deviceAddress) {
  String address;
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16 ) address += "0";
    address += String(deviceAddress[i], HEX);
  }
  return address;
}

bool readRequest(EthernetClient& client) {
  bool currentLineIsBlank = true;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n' && currentLineIsBlank) {
        return true;
      } else if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
    }
  }
  return false;
}