// Scan for other access points:
// https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/scan-class.html

// softAP and station mode simultaneously
// https://github.com/esp8266/Arduino/issues/119#issuecomment-157418957
// https://stackoverflow.com/questions/47345141/esp8266-wifi-ap-sta-mode

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

const unsigned long time_between_1h_readings_ms = 10000UL; // 1000 ms seemed stable
const unsigned long time_between_24h_readings_ms = 60000UL;
const int oneWireBus = 4; // D2 is the same as gpio4 on my board...
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Address for temperature sensor to use (if no sensor matches this at boot, first sensor will be used).
DeviceAddress sensorAddress = {0x28,0xff,0xba,0xa4,0x64,0x14,0x03,0x13};

// Index of temperature sensor to use (will be updated at boot)
int sensorIndex = 0;

IPAddress local_IP(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

const char* SSID = "TestAP";
const char* password = "testtest"; // Password needs to be at least 8 characters


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
  
  static const char s[] PROGMEM = R"rawliteral(
<html>
<head>
<title>TempViewer - Settings</title>
</head>
<body onload="myOnLoad()">

<div>
Settings

<button onclick="window.location.href='main.html'" style="position: absolute; right: 0;">Values</button>

</div>

<div id="settings">
<fieldset>
  <legend>Waiting for sensors:</legend>
</fieldset>
</div>

<script>

// TODO: remove this?
var sensors = [ { "id":"0000000000000000", "name":"No Data", "readings":[0.0]} ];

function renameSensor(sensorId, sensorName) {
  var newName = prompt("Enter new name for sensor " + sensorId, sensorName)
  myPatch("sensors/" + sensorId, { "name": newName });
}

function myPatch(url, data)
{
  var xmlhttp = new XMLHttpRequest();
  xmlhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      myRefresh();
    }
    else if (this.readyState == 4)
    {
      console.log("PATCH failed for url'" + url + "' and object '" + JSON.stringify(data) + "'");
      myRefresh();
    }
  }
  
  xmlhttp.open("PATCH", url, true);
  xmlhttp.setRequestHeader("Content-Type", "application/json;charset=UTF-8");
  xmlhttp.send(JSON.stringify(data));
}

function handleClick(checkbox) {
  myPatch("sensors/" + checkbox.id, {"active": checkbox.checked ? 1 : 0 });
  console.log("Checkbox click callback:" + JSON.stringify(checkbox.checked));
}

function myRefresh()
{
  var xmlhttp = new XMLHttpRequest();
  var url = "sensors";
  xmlhttp.onreadystatechange = function() {
    var h = document.getElementById("myLastValue");
    var d = document.getElementById("myLastUpdate");
    if (this.readyState == 4 && this.status == 200) {
      var myArr = JSON.parse(this.responseText);
      sensors = myArr["sensors"];
      
      var s = document.getElementById("settings")
      var str = '<fieldset>\n<legend>Temperature sensors:</legend>\n';
      for (var i = 0; i < sensors.length; i++)
      {
    str += '<div>\n<input type="checkbox" id="'  + sensors[i].id + 
    '" name="' + sensors[i].name + '"' +
    ' onclick="handleClick(this);" ' +
    (sensors[i].active ? 'checked>' : '>') + 
    '<label for="' + sensors[i].id + '">' +
    sensors[i].name + ': ' + 'id=' + sensors[i].id + ', last value='  +
    sensors[i].lastValue.toFixed(2) + '</label>';
    
    str += '<button onclick="renameSensor(\'' + sensors[i].id + '\', \'' + sensors[i].name + '\')"> Rename</button>';
    
    str += '</div>\n';
    }
    str += '</fieldset>\n';
    s.innerHTML = str;
    
/*
      if (sensors[0]["readings"].length != 0)
      {
        h.innerHTML = sensors[0]["readings"][sensors[0]["readings"].length - 1].toFixed(2);
        var today = new Date();
        d.innerHTML = "" + today.getFullYear() + "-" + 
                           String(today.getMonth() + 1).padStart(2, '0') + "-" +
                           String(today.getDate()).padStart(2, '0') + " " +
                           String(today.getHours()).padStart(2, '0') + ":" +
                           String(today.getMinutes()).padStart(2, '0') + ":" +
                           String(today.getSeconds()).padStart(2, '0')
      } else {
        h.innerHTML = "UNAVAILABLE";
      }
      */
    }
    else if (this.status == 404)
    {
      sensors = [ { "id":"0000000000000000", "name":"No Data", "readings":[0.0]} ];
      h.innerHTML = "UNAVAILABLE";
      d.innerHTML = "UNAVAILABLE";
      myRedraw();
    }
  };
  xmlhttp.open("GET", url, true);
  xmlhttp.send();
}

