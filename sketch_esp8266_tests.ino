// Scan for other access points:
// https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/scan-class.html

// softAP and station mode simultaneously
// https://github.com/esp8266/Arduino/issues/119#issuecomment-157418957
// https://stackoverflow.com/questions/47345141/esp8266-wifi-ap-sta-mode

// Sending replies using multiple calls to the web server (to preserve RAM):
// https://github.com/esp8266/Arduino/issues/3205


// SPIFFS
// https://github.com/esp8266/arduino-esp8266fs-plugin/tree/0.4.0
// https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
//https://github.com/pellepl/spiffs/wiki/FAQ

// TODO: split program up (include .h and .cpp-files into the sketch, but edit elsewhere?)

// TODO: Report values for multiple sensors? (might be limited by RAM)?
// TODO: store name of sensors permanently in the flash file system?
// TODO: store sensors to view in flash filesystem + make them easy to select?
// TODO: Cache results for json object ?
// TODO: Support logging into an already available network either as an option, or when running as softAP)
// TODO: break down sending of different sensors into separate calls to the web server (to be nicer on RAM). see https://github.com/esp8266/Arduino/issues/3205
#include <DallasTemperature.h>

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "FS.h"

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

struct ConfigSoftAP
{
  ConfigSoftAP() : 
    _ip(192,168,1,1),
    _gateway(192,168,1,1),
    _subnet(255,255,255,0),
    _ssid{"TestAP"},
    _password{"testtest"},
    _modified(false)
  { /* no code */ }

  IPAddress const & getIp() const { return _ip; }
  bool setIp(String const & newIP) { return setIpIfValid(newIP, _ip); }

  IPAddress const & getGateway() const { return _gateway; }
  bool setGateway(String const & newIP) { return setIpIfValid(newIP, _gateway); }

  IPAddress const & getSubnet() const { return _subnet; }
  bool setSubnet(String const & newIP) { return setIpIfValid(newIP, _subnet); }

  const char* getSsid() const { return _ssid; }
  bool setSsid(String const & str)
  {
    if (str.length() < 1 || str.length() >= sizeof(_ssid))
    {
      return false;
    }
    strncpy(_ssid, str.c_str(), sizeof(_ssid));
    _modified = true;
    return true;
  }

  const char* getPassword() const { return _password; }
  bool setPassword(String const & str)
  {
    if (str.length() >= sizeof(_password))
    {
      return false;
    }
    strncpy(_password, str.c_str(), sizeof(_password));
    _modified = true;
    return true;
  }

  bool isModified() const { return _modified; };

  /** Save values to flash */
  bool save()
  {
    String ip = _ip.toString();
    String gateway = _gateway.toString();
    String subnet = _subnet.toString();
    
    StaticJsonDocument<512> json;
    json["ssid"] = _ssid;
    json["password"] = _password;
    json["ip"] = ip.c_str();
    json["gateway"] = gateway.c_str();
    json["subnet"] = subnet.c_str();

    char response[512] = {};
    size_t toWrite = serializeJson(json, response, sizeof(response));

    if (!toWrite || toWrite >= sizeof(response))
    {
      Serial.println("too much");
      return false; 
    }

//    Serial.print("Writing: ");
//    Serial.println(response);

    File configFile = SPIFFS.open("/config/wifi/softap", "w");
    if (!configFile) {
      Serial.println("file open failed");
      return false;
    }

    size_t written = configFile.println(response);
    if (!written) {
      Serial.println("not written");
      configFile.close();
      return false;
    }
    configFile.close();

    _modified = false;
    return true;
  }

