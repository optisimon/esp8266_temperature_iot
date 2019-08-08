
When running in softAP mode, the unit will by default  present itself on
192.168.1.1.

=== Summary of json objects available: ===

GET returns status code 200 on success
PATCH returns status code 200 on success (and the text OK)



|| access || uri               || notes ||
 | GET     | /api/sensors           | All sensors detected at power on |
 | GET     | /api/sensors/SENSOR_ID | detailed information for one sensor |
 | PATCH   | /api/sensors/SENSOR_ID | update name or active status for sensor |
 | GET     | /api/readings/1h       | all readings for active sensors (last hour) |
 | GET     | /api/readings/24h      | all readings for active sensors (last 24 hours) |


NOT AVAILABLE YET:
 | GET     | /api/wifi/softap       | soft AP settings (SSID, password (will return stars), ip, netmask, gateway |
 | PATCH   | /api/wifi/softap       | update settings above |

 | GET     | /api/wifi/scan         | list of detected networks (slow) |
 | GET     | /api/wifi/station      | SSID, password (will return stars), enable, etc for another WiFi to connect to |
 | PATCH   | /api/wifi/station      | SSID, password, enable, etc for another WiFi to connect to |

 | PATCH   | /api/persist           | store settings to flash |

==== /api/sensors ====

{
  "sensors": [
    {
      "id": "28ff98fd6d14042e",
      "name": "Sensor0",
      "active": 0,
      "lastValue": 24.06
    },
    {
      "id": "28ffc2fd6d140406",
      "name": "upper",
      "active": 1,
      "lastValue": 25.88
    }
  ]
}


==== /api/sensors/28ff98fd6d14042e ====

where 28ff98fd6d14042e is the sensor ID of one sensor attached at power on

{
  "id": "28ff98fd6d14042e",
  "name": "Sensor0",
  "active": 0,
  "lastValue": 24.06
}


==== /api/readings/1h ====

{
  "sensors": [
    {
      "id": "28ffbaa464140313",
      "name": "middle",
      "readings": [0, 20.34, 20.50, ...]
    },
    {
      "id": "28ffc2fd6d140406",
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
      "name": "middle",
      "readings": [0, 20.34, 20.50, ...]
    },
    {
      "id": "28ffc2fd6d140406",
      "name": "upper",
      "readings": [0.00, 0.00, 0.00, ...]
    }
  ]
}


==== /api/wifi/softap ====

TODO: should channel be available (will change if running network as well, since they need to be on the same channel

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

{
  "enabled": 1,
  "assignment": "dhcp|static",
  "ssid": "HouseNetwork",
  "password": "********",
  "ip": "192.168.1.1",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  
  "assigned_ip": "192.168.1.1",
  "assigned_gateway": "192.168.1.1",
  "assigned_subnet": "255.255.255.0",
}

==== /api/persist ====

"persist_now" always reads back 0. If set non-zero, settings are stored to flash
"unsaved_changes" indicates whether there are any unsaved changes

{
  "persist_now": 0,
  "unsaved_changes": 1
}



=== Flash file system ===

Have these files:
  "settings/wifi/softap"
  "settings/wifi/network"
  "settings/sensors"


==== settings/sensors (flash FS) ====

{
  "sensors": [
    {
      "id": "28ff98fd6d14042e",
      "name": "Sensor0",
      "active": 0
    }
  ]
}
