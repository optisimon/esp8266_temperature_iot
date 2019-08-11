// using gdb!
// https://arduino-esp8266.readthedocs.io/en/latest/gdb.html

// esp8266 documentation
//https://github.com/esp8266/Arduino#documentation

// Scan for other access points:
// https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/scan-class.html


// softAP and station mode simultaneously
// https://github.com/esp8266/Arduino/issues/119#issuecomment-157418957
// https://stackoverflow.com/questions/47345141/esp8266-wifi-ap-sta-mode
// https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/generic-class.html

// Important implications on only one wifi channel:
// https://bbs.espressif.com/viewtopic.php?t=324


// Sending replies using multiple calls to the web server (to preserve RAM):
// https://github.com/esp8266/Arduino/issues/3205

// Different servers on the softAP and network interfaces
// https://arduino.stackexchange.com/questions/67269/create-one-server-in-ap-mode-and-another-in-station-mode

// SPIFFS
// https://github.com/esp8266/arduino-esp8266fs-plugin/tree/0.4.0
// https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
//https://github.com/pellepl/spiffs/wiki/FAQ

// TODO: FW update in web interface
// TODO: split program up (include .h and .cpp-files into the sketch, but edit elsewhere?)
// TODO: Should we do something when an interface disconnects / reconnects: https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/generic-class.html
// TODO: warn if softAP and network overlaps (web server only serves on one interface in that case)
// TODO: once connecting to other network, store channel and use it for softap only as well. (current workaround: bring up other network first)
// TODO: Report values for multiple sensors? (might be limited by RAM)?
// TODO: store sensors to view in flash filesystem + make them easy to select?
// TODO: break down sending of different sensors into separate calls to the web server (to be nicer on RAM). see https://github.com/esp8266/Arduino/issues/3205
#include <DallasTemperature.h>

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <FS.h>

#include "CircularBuffer.hpp"

const unsigned long time_between_1h_readings_ms = 10000UL; // 1000 ms seemed stable
const unsigned long time_between_24h_readings_ms = 60000UL;
const int oneWireBus = 4; // D2 is the same as gpio4 on my board...
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Address for temperature sensor to use (if no sensor matches this at boot, first sensor will be used).
DeviceAddress sensorAddress = {0x28,0xff,0xba,0xa4,0x64,0x14,0x03,0x13};

// Index of temperature sensor to use (will be updated at boot)
int sensorIndex = 0;

/** 
 *  Set ip adress only for valid IP adresses (no change of ip for invalid input).
 *  @return true if str was a valid IP address and was stored into ip.
 *  
 */
bool onlySetIpIfValid(String const & str, IPAddress &ip)
{
  IPAddress next;
  bool isValid = next.fromString(str);
  if (isValid) {
    ip = next;
  }
  return isValid;
}

/*
struct IJsonConfig()
{
  virtual bool patch(char const * jsonString) = 0;
  virtual bool save() = 0;
  virtual bool load() = 0;
  virtual bool isModified() const = 0;  
  virtual const char* getDefaultKeys() = 0;
}

bool areAllKeysKnownAndOfCorrectType(JsonDocument const & patch, const char* defaults)
{
  DynamicJsonDocument def(4*strlen(defaults)); // TODO: better heuristics...
  deserializeJson(def, defaults);

  check that all keys in patch exist in defaults and have the correct type
}
*/

#include "ConfigSoftAP.hpp"
ConfigSoftAP configSoftAP;

#include "ConfigNetwork.hpp"
ConfigNetwork configNetwork;


bool serveFromSpiffs(String const & uri, const char* contenttype="text/html");
String deviceAddressToString(DeviceAddress const & da);
void stringToDeviceAddress(DeviceAddress da, String const & id);
String sensorToString(int allSensorIndex);


ESP8266WebServer server(80);

struct Sensor {
  int16_t index; ///< sensor index as determined by DallasTemperature class
  DeviceAddress deviceAddress;
  char id[17];
  char name[17];
  bool active;
  float lastValue; ///< not persisted
  Sensor() : index(0), deviceAddress{}, id{}, name{}, active(false), lastValue{}
  { /* no code */ }
};

#define MAX_NUM_SENSORS 10
int16_t numAllSensors = 0;
Sensor allSensors[MAX_NUM_SENSORS];

