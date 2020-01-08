# ESP8266 Temperature plotting iot #

This is the software portion of the temperature plotting iot project.

Project homepage: https://optisimon.com/esp8266/arduino/temperature/monitoring/2020/01/07/esp8266-temperature-plotting-iot/

Software portion: https://github.com/optisimon/esp8266_temperature_iot

Hardware portion: https://github.com/optisimon/esp8266_temperature_iot_PCB/


This project makes it possible to monitor temperature readings from a number of sensors simultaneously.

The frontend is basically a small temperature plotting html page.

A static web page is served to anyone connecting. A bit of javascript periodically request a json object containing all values for the curves to plot, and a html canvas element is used for actually drawing the data.

The user can choose between a plot containing readings from the last 24 hours, or a plot for the last hour.

The web interface also allows renaming all sensors arbitrarily, selection of which sensors to store / plot, as well as allows changing network settings.

It can be accessed wirelessly over WiFi as it’s pretending to be an access point (with some limitations, such as not allowing more than four clients connected simultaneously). The software can optionally also connect to another WiFi network to make itself accessible there. (Make sure its pretended access point IP and the other network don’t have overlapping IP address ranges. That don’t seem to be supported at all by the ESP8266 network stack).

The software provides an external api which is well documented and straight forward to use.

The current hardware consist of a custom PCB with a MCP3208 ADC and a Wemos D1 mini pro ESP8266 module.
Up to 6 NTC sensors can be attached directly to the PCB.
Alternatively, digital one wire temperature sensors (DS18B20) could be attached.

The PCB layout (kicad design files as well as gerber files) are available at:
https://github.com/optisimon/esp8266_temperature_iot_PCB

The 6 channel limit for number of simultaneous temperature plots was arbitrarily set,
and can probably be increased easily (but then having all curves visible at the same time in the web GUI could be messy).


# Implementation details #

Nowadays using SPIFFS (a flash file system) for storing configuration files as well as web pages on the esp8266. This unfortunately makes the workflow slightly more complicated, but there are lots of tutorials explaining this such as:
https://github.com/esp8266/arduino-esp8266fs-plugin/tree/0.4.0
https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
https://github.com/pellepl/spiffs/wiki/FAQ


## Summary of settings in the arduino environment which seem relevant: ##

Arduino 1.8.9 settings:
LOLIN(WEMOS) D1 mini Pro
Flash Size: 16M (14M SPIFFS)

Installed libraries:
ArduinoJson 6.11.3
DallasTemperature 3.8.0
OneWire 2.3.4

Installed tools:
https://github.com/esp8266/arduino-esp8266fs-plugin/tree/0.4.0


### Summary of json API: ###

HTTP_GET returns status code 200 on success

HTTP_PATCH returns status code 200 on success (and the text OK)



| access  | url                    | notes |
|---------|------------------------|-------|
| GET     | /api/sensors           | All sensors detected at power on |
| GET     | /api/sensors/SENSOR_ID | detailed information for one sensor |
| PATCH   | /api/sensors/SENSOR_ID | update name or active status for sensor. NOT persisted to flash automatically |
| GET     | /api/readings/1h       | all readings for active sensors (last hour) |
| GET     | /api/readings/24h      | all readings for active sensors (last 24 hours) |
| GET     | /api/wifi/softap       | soft AP settings (SSID, password (will return stars), ip, netmask, gateway |
| PATCH   | /api/wifi/softap       | update settings above. Is persisted to flash automatically |
| GET     | /api/wifi/network      | SSID, password (will return stars), enable, etc for another WiFi to connect to |
| PATCH   | /api/wifi/network      | SSID, password, enable, etc for another WiFi to connect to. Is persisted to flash automatically |
| PATCH   | /api/persist           | persist sensor settings to flash. TODO: use for all settings or separate in sensors/ and wifi/ ? |


*NOT IMPLEMENTED:*

| access  | url                    | notes |
|---------|------------------------|-------|
| GET     | /api/wifi/scan         | list of detected networks (slow) |


<pre>
==== /api/sensors ====

{
  "sensors": [
    {
      "id": "28ff98fd6d14042e",
      "type": "OneWire",
      "name": "Sensor0",
      "active": 0,
      "lastValue": 24.06
    },
    {
      "id": "28ffc2fd6d140406",
      "type": "OneWire",
      "name": "upper",
      "active": 1,
      "lastValue": 25.88
    },
    {
      "id": "NTC-0",
      "type": "NTC",
      "name": "boiler_middle",
      "active": 1,
      "lastValue": 57.20
    }
  ],
  "max_num_active": 2
}


==== /api/sensors/28ff98fd6d14042e ====

where 28ff98fd6d14042e is the sensor ID of one sensor attached at power on

{
  "id": "28ff98fd6d14042e",
  "type": "OneWire",
  "name": "Sensor0",
  "active": 0,
  "lastValue": 24.06
}


==== /api/readings/1h ====

{
  "sensors": [
    {
      "id": "28ffbaa464140313",
      "type": "OneWire",
      "name": "middle",
      "readings": [0, 20.34, 20.50, ...]
    },
    {
      "id": "28ffc2fd6d140406",
      "type": "OneWire",
      "name": "upper",
      "readings": [0.00, 0.00, 0.00, ...]
    }
  ]
}


==== /api/readings/24h ====

{
  "sensors": [
    {
      "id": "28ffbaa464140313",
      "type": "OneWire",
      "name": "middle",
      "readings": [0, 20.34, 20.50, ...]
    },
    {
      "id": "28ffc2fd6d140406",
      "type": "OneWire",
      "name": "upper",
      "readings": [0.00, 0.00, 0.00, ...]
    }
  ]
}


==== /api/wifi/softap ====

NOTE: the password field will allways return "********" for security reasons
TODO: should channel be available (will change if running against a network as well, since they need to be on the same channel

{
  "ssid": "TestAP",
  "password": "********",
  "ip": "192.168.1.1",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0"
}


==== /api/wifi/scan ====

TODO: NOT DECIDED YET (since this is a slow operation?)


==== /api/wifi/network ====

NOTE: the password field will allways return "********" for security reasons
TODO: consider adding read back possibility of DHCP lease network parameters?

{
  "enabled": 1,
  "assignment": "dhcp|static",
  "ssid": "HouseNetwork",
  "password": "********",
  "static": {
    "ip": "192.168.1.1",
    "gateway": "192.168.1.1",
    "subnet": "255.255.255.0"
  }
}

==== /api/persist ====

"persist_now" always reads back 0. If set non-zero, sensor settings are stored to flash.
"unsaved_changes" currently always return 1 (TODO: implement?)

{
  "persist_now": 0,
  "unsaved_changes": 1
}



=== Flash file system ===

Have these files:
  "config/wifi/softap"
  "config/wifi/network"
  "config/sensors"


==== config/sensors (flash FS) ====

{
  "sensors": [
    {
      "id": "28ff98fd6d14042e",
      "type": "OneWire",
      "name": "Sensor0",
      "active": 0
    },
    {
      "id": "0000000000000001",
      "type": "NTC",
      "name": "Sensor1",
      "active": 0
    }
  ]
}

==== /config/wifi/network ====
{
  "enabled": 1,
  "assignment": "dhcp|static",
  "ssid": "HouseNetwork",
  "password": "********",
  "static": {
    "ip": "192.168.1.1",
    "gateway": "192.168.1.1",
    "subnet": "255.255.255.0"
  }
}
</pre>
