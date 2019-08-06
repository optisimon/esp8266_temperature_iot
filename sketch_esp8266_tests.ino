// Scan for other access points:
// https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/scan-class.html

// softAP and station mode simultaneously
// https://github.com/esp8266/Arduino/issues/119#issuecomment-157418957
// https://stackoverflow.com/questions/47345141/esp8266-wifi-ap-sta-mode

// TODO: Report values for multiple sensors? (might be limited by RAM)?
// TODO: Configure name of sensors (and store that permanently in the flash file system?
// TODO: Cache results for json object ?
// TODO: Support logging into an already available network either as an option, or when running as softAP)
#include <DallasTemperature.h>

#include <ESP8266WebServer.h>

#include <ESP8266WiFi.h>

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

CircularBuffer<float, 360> readings_1h;
CircularBuffer<float, 1440> readings_24h;

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
  String s = R"rawliteral(
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
</head>
<body onload="myOnLoad()">

<div>Last Value: <span id="myLastValue"></span>.
Last Update: <span id="myLastUpdate">No readings yet</span>.
Trend for
<select id="myDurationSelect" onchange="myDurationChanged()">
 <option value="1h" selected="selected">last hour</option>
 <option value="24h">last 24 hours</option>
</select>
</div>

<div id="canvasDiv">
<canvas id="myCanvas", width="80", height="300" style="border:1px solid #808080;">
Sorry, your browser does not support the canvas element!
</canvas>
</div>

<script>
var globalRequestDuration = "";
var readings = [0.0]; // TODO: rename because global, allow empty array?

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
  var scaleX = c.width * 1.0 / readings.length;
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
  ctx.font="100% sans-serif";
  for (var x = 0; x < divisionsX; x++)
  {
    ctx.moveTo(x * (c.width - 1) / divisionsX, 0);
    ctx.lineTo(x * (c.width - 1) / divisionsX, c.height);
  }
  for (var y = 0; y < divisionsY; y++)
  {
    ctx.moveTo(0, y * (c.height - 1) / divisionsY);
    ctx.lineTo(c.width-1, y * (c.height - 1) / divisionsY);
    ctx.strokeText(((divisionsY - y) * maxY / divisionsY).toFixed(1), 5, y * (c.height - 1) / divisionsY - 1);
  }
  ctx.stroke();

  // draw curve
  ctx.beginPath();
  ctx.moveTo(0, c.height - scaleY * readings[0]);
  ctx.strokeStyle = 'black';
  ctx.lineWidth = '2';
  for (var i = 0; i < readings.length; i++) {
    ctx.lineTo(i * scaleX, c.height - scaleY * readings[i]);
  }
  ctx.stroke();
}

function myRefresh() {
  var xmlhttp = new XMLHttpRequest();
  var url = "readings_" + globalRequestDuration + ".js";
  xmlhttp.onreadystatechange = function() {
    var h = document.getElementById("myLastValue");
    var d = document.getElementById("myLastUpdate");
    if (this.readyState == 4 && this.status == 200) {
      var myArr = JSON.parse(this.responseText);
      readings = myArr["readings"];

      if (readings.length != 0)
      {
        h.innerHTML = readings[readings.length - 1].toFixed(2);
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
      readings = [0.0]; // TODO: allow empty array?
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

    // Needed to let the div fill the remaining space without scrolling
    c.width = 0;
    c.height = 0;
    
    c.width = d.offsetWidth - 20; // TODO: https://stackoverflow.com/questions/1664785/resize-html5-canvas-to-fit-window
    c.height = d.offsetHeight - 20; // TODO: either full size, or determine absolute size somehow
    myRedraw();
  }
}
</script>

</body>

</html>
)rawliteral";
  server.send(200, "text/html", s);
}

void handleReadings_1h()
{
  String s = "{\"readings\":[";
  for (int i = 0; i < readings_1h.size(); i++)
  {
    if (i != 0) {
      s += ", ";
    }
    String val(readings_1h[i], 2);
    s += val;
  }
  s += "]}\n";
  server.send(200, "application/javascript", s);
}

void handleReadings_24h()
{
  String s = "{\"readings\":[";
  for (int i = 0; i < readings_24h.size(); i++)
  {
    if (i != 0) {
      s += ", ";
    }
    String val(readings_24h[i], 2);
    s += val;
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
  server.on("/readings_1h.js", handleReadings_1h);
  server.on("/readings_24h.js", handleReadings_24h);
  server.begin();
  Serial.print("Server listening on ");
  Serial.println(WiFi.softAPIP());

  readings_1h.fill(0.0f);
  readings_24h.fill(0.0f);

  Serial.print("Starting temperature sensor monitoring... ");
  sensors.begin(); // TODO: do we have a return status??
  Serial.print(sensors.getDeviceCount());
  Serial.println(" devices found:");

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
    for (int j = 0; j < 8; j++)
    {
      if (da[j] < 10)
      {
        Serial.print("0");
      }
      Serial.print(da[j], HEX);
    }
    Serial.println( i == sensorIndex ? " <-- will be used":"");
  }
}

void loop()
{
  Serial.println("loop()");

  sensors.requestTemperatures();
  float temperatureCelcius = sensors.getTempCByIndex(sensorIndex);
  Serial.println(temperatureCelcius);
      
  unsigned long startMillis_1h = millis();
  unsigned long startMillis_24h = startMillis_1h; // TODO: clean this up?

  readings_1h.push_back_erase_if_full(temperatureCelcius);
  readings_24h.push_back_erase_if_full(temperatureCelcius);

  bool shouldRead1h = false;
  bool shouldRead24h = false;

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
    
    sensors.requestTemperatures();
    float temperatureCelcius = sensors.getTempCByIndex(sensorIndex); //float temperatureCelcius = 0.1f * random(0, 500);
    Serial.println(temperatureCelcius);
    
    if (shouldRead1h) {
      readings_1h.push_back_erase_if_full(temperatureCelcius);
      startMillis_1h += time_between_1h_readings_ms;
      shouldRead1h = false;
    }

    if (shouldRead24h) {
      readings_24h.push_back_erase_if_full(temperatureCelcius);
      startMillis_24h += time_between_24h_readings_ms;
      shouldRead24h = false;
    }
      
  }
}