struct ConfigSensors {
  bool patch(char const * jsonString) {
    // TODO: move in patching from web server code (which was accessing one sensor at a time)
    return false; // TODO: implement
  }
  bool save()
  {
    String s = R"EOF({"sensors":[)EOF";
    for (int i = 0; i < numAllSensors; i++)
    {
      if (i != 0) { s += ", "; }
      s += sensorToString(i);
    }
    s += "]}\n";
    
    File configFile = SPIFFS.open("/config/sensors", "w");
    if (!configFile) {
      Serial.println("file open failed");
      return false;
    }

    size_t written = configFile.println(s.c_str());
    if (!written) {
      Serial.println("not written");
      configFile.close();
      return false;
    }
    configFile.close();

    _modified = false;
    return true;
  }

  bool load() {
    // This will be a bit odd, as we populate the detected ones, and then patch them with saved names and "active" flags from flash
    populateAllSensors();

    File configFile = SPIFFS.open("/config/sensors", "r");
    if (!configFile) {
      Serial.println("not found");
      return false;
    }
  
    size_t size = configFile.size();
    if (size > 2048) {
      Serial.println("too large");
      return false;
    }
  
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    configFile.close();
  
    StaticJsonDocument<512> json;
    DeserializationError error = deserializeJson(json, buf.get());
    if (error) {
      Serial.println("deserialize fail");
      return false;
    }

    if (json.containsKey("sensors") && json["sensors"].is<JsonArray>())
    {
      JsonArray arr = json["sensors"];
      for (JsonObject sensor : arr)
      {
        char const* keys[] = {"id", "name", "active", nullptr};
        char const** it = keys;
        while (*it) {
          if (!sensor.containsKey(*it)) {
            Serial.printf("missing key sensor[].%s", *it);
            return false;
          }
          it++;
        }

        for (int i = 0; i < numAllSensors; i++)
        {
          if (strncasecmp(sensor["id"].as<const char*>(), allSensors[i].id, sizeof(allSensors[i].id)) == 0) // TODO: should this be case sensitive?
          {
            allSensors[i].active = sensor["active"].as<int>();
            strncpy(allSensors[i].name, sensor["name"].as<const char*>(), sizeof(allSensors[i].name));
            break;
          }
        }
      }
    }
    _modified = false;
    return true;
  }
  bool isModified() const { return _modified; }
  private:
  bool _modified;
  String sensorToString(int allSensorIndex)
  {
    String s = "{\"id\":\"" + deviceAddressToString(allSensors[allSensorIndex].deviceAddress) +
    "\", \"name\":\"" + allSensors[allSensorIndex].name +
    "\", \"active\":" + (allSensors[allSensorIndex].active ? "1" : "0") + "}";
    return s;
  }
} configSensors;


void populateAllSensors()
{
  numAllSensors = sensors.getDeviceCount();
  if (numAllSensors > MAX_NUM_SENSORS)
  {
    numAllSensors = MAX_NUM_SENSORS;
    Serial.print("ERROR: too many sensors connected");
  }
  for (int i = 0; i < numAllSensors; i++)
  {
    allSensors[i].index = i;
    memset(allSensors[i].deviceAddress, 0, sizeof(allSensors[i].deviceAddress));
    sensors.getAddress(allSensors[i].deviceAddress, i);
    strncpy(allSensors[i].id, deviceAddressToString(allSensors[i].deviceAddress).c_str(), sizeof(allSensors[i].id));
    snprintf(allSensors[i].name, sizeof(allSensors[i].name), "Sensor%d", allSensors[i].index);
    allSensors[i].active = false;
    allSensors[i].lastValue = 0;
  }
}


struct ServedSensor {
  int allSensorsIndex;
  CircularBuffer<float, 360> readings_1h;
  CircularBuffer<float, 1440> readings_24h;
};

int16_t numServedSensors = 0;
ServedSensor servedSensors[2] = {{}, {}}; // internal compiler error if only '= {};'

void handleSettings()
{
  // When running tests against main.html requiring a web server on the other end
  // one could run this in the source folder:
  // python3 -m http.server --bind 127.0.0.1
  // and browse to 127.0.0.1:8000

  bool ok = serveFromSpiffs("/settings.html");
  if (!ok)
  {
    server.send(404, "text/plain", "File not found");
  }
}

void handleRoot()
{
  // When running tests against main.html requiring a web server on the other end
  // one could run this in the source folder:
  // python3 -m http.server --bind 127.0.0.1
  // and browse to 127.0.0.1:8000

  bool ok = serveFromSpiffs("/main.html");
  if (!ok)
  {
    server.send(404, "text/plain", "File not found");
  }
}


