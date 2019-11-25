#pragma once

#include "Sensor.hpp"

struct ConfigSensors {
  enum { MAX_NUM_SENSORS = 10 };
  int16_t numAllSensors = 0;          // TODO: make this private and add accessors
  Sensor allSensors[MAX_NUM_SENSORS]; // TODO: make this private and create accessors

  bool patchSingleSensor(int sensorIndex, char const * jsonString) {
    // TODO: move in patching from web server code (which was accessing one sensor at a time)
    const size_t capacity = JSON_OBJECT_SIZE(2) + 200;
    StaticJsonDocument<capacity> root;
    DeserializationError error = deserializeJson(root, jsonString);
    
    if (error)
    {
      Serial.println("Parsing failed!");
      server.send(400, "text/plain", "ERROR"); // TODO: which status code???
      return false;
    }
    else 
    {
      if (root.containsKey("name"))
      {
        strncpy(allSensors[sensorIndex].name, root["name"].as<char*>(), sizeof(allSensors[sensorIndex].name));
        allSensors[sensorIndex].name[sizeof(allSensors[sensorIndex].name) - 1] = '\0';
      }
      if (root.containsKey("active"))
      {
        allSensors[sensorIndex].active = (root["active"].as<int>() == 0) ? false : true;
      }

      
      return true;
      // No idea what to do...
    }
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
    populateAllAdcChannels(); // Additionally, we reserve some for adc measurements of NTC resistors.

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
        char const* keys[] = {"id", "type", "name", "active", nullptr};
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
            allSensors[i].type = Sensor::Type::OneWire;
            allSensors[i].active = false;
            allSensors[i].lastValue = 0;
        }
    }

    void populateAllAdcChannels()
    {
      // Fake sensor names (i.e. reserve four for NTC because ADC have 4 channels)
      #define NUM_ADC_CHANNELS 4
      for (int i = 0; i < NUM_ADC_CHANNELS; i++)
      {
        if (numAllSensors >= ConfigSensors::MAX_NUM_SENSORS)
        {
          Serial.println("ERROR: not enough room for additional analog sensors");
          break;
        }
        Sensor & s = allSensors[numAllSensors];
        s = {};
        s.type = Sensor::Type::NTC;
        s.index = i;
        s.active = true;
        snprintf(s.id, sizeof(s.id), "000000000000000%d", i);
        snprintf(s.name, sizeof(s.name), "NTC-%d", i);
        s.lastValue = 0;
        numAllSensors++;
      }
    }

    int getNumActive() const
    {
      int cnt = 0;
      for (int i = 0; i < numAllSensors; i++)
      {
        if (allSensors[i].active)
        {
          cnt++;
        }
      }
      return cnt;
    }

    bool isModified() const { return _modified; }
    private:
    bool _modified;
    String sensorToString(int allSensorIndex)
    {
        String id;
        if (allSensors[allSensorIndex].type == Sensor::Type::OneWire)
        {
            id = deviceAddressToString(allSensors[allSensorIndex].deviceAddress);
        }
        else if (allSensors[allSensorIndex].type == Sensor::Type::NTC)
        {
            id = String("000000000000000") + String(allSensors[allSensorIndex].index);
        }
        String s = "{\"id\":\"" + id +
        "\", \"type\":\"" + toString(allSensors[allSensorIndex].type) +
        "\", \"name\":\"" + allSensors[allSensorIndex].name +
        "\", \"active\":" + (allSensors[allSensorIndex].active ? "1" : "0") + "}";
        return s;
    }
} configSensors;
