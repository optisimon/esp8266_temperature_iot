#include <DallasTemperature.h>

#include <ESP8266WebServer.h>

#include <ESP8266WiFi.h>

const unsigned long time_between_readings_ms = 10000; // 1000 ms seemed stable
const int oneWireBus = 4; // D2 is the same as gpio4 on my board...
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);


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

CircularBuffer<float, 360> readings;

void handleRoot()
{
  // created string by running
  // cat main.html | sed -e 's/\"/\\"/g' -e 's/$/\\n\"/' -e 's/^/\"/'
  
  //String s = "<h1>Something is working! /sg</h1><p>Readings: ";
  String s = "<html>\n"
"\n"
"<body onload=\"myOnLoad()\">\n"
"<H1 id=\"myHeader\">Last value: </H1>\n"
"<canvas id=\"myCanvas\", width=\"80\", height=\"300\" style=\"border:1px solid #808080;\">\n"
"Sorry, your browser does not support the canvas element!\n"
"</canvas>\n"
"\n"
"<script src=\"readings.js\"></script>\n"
"\n"
"<script>\n"
"function myOnLoad() {\n"
"  var h = document.getElementById(\"myHeader\");\n" // TODO: handle length 0 nicely?
"  h.innerHTML = \"Last value: \" + readings[readings.length - 1].toFixed(2);\n"
"  var c = document.getElementById(\"myCanvas\");\n"
"  var ctx = c.getContext(\"2d\");\n"
"\n"
"  initialize();\n"
"\n"
"  function initialize() {\n"
"    window.addEventListener('resize', resizeCanvas, false);\n"
"    resizeCanvas();\n"
"    setInterval(refresh, 10000);\n"
"  }\n"
"\n"
"  function redraw() {\n"
"    ctx.strokeStyle = 'blue';\n"
"    ctx.lineWidth = '5';\n"
"    ctx.strokeRect(0, 0, c.width, c.height);\n"
"\n"
"    ctx.strokeStyle = 'black';\n"
"    ctx.lineWidth = '2';\n"
"    var scaleX = c.width * 1.0 / readings.length;\n"
"    var scaleY = c.height * 1.0 / 50;\n"
"    ctx.moveTo(0, c.height - scaleY * readings[0]);\n"
"    for (var i = 0; i < readings.length; i++) {\n"
"      ctx.lineTo(i * scaleX, c.height - scaleY * readings[i]);\n"
"      ctx.stroke();\n"
"    }\n"
"  }\n"
"\n"
"  function refresh() {\n"
"   \n"
"    // TODO: fetch new fetch new json object and redraw() without reload\n"
"    //       https://www.w3schools.com/js/js_json_http.asp\n"
"    location.reload(true);\n"
"  }\n"
"\n"
"  // Runs each time the DOM window resize event fires.\n"
"  // Resets the canvas dimensions to match window,\n"
"  // then draws the new borders accordingly.\n"
"  function resizeCanvas() {\n"
"    c.width = window.innerWidth - 50; // TODO: https://stackoverflow.com/questions/1664785/resize-html5-canvas-to-fit-window\n"
"    c.height = window.innerHeight - 100; // TODO: either full size, or determine absolute size somehow\n"
"    redraw();\n"
"  }\n"
"\n"
"}\n"
"</script>\n"
"\n"
"</body>\n"
"\n"
"</html>\n";
  server.send(200, "text/html", s);
}

void handleReadings()
{
  String s = "readings = [";
  for (int i = 0; i < readings.size(); i++)
  {
    if (i != 0) {
      s += ", ";
    }
    String val(readings[i], 2);
    s += val;
  }
  s += "]\n";
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
  server.on("/readings.js", handleReadings);
  server.begin();
  Serial.print("Server listening on ");
  Serial.println(WiFi.softAPIP());

  readings.fill(0.0f);
/*  
  readings[0] = 0;
  readings[1] = 10;
  readings[2] = 30;
  readings[3] = 20;
  readings[4] = 0;
  readings[5] = 15;
*/  

  Serial.print("Starting temperature sensor monitoring... ");
  sensors.begin(); // TODO: do we have a return status??
  Serial.print(sensors.getDeviceCount());
  Serial.println(" devices found");
}

void loop()
{
  unsigned long startMillis = millis();

  while (true)
  {
    //Serial.printf("Stations connected = %d\n", WiFi.softAPgetStationNum());
    //delay(3000);
  
    sensors.requestTemperatures();
    float temperatureCelcius = sensors.getTempCByIndex(0);
    //float temperatureCelcius = 0.1f * random(0, 500);
    readings.push_back_erase_if_full(temperatureCelcius);
    Serial.println(temperatureCelcius);
  
    // While busy waiting for next reading - handle web requests
    do
    {
      server.handleClient();
      delay(20); // TODO: is this needed??
      //            Working combination is 500ms / reading + 10ms here. (ap most often there)
      //            Non-working combination is 500ms / reading + 1ms here (ap disapperas)
      //            Working rock stable: 1000ms / 20ms
    }
    while (millis() - startMillis < time_between_readings_ms);

    startMillis += time_between_readings_ms;
  }
}