String getSensorStart(int allSensorsIndex) {
  Sensor const & sensor = allSensors[allSensorsIndex];
  String s = "{\"id\":\"" + String(sensor.id) + "\", \"name\":\"" + String(sensor.name) + "\", \"readings\":[";
  return s;
}


bool serveFromSpiffs(String const & uri, const char* contenttype)
{
  if (uri.indexOf("..") != -1)
  {
    // prevent nasty people (in case SPIFFS now or in the future support "/html/../config/")
    return false;
  }

  String path = "/html";
  if (!uri.startsWith("/")) {
    path += "/";
  }
  path += uri;

  if (SPIFFS.exists(path))
  {
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contenttype);
    file.close();
    return true;
  }
  
  return false;
}

void sendError(String errMsg)
{
  errMsg = "ERROR: " + errMsg + "\n" + "URI: " + server.uri();
  server.send(400, "text/plain", errMsg);
}

void returnConfigReplaceField(const char* location, const char* fieldToReplace, const char* newValue)
{
  File configFile = SPIFFS.open(location, "r");
  if (!configFile) {
    sendError("Failed to open config file");
    return;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    sendError("Config file size is too large");
    return;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  configFile.close();

  StaticJsonDocument<512> json;
  DeserializationError error = deserializeJson(json, buf.get());
  if (error)
  {
    String e = String("Failed to parse config file\n===CONTENT===\n") + buf.get() + "\n======\n";
    sendError(e);
    return;
  }

  if (!json.containsKey(fieldToReplace))
  {
    sendError("Config file damaged??");
    return;
  }
  json[fieldToReplace] = "********";
  String response;
  serializeJson(json, response);
  server.send(200, "application/javascript", response);
}

void handleWifiSoftAP()
{
  if (server.method() == HTTP_GET)
  {
    returnConfigReplaceField("/config/wifi/softap", "password", "********");
  }
  else if (server.method() == HTTP_PATCH && server.hasArg("plain"))
  {
    String const json = server.arg("plain");

    bool ok = configSoftAP.patch(json.c_str());
    if (ok && configSoftAP.isModified()) {
      ok = configSoftAP.save();
    }
    if (ok)
    {
      server.send(200, "text/plain", "OK");
    }
    else 
    {
      server.send(400, "text/plain", "ERROR"); // TODO: which status code???
    }
  }
  else
  {
    sendError("???");
  }
}

void handleWifiNetwork()
{
  if (server.method() == HTTP_GET)
  {
    returnConfigReplaceField("/config/wifi/network", "password", "********");
  }
  else if (server.method() == HTTP_PATCH && server.hasArg("plain"))
  {
    String const json = server.arg("plain");

    bool ok = configNetwork.patch(json.c_str());
    if (ok && configNetwork.isModified()) {
      ok = configNetwork.save();
    }
    if (ok)
    {
      server.send(200, "text/plain", "OK");
    }
    else 
    {
      server.send(400, "text/plain", "ERROR"); // TODO: which status code???
    }
  }
  else
  {
    sendError("???");
  }
}

void handlePersist()
{
  if (server.method() == HTTP_GET)
  {
    // TODO: keep track of unsaved changes
    server.send(200, "application/javascript", "{\"persist_now\": 0, \"unsaved_changes\": 1}");
  }
  else if (server.method() == HTTP_PATCH && server.hasArg("plain"))
  {
    String const json = server.arg("plain");
    
    const size_t capacity = JSON_OBJECT_SIZE(2) + 200;
    StaticJsonDocument<capacity> root;
    DeserializationError error = deserializeJson(root, json.c_str());
    
    if (error)
    {
      Serial.println("Parsing failed!");
      server.send(400, "text/plain", "ERROR"); // TODO: which status code???
      return;
    }
    else 
    {
      if (root.containsKey("persist_now") && root["persist_now"].is<int>() && root["persist_now"].as<int>() != 0)
      {
        bool ok = configSensors.save();
        if (ok)
        {
          server.send(200, "text/plain", "OK");
        }
        else
        {
          server.send(400, "text/plain", "ERROR saving sensors");
        }
      }
    }
  }
  else
  {
    // TODO: implement
    sendError("only HTTP_GET supported");
  }
}

void handleNotFound()
{
  if (server.uri().startsWith("/api/sensors/"))
  {
    String id = server.uri().substring(13);
    if (id.length() == 16)
    {
      //DeviceAddress da;
      //stringToDeviceAddress(da, id);
      for (int i = 0; i < numAllSensors; i++)
      {
        if (strncasecmp(id.c_str(), allSensors[i].id, 16) == 0) // TODO: should this be case sensitive?
        {
          if (server.method() == HTTP_GET)
          {
            String s = sensorToString(i);
            server.send(200, "application/javascript", s);
            return;
          }
          else if (server.method() == HTTP_PATCH && server.hasArg("plain"))
          {
            String const json = server.arg("plain");
            
            const size_t capacity = JSON_OBJECT_SIZE(2) + 200;
            StaticJsonDocument<capacity> root;
            DeserializationError error = deserializeJson(root, json.c_str());
            
            if (error)
            {
              Serial.println("Parsing failed!");
              server.send(400, "text/plain", "ERROR"); // TODO: which status code???
              return;
            }
            else 
            {
              if (root.containsKey("name"))
              {
                strncpy(allSensors[i].name, root["name"].as<char*>(), sizeof(allSensors[i].name));
                allSensors[i].name[sizeof(allSensors[i].name) - 1] = '\0';
              }
              if (root.containsKey("active"))
              {
                allSensors[i].active = (root["active"].as<int>() == 0) ? false : true;
              }

              server.send(200, "text/plain", "OK");
              return;
              // No idea what to do...
            }
          }
        }
      }
    }
  }
  else if (serveFromSpiffs(server.uri()))
  {
    return; // all went well
  }
  
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: HTTP_";
  switch(server.method())
  {
    case HTTP_ANY: message += "ANY"; break;
    case HTTP_GET: message += "GET"; break;
    case HTTP_POST: message += "POST"; break;
    case HTTP_PUT: message += "PUT"; break;
    case HTTP_PATCH: message += "PATCH"; break;
    case HTTP_DELETE: message += "DELETE"; break;
    case HTTP_OPTIONS: message += "OPTIONS"; break;
    default: message += "????";
  }
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);

  Serial.write(message.c_str());
}