  /** Load values from flash */
  bool load()
  {
    File configFile = SPIFFS.open("/config/wifi/softap", "r");
    if (!configFile) {
      Serial.println("not found");
      return false;
    }
  
    size_t size = configFile.size();
    if (size > 1024) {
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
    char const* keys[] = {"ip", "gateway", "subnet", "ssid", "password", nullptr};
    char const** it = keys;
    while (*it) {
      if (!json.containsKey(*it)) {
        Serial.printf("missing key %s", *it);
        return false;
      }
      it++;
    }

    ConfigSoftAP next;
    if (next.setIp(json["ip"]) &&
        next.setGateway(json["gateway"]) &&
        next.setSubnet(json["subnet"]) &&
        next.setSsid(json["ssid"]) &&
        next.setPassword(json["password"]))
    {
      next._modified = false;
      *this = next;
      return true;
    }
    Serial.println("malformed?");
    return false;
  }

  /** Update zero or more elements provided in json input
      @return true if validation OK and data (if any) updated. On error, no fields are updated
   */
  bool patch(char const * jsonString)
  {
    StaticJsonDocument<512> root;
    DeserializationError error = deserializeJson(root, jsonString);
    
    char const* keys[] = {"ip", "gateway", "subnet", "ssid", "password", nullptr};
    for (JsonPair const & kv : root.as<JsonObject>())
    {
      char const** it = keys;
      bool currentKeyValid = false;
      while (*it) {
        bool keyMatch = strcmp(kv.key().c_str(), *it) == 0;
        bool isString = kv.value().is<char const*>() || kv.value().is<char*>();
        if (keyMatch && isString)
        {
          currentKeyValid = true;
          break;
        }
        it++;
      }
      if (!currentKeyValid) {
        return false;
      }
    }

    ConfigSoftAP next = *this;
    if (root.containsKey("ip") && !next.setIp(root["ip"].as<char*>())) { return false; }
    if (root.containsKey("gateway") && !next.setGateway(root["gateway"].as<char*>())) { return false; }
    if (root.containsKey("subnet") && !next.setSubnet(root["subnet"].as<char*>())) { return false; }
    if (root.containsKey("ssid") && !next.setSsid(root["ssid"].as<char*>())) { return false; }
    if (root.containsKey("password") && !next.setPassword(root["password"].as<char*>())) { return false; }

    if (next == *this)
    {
      next._modified = false;
    }
    *this = next;
    return true;
  }

  bool operator==(ConfigSoftAP const& other) const {
    return other._ip == _ip &&
           other._gateway == _gateway &&
           other._subnet == _subnet &&
           other._ssid == _ssid &&
           other._password == _password;
  }

private:
  bool setIpIfValid(String const & str, IPAddress &ip) {
    bool ok = onlySetIpIfValid(str, ip);
    if (ok) {
      _modified = true;
    }
    return ok;
  }

  IPAddress _ip;
  IPAddress _gateway; ///< Quite useless, since we don't route traffic to another net
  IPAddress _subnet;

  /** WPA2 standard allows a maximum of 32 chars (although some routers only allow 31 char) */
  char _ssid[33];

  /** Passwords are by the WPA2 standard limited to be at least 8 characters long,
      and are not allowed to be longer than 63 characters. Leave empty for an open network. */
  char _password[64];

  bool _modified;
} configSoftAP;


bool serveFromSpiffs(String const & uri, const char* contenttype="text/html");
String deviceAddressToString(DeviceAddress const & da);
String sensorToString(int allSensorIndex);


template<class T, int N>
class CircularBuffer {
public:
  CircularBuffer() : _readPos(0), _writePos(0), _size(0) {
    
  }
  ~CircularBuffer() {
    // NO CODE
  }
  void fill(T value) // TODO: consider using ref (and overload it for rvalues)
  {
    _size = N;
    _readPos = 0;
    _writePos = 0;
    for (int i = 0; i < N; i++)
    {
      _data[i] = value;
    }
  }
  /** @return false if no more space available */
  bool push_back(const T& value) {
     if (_size == N) {
       return false;
     }
     _data[_writePos] = value;
     _writePos = getNextPos(_writePos);
     _size++;
  }
  void pop_front() {
    if (_size < 1)
    {
      _size = 0;
      return; // decide what to do in this error case
    }
    else
    {
      _readPos = getNextPos(_readPos);
      _size--;
    }
  }
  bool push_back_erase_if_full(const T& value) {
     while (_size >= N) {
       pop_front();
     }
     _data[_writePos] = value;
     _writePos = getNextPos(_writePos);
     _size++;
  }
  int size() const { return _size; }
  
  T& operator[](int pos)
  {
    if (pos > _size)
    {
      static T nullInitialized{};
      Serial.println("ERROR: pos > size in circular buffer");
      return nullInitialized; // todo decide what to do in this error case...
    }
    else
    {
      int offset = _readPos + pos;
      if (offset >= N)
        offset -= N;
      return _data[offset];
    }
  }
private:
  int getNextPos(int currentPos)
  {
    if ((currentPos + 1) < N)
      return currentPos + 1;
    else
      return 0;
  }
  T _data[N];
  int _readPos;
  int _writePos;
  int _size;
};



ESP8266WebServer server(80);

struct Sensor {
  int16_t index; // sensor index as determined by DallasTemperature class
  DeviceAddress deviceAddress;
  char id[17];
  char name[17];
  bool active;
  float lastValue;
  Sensor() : index(0), deviceAddress{}, id{}, name{}, active(false), lastValue{}
  { /* no code */ }
};

#define MAX_NUM_SENSORS 10
int16_t numAllSensors = 0;
Sensor allSensors[MAX_NUM_SENSORS];


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

/*
class RawTempToString {
  struct Entry {
    int16_t rawTemp;
    char cachedRepresentation[8]; // enough to hold "-127.00" and null termination
  }

  static CircularBuffer<Entry, 16> cache; // TODO: start using this cache for conversions
  
  public:
    static String convertToCelsius(int16_t raw) {
      float deg = DallasTemperature::rawToCelsius(raw);
      String ans(deg, 2);
      return ans;
    }

}
*/


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
  else
  {
    // TODO: implement
    sendError("only HTTP_GET supported");
  }
}

void handlePersist()
{
  if (server.method() == HTTP_GET)
  {
    // TODO: keep track of unsaved changes
    server.send(200, "application/javascript", "{\"persist_now\": 0, \"unsaved_changes\": 1}");
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
  Serial.print("Server listening on: ");
  Serial.println(WiFi.softAPIP());

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

void  stringToDeviceAddress(DeviceAddress da, String const & id)
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