function myOnLoad() {
  var c = document.getElementById("settings");
  
  initialize();

  function initialize() {
    myRefresh();
    setInterval(myRefresh, 100000); // used to be 10000
  }
}
</script>

</body>

</html>
)rawliteral";
  server.send_P(200, "text/html", s);
}



void handleRoot()
{
  // created string by running
  // cat main.html | sed -e 's/\"/\\"/g' -e 's/$/\\n\"/' -e 's/^/\"/'
  //
  // When running tests against main.html requiring a web server on the other end
  // one could run this in the source folder:
  // python3 -m http.server --bind 127.0.0.1
  // and browse to 127.0.0.1:8000
  
  //String s = "<h1>Something is working! /sg</h1><p>Readings: ";
  static const char s[] PROGMEM = R"rawliteral(
<html>
<head>
<style>
body
{
  display:table;
  width:100%;
  height:100%;
}
div
{
  display:table-row;
}
div#canvasDiv
{
  height:100%;
  padding:0px;
}
</style>
<title>TempViewer</title>
</head>
<body onload="myOnLoad()">

<div>Last Value: <span id="myLastValue"></span>.
Last Update: <span id="myLastUpdate">No readings yet</span>.
Trend for
<select id="myDurationSelect" onchange="myDurationChanged()">
 <option value="1h" selected="selected">last hour</option>
 <option value="24h">last 24 hours</option>
</select>
<button onclick="window.location.href='settings.html'" style="position: absolute; right: 0;">Settings</button>
</div>

<div id="canvasDiv">
<canvas id="myCanvas", width="80", height="300" style="border:1px solid #808080;">
Sorry, your browser does not support the canvas element!
</canvas>
</div>

<script>
var globalRequestDuration = "";
var sensors = [ { "id":"0000000000000000", "name":"No Data", "readings":[0.0]} ];

function myDurationChanged() {  
  var s = document.getElementById("myDurationSelect");
  globalRequestDuration = s.options[s.selectedIndex].value;
  myOnLoad();
}
myDurationChanged();

function myRedraw() {
  var c = document.getElementById("myCanvas");
  var ctx = c.getContext("2d");
  ctx.clearRect(0, 0, c.width, c.height);

  ctx.beginPath();
  ctx.strokeStyle = 'blue';
  ctx.lineWidth = '5';
  ctx.strokeRect(0, 0, c.width, c.height);

  ctx.beginPath();
  ctx.lineWidth = '1';
  var scaleX = c.width * 1.0 / sensors[0]["readings"].length;
  var maxY = 50;
  var scaleY = c.height * 1.0 / maxY;

  // Make grid
  if (globalRequestDuration == "1h")
  {
    var divisionsX = 6;
  }
  else
  {
    var divisionsX = 24;
  }
  
  var divisionsY = 5;
  ctx.strokeStyle = "blue";
  ctx.fillStyle = ctx.strokeStyle;
  ctx.font="100% sans-serif";
  for (var x = 0; x < divisionsX; x++)
  {
    ctx.moveTo(x * (c.width - 1) / divisionsX, 0);
    ctx.lineTo(x * (c.width - 1) / divisionsX, c.height);
  }
  var rightOfYValues = 5;
  for (var y = 0; y < divisionsY; y++)
  {
    ctx.moveTo(0, y * (c.height - 1) / divisionsY);
    ctx.lineTo(c.width-1, y * (c.height - 1) / divisionsY);
    var txt = ((divisionsY - y) * maxY / divisionsY).toFixed(1);
    ctx.fillText(txt, 5, y * (c.height - 1) / divisionsY - 1);
    rightOfYValues = Math.max(rightOfYValues, ctx.measureText(txt).width + 5 + 5);
  }
  ctx.stroke();

  // draw curve
  for (var sensor = 0; sensor < sensors.length; sensor++)
  {
    ctx.beginPath();
    ctx.moveTo(0, c.height - scaleY * sensors[sensor]["readings"][0]);
    if (sensor == 0)
    ctx.strokeStyle = 'black';
    else if (sensor == 1)
    ctx.strokeStyle = 'red';
    else if (sensor == 2)
    ctx.strokeStyle = 'green';
    else
    ctx.strokeStyle = 'gray'; // TODO: support more curves?
    ctx.fillStyle = ctx.strokeStyle;
    
    ctx.lineWidth = '2';
    for (var i = 0; i < sensors[sensor]["readings"].length; i++) {
    ctx.lineTo(i * scaleX, c.height - scaleY * sensors[sensor]["readings"][i]);
    }
    
    ctx.fillRect(rightOfYValues, (sensor + 1) * 20, 10, 10)
    ctx.textBaseline = 'middle';
    ctx.fillText(sensors[sensor]["name"] + " (" + sensors[sensor]["readings"][sensors[sensor]["readings"].length - 1].toFixed(2) + ")",
                 rightOfYValues + 15, 5 + (sensor + 1) * 20); // TODO: get font height ?
    ctx.textBaseline = 'alphabetic';
    ctx.stroke();
  }
}

function myRefresh() {
  var xmlhttp = new XMLHttpRequest();
  var url = "sensors_" + globalRequestDuration + ".js";
  xmlhttp.onreadystatechange = function() {
    var h = document.getElementById("myLastValue");
    var d = document.getElementById("myLastUpdate");
    if (this.readyState == 4 && this.status == 200) {
      var myArr = JSON.parse(this.responseText);
      sensors = myArr["sensors"];

      if (sensors[0]["readings"].length != 0)
      {
        h.innerHTML = sensors[0]["readings"][sensors[0]["readings"].length - 1].toFixed(2);
        var today = new Date();
        d.innerHTML = "" + today.getFullYear() + "-" + 
                           String(today.getMonth() + 1).padStart(2, '0') + "-" +
                           String(today.getDate()).padStart(2, '0') + " " +
                           String(today.getHours()).padStart(2, '0') + ":" +
                           String(today.getMinutes()).padStart(2, '0') + ":" +
                           String(today.getSeconds()).padStart(2, '0')
      } else {
        h.innerHTML = "UNAVAILABLE";
      }
      myRedraw();
    }
    else if (this.status == 404)
    {
      sensors = [ { "id":"0000000000000000", "name":"No Data", "readings":[0.0]} ];
      h.innerHTML = "UNAVAILABLE";
      d.innerHTML = "UNAVAILABLE";
      myRedraw();
    }
  };
  xmlhttp.open("GET", url, true);
  xmlhttp.send();
}

function myOnLoad() {
  var c = document.getElementById("myCanvas");
  var ctx = c.getContext("2d");
  
  initialize();

  function initialize() {
    window.addEventListener('resize', resizeCanvas, false);
    resizeCanvas();
    myRefresh();
    setInterval(myRefresh, 10000);
  }

  // Runs each time the DOM window resize event fires.
  // Resets the canvas dimensions to match window,
  // then draws the new borders accordingly.
  function resizeCanvas() {
  var d = document.getElementById("canvasDiv");
    var c = document.getElementById("myCanvas");
    
    // Needed to let the canvasDiv div fill the remaining space without scrolling
    c.width = 0;
    c.height = 0;
    
    c.width = d.offsetWidth - 20;
    c.height = d.offsetHeight - 20;
    myRedraw();
  }
}
</script>

</body>

</html>
)rawliteral";
  server.send_P(200, "text/html", s);
}

