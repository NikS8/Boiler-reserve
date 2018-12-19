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

#define RESET_UPTIME_TIME 2592000000    //  30 * 24 * 60 * 60 * 1000 = 2592000000 // reset after 30 days uptime 
#define REST_SERVICE_URL "192.168.1.180"
#define REST_SERVICE_PORT 3010
char settingsServiceUri[] = "settings/boilerBack";
char intervalLogServiceUri[] = "/intervalLog/boilerBack";

bool isInitialized = false;

? double Irms1; глобальные нехорошо
? double Irms2; глобальные нехорошо
? double Irms3; глобальные нехорошо

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
  ! определить их номера
  //isDS18ParasitePowerModeOn = ds18Sensors.isParasitePowerMode();
  ? ds18Sensors.setWaitForConversion(false);

  ? getSettings(); выполняется только здесь и один раз - не читаемо - для меня лучше все фунция здесь
    
    String responseText = doRequest(settingsServiceUri, "");
  // TODO parse settings and fill values to variables
  //intervalLogServicePeriod = 10000;
  //settingsServiceUri 
  //intervalLogServiceUri
  //ds18Precision
  ? ds18Sensors.requestTemperatures(); тут температуру еще не измеряем
  intervalLogServiceTimer.setTimeout(intervalLogServicePeriod);
  intervalLogServiceTimer.restart();
  ds18ConversionTimer.setTimeout(DS18_CONVERSION_TIME);
  ds18ConversionTimer.restart();
  ? isInitialized = true;

}

void loop() {
  currentTime = millis();
  resetWhen30Days();

 ? void loop всегда только после удачного void setup
 // if (isInitialized) {
    realTimeService(); 
    intrevalLogService();
 // }
}

void realTimeService() {

  EthernetClient reqClient = httpServer.available();
  if (!reqClient) return;

  while (reqClient.available()) reqClient.read();

    ds18Sensors.requestTemperatures(); //   тут команда на измерение

  String data = createDataJsonString();

  reqClient.println("HTTP/1.1 200 OK");
  reqClient.println("Content-Type: application/json");
  reqClient.println("Content-Length: " + data.length());
  reqClient.println();
  reqClient.print(data);

  reqClient.stop();
}

String createDataJsonString() {
  String output;
  ? StaticJsonBuffer<200> jsonBuffer; попробовать DynamicJsonBuffer jsonBuffer;
  // или const int BUFFER_SIZE = JSON_OBJECT_SIZE(6) + JSON_ARRAY_SIZE(3); StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

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

****************************************************
//      Тип	                  Размер
//  JsonArray of N element	4 + 8 * N
//  JsonObject of N element	4 + 10 * N
//  Так как JsonObject > JsonArray 
//   то вариант на JsonArray
String createDataJsonString() {
  String output;

    const int BUFFER_SIZE = JSON_ARRAY_SIZE(3); 
    StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonArray& transDataArray = jsonDataObject.createNestedArray("boilerBack");
  
 // jsonDataObject["deviceId"] ="boilerBack";
 // jsonDataObject["version"] ="0.1";

  flowBoilerBackArray.add(getFlowData());  
  
  transDataArray.add(emon1.calcIrms(1480));
  transDataArray.add(emon2.calcIrms(1480));
  transDataArray.add(emon3.calcIrms(1480));

  for (uint8_t index = 0; index < ds18DeviceCount; index++) {
    DeviceAddress deviceAddress;
    ds18Sensors.getAddress(deviceAddress, index);
    transDataArray.add(dsAddressToString(deviceAddress));
    transDataArray.add(getTempC(deviceAddress));
  }

  jsonDataObject.printTo(Serial);
  Serial.println();
  jsonDataObject.printTo(output);
  return output;
}