#pragma once

#include "Sensor.hpp"

struct ConfigSensors {
  enum { MAX_NUM_SENSORS = 10 };
  int16_t numAllSensors = 0;          // TODO: make this private and add accessors
  Sensor allSensors[MAX_NUM_SENSORS]; // TODO: make this private and create accessors

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
    populateAllOneWireSensors();

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

    void populateAllOneWireSensors()
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