String sensorToString(int allSensorIndex)
{
  String s = "{\"id\":\"" + deviceAddressToString(allSensors[allSensorIndex].deviceAddress) +
  "\", \"name\":\"" + allSensors[allSensorIndex].name +
  "\", \"active\":" + (allSensors[allSensorIndex].active ? "1" : "0") +
  ", \"lastValue\":" + String(allSensors[allSensorIndex].lastValue, 2) + "}";
  // TODO: update when we have persistent name storage
  return s;
}

void handleSensors()
{
  String s = R"EOF({"sensors":[)EOF";
  for (int i = 0; i < numAllSensors; i++)
  {
    if (i != 0) { s += ", "; }
    s += sensorToString(i);
  }
  s += "]}\n";
  server.send(200, "application/javascript", s);
}

void handleSensors_1h()
{
  String s = R"rawliteral({"sensors":[)rawliteral";
  for (int k = 0; k < numServedSensors; k++)
  {
    if (k != 0) { s += ", "; }
    s += getSensorStart(servedSensors[k].allSensorsIndex);
    for (int i = 0; i < servedSensors[k].readings_1h.size(); i++)
    {
      if (i != 0) {
        s += ", ";
      }
      String val(servedSensors[k].readings_1h[i], 2);
      s += val;
    }
    s += "]}\n"; // sensor end
  }
  s += "]}\n";
  server.send(200, "application/javascript", s);
}

void handleSensors_24h()
{
  String s = R"rawliteral({"sensors":[)rawliteral";
  for (int k = 0; k < numServedSensors; k++)
  {
    if (k != 0) { s += ", "; }
    s += getSensorStart(servedSensors[k].allSensorsIndex);
    for (int i = 0; i < servedSensors[k].readings_24h.size(); i++)
    {
      if (i != 0) {
        s += ", ";
      }
      String val(servedSensors[k].readings_24h[i], 2);
      s += val;
    }
    s += "]}\n"; // sensor end
  }
  s += "]}\n";
  server.send(200, "application/javascript", s);
}