String getSensorStart(int allSensorsIndex) {
  Sensor const & sensor = allSensors[allSensorsIndex];
  String s = "{\"id\":\"" + String(sensor.id) + "\", \"name\":\"" + String(sensor.name) + "\", \"readings\":[";
  return s;
}

void handleNotFound()
{
  if (server.uri().startsWith("/sensors/"))
  {
    String id = server.uri().substring(9);
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
            
            const size_t capacity = JSON_OBJECT_SIZE(2) + 100;
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
  
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
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

  Serial.print("Setting soft-AP configuration ... ");
  boolean softApConfigSuccess = WiFi.softAPConfig(local_IP, gateway, subnet);
  Serial.println( softApConfigSuccess ? "Ready" : "Failed!");

  Serial.print("Setting soft-AP ... ");
  boolean softApStartSuccess = WiFi.softAP(SSID, password);
  Serial.println( softApStartSuccess ? "Ready" : "Failed");

  server.on("/", handleRoot);
  server.on("/main.html", handleRoot);
  server.on("/settings.html", handleSettings);
  server.on("/sensors", handleSensors);
  server.on("/sensors/", handleSensors);
  server.on("/sensors_1h.js", handleSensors_1h);
  server.on("/sensors_24h.js", handleSensors_24h);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.print("Server listening on ");
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