void setup()
{
  // Do not automatically connect on power on to the last used access point.
  // Do not start things automatically + reduce wear on flash
  // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/generic-class.html
  // https://github.com/esp8266/Arduino/issues/1054
  WiFi.persistent(false);
  
  WiFi.disconnect();
  WiFi.softAPdisconnect();
  WiFi.setAutoConnect(false);
    
  Serial.begin(115200);
  while(!Serial)
  {
    ; // wait for serial port to connect. only needed for native USB port
  }
  Serial.println();

  SPIFFS.begin();

  Serial.print("Loading softAP config from flash ... ");
  bool softApLoadSuccess = configSoftAP.load();
  Serial.println( softApLoadSuccess ? "Ready" : "Failed!");
  Serial.flush();

  // Setting DNS host name TODO: check which interfaces are effected.
  WiFi.hostname(configSoftAP.getSsid());

  Serial.print("Loading Network config from flash ... ");
  bool networkLoadSuccess = configNetwork.load();
  Serial.println( networkLoadSuccess ? "Ready" : "Failed!");
  Serial.flush();

  // TODO: connect to wifi network etc...
  if (configNetwork.getEnabled())
  {
    WiFi.setAutoReconnect(true); // attempt to reconnect to an access point in case it is disconnected.
    
    Serial.print("Setting network config for \"");
    Serial.print(configNetwork.getSsid());
    Serial.print("\" ... ");
    bool networkConfigSuccess = false;
    if (configNetwork.getAssignment() == ConfigNetwork::Assignment::STATIC)
    {
      networkConfigSuccess = WiFi.config(
        configNetwork.getStaticIp(),
        configNetwork.getStaticGateway(),
        configNetwork.getStaticSubnet()
      ); // TODO: consider adding custom DNS settings as well
    } else {
      IPAddress zeroIp(0,0,0,0);
      networkConfigSuccess = WiFi.config(zeroIp, zeroIp, zeroIp); // TODO: consider adding custom DNS settings as well
    }    
    Serial.println( networkConfigSuccess ? "Ready" : "Failed!");
    Serial.flush();

    if (networkConfigSuccess)
    {
      Serial.printf("Connecting to \"%s\" ... ", configNetwork.getSsid());
      bool tmp = WiFi.begin(configNetwork.getSsid(), configNetwork.getPassword());
      delay(50); // TODO: here since other people had it... ()
      switch(WiFi.waitForConnectResult())
      {
        case WL_CONNECTED: Serial.println("Connected"); break;
        case WL_NO_SSID_AVAIL: Serial.println("SSID cannot be reached"); break;
        case WL_CONNECT_FAILED: Serial.println("Connect failed (password incorrect?)"); break;
        case WL_IDLE_STATUS: Serial.println("when Wi-Fi is in process of changing between statuses"); break;
        case WL_DISCONNECTED: Serial.println("module not in station mode"); break;
        case -1: Serial.println("Timed out"); break;
        default: Serial.println("Failed!"); break;
      }
    }
  }

// Good documentation https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/station-class.html
// More info about softAP WIFI configuration at https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/soft-access-point-class.html
// More info about strange softAP password requirements at https://github.com/esp8266/Arduino/issues/1141
  Serial.print("Setting soft-AP configuration ... ");
  boolean softApConfigSuccess = WiFi.softAPConfig(
    configSoftAP.getIp(),
    configSoftAP.getGateway(),
    configSoftAP.getSubnet()
  );
  Serial.println( softApConfigSuccess ? "Ready" : "Failed!");
  Serial.flush();

  Serial.print("Setting soft-AP ... ");
  boolean softApStartSuccess = WiFi.softAP(
    configSoftAP.getSsid(),
    configSoftAP.getPassword(),
    /* channel*/ 1,
    /* hidden */ false,
    /*max_connection (default 4)*/ 8
  );
  Serial.println( softApStartSuccess ? "Ready" : "Failed");
  Serial.flush();

  // TODO: should this be configurable (might confuse more people)
  Serial.printf("Starting mDNS responder (as \"%s.local\")... ", configSoftAP.getSsid()); // using that on the remote network as well
  Serial.println(MDNS.begin(configSoftAP.getSsid()) ? "Ready" : "Failed");

  server.on("/", handleRoot);
  //server.on("/main.html", handleRoot);
  //server.on("/spiffs_test.html", spiffsTest);
  //server.on("/settings.html", handleSettings);
  server.on("/api/sensors", handleSensors);
  server.on("/api/sensors/", handleSensors);
  server.on("/api/readings/1h", handleSensors_1h);
  server.on("/api/readings/24h", handleSensors_24h);
  server.on("/api/wifi/softap", handleWifiSoftAP);
  server.on("/api/wifi/network", handleWifiNetwork);
  server.on("/api/persist", handlePersist);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.print("Server listening on: softAP:");
  Serial.print(WiFi.softAPIP());
  Serial.print(", STA:");
  Serial.print(WiFi.localIP());
  Serial.println("");

  MDNS.addService("http", "tcp", 80);

  for (int i = 0; i < 2; i++)
  {
    servedSensors[i].readings_1h.fill(0.0f);
    servedSensors[i].readings_24h.fill(0.0f);
  }

  Serial.print("Starting temperature sensor monitoring... ");
  sensors.begin(); // TODO: do we have a return status??
  Serial.print(sensors.getDeviceCount());
  Serial.println(" devices found:");

  populateAllSensors();
  
  numServedSensors = sensors.getDeviceCount();
  if (numServedSensors > 2)
  {
    numServedSensors = 2;
  }
  for (int i = 0; i < sensors.getDeviceCount(); i++)
  {
    DeviceAddress da = {};
    sensors.getAddress(da, i);
    if (memcmp(da, sensorAddress, 8) == 0)
    {
      sensorIndex = i;
    }
  }

  for (int i = 0; i < sensors.getDeviceCount(); i++)
  {
    DeviceAddress da = {};
    sensors.getAddress(da, i);
    Serial.print("    0x");
    Serial.print(deviceAddressToString(da));
    Serial.println( i == sensorIndex ? " <-- will be used":"");
  }
  
  // update sensorAddress to match the actually used sensor
  sensors.getAddress(sensorAddress, sensorIndex);

  servedSensors[0].allSensorsIndex = sensorIndex;
  allSensors[sensorIndex].active = true;

  if (numServedSensors > 1) {
    servedSensors[1].allSensorsIndex = 1;
    allSensors[1].active = true;
  }

  Serial.print("Loading saved sensor configurations ... ");
  Serial.println(configSensors.load() ? "Ready":"Failed!");
}

String deviceAddressToString(DeviceAddress const & da)
{
  String s;
  for (int j = 0; j < 8; j++)
  {
    String add(da[j], HEX);
    if (add.length() == 1)
    {
      s += "0";
    }
    s += add;
  }
  return s;
}

void stringToDeviceAddress(DeviceAddress da, String const & id)
{
  da = {};
  for (int i = 0; i < 8; i++)
  {
    String s = id.substring(i*2, i*2+2);
    unsigned long d = strtoul(s.c_str(), nullptr, 16);
    da[i] = d;
  }
}


void readSensors(bool shouldRead1h, bool shouldRead24h)
{
  sensors.requestTemperatures();

  for (int16_t i = 0; i < numAllSensors; i++)
  {
    float temperatureCelcius = sensors.getTempC(allSensors[i].deviceAddress);
    allSensors[i].lastValue = temperatureCelcius;
    Serial.print(temperatureCelcius);

    for (int j = 0; j < numServedSensors; j++)
    {
      if (servedSensors[j].allSensorsIndex == i)
      {
        if (shouldRead1h) {
          servedSensors[j].readings_1h.push_back_erase_if_full(temperatureCelcius);
        }
        if (shouldRead24h) {
          servedSensors[j].readings_24h.push_back_erase_if_full(temperatureCelcius);
        }
      }
    }
    Serial.print(" ");
  }
  Serial.println();
}

void loop()
{
  Serial.println("loop()");

  readSensors(true, true);

  bool shouldRead1h = false;
  bool shouldRead24h = false;

  unsigned long startMillis_1h = millis();
  unsigned long startMillis_24h = startMillis_1h; // TODO: clean this up?

  while (true)
  {
    //Serial.printf("Stations connected = %d\n", WiFi.softAPgetStationNum());
    //delay(3000);
  
    // While busy waiting for next reading - handle web requests
    do
    {
      MDNS.update(); // NOTE are some bugs in : https://github.com/esp8266/Arduino/issues/4790
      server.handleClient();
      delay(20); // TODO: is this needed??
      //            Working combination is 500ms / reading + 10ms here. (ap most often there)
      //            Non-working combination is 500ms / reading + 1ms here (ap disapperas)
      //            Working rock stable: 1000ms / 20ms
      shouldRead1h = (millis() - startMillis_1h) >= time_between_1h_readings_ms;
      shouldRead24h = (millis() - startMillis_24h) >= time_between_24h_readings_ms;
    }
    while (! (shouldRead1h || shouldRead24h));
    
    readSensors(shouldRead1h, shouldRead24h);

    if (shouldRead1h) {
      startMillis_1h += time_between_1h_readings_ms;
      shouldRead1h = false;
    }

    if (shouldRead24h) {
      startMillis_24h += time_between_24h_readings_ms;
      shouldRead24h = false;
    }
  }
}
