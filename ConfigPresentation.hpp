#pragma once

struct ConfigPresentation
{
  enum class TemperatureUnit {
    Celsius = 'C',
    Fahrenheit = 'F',
    Kelvin = 'K'
  };
  ConfigPresentation() : 
    _modified(false),
    _presentationUnit(TemperatureUnit::Celsius),
    _ymax(90.0),
    _ymin(0.0),
    _yincrement(10.0)
  { /* no code */ }

  bool setPresentationUnit(char unit)
  {
    if (unit != getPresentationUnit())
    {
      if (unit == char(TemperatureUnit::Celsius) || 
          unit == char(TemperatureUnit::Fahrenheit) ||
          unit == char(TemperatureUnit::Kelvin))
      {
        _presentationUnit = TemperatureUnit(unit);
        _modified = true;
      }
      else
      {
        return false;
      }
    }
    return true;
  }
  char getPresentationUnit() const { return char(_presentationUnit); }

  float getYMax() const { return _ymax; }
  bool setYMax(float ymax)
  {
    if (ymax != _ymax)
    {
      _ymax = ymax;
      _modified = true;
    }
    return true;
  }

  float getYMin() const { return _ymin; }
  bool setYMin(float ymin)
  {
    if (ymin != _ymin)
    {
      _ymin = ymin;
      _modified = true;
    }
    return true;
  }

  float getYIncrement() const { return _yincrement; }
  bool setYIncrement(float yincrement)
  {
    if (yincrement != _yincrement)
    {
      _yincrement = yincrement;
      _modified = true;
    }
    return true;
  }

  bool isModified() const { return _modified; };

  /** Save values to flash */
  bool save()
  {
    String unit;
    unit += char(_presentationUnit);
    
    StaticJsonDocument<512> json;
    json["ymin"] = _ymin;
    json["ymax"] = _ymax;
    json["yincrement"] = _yincrement;
    json["unit"] = unit;

    char response[512] = {};
    size_t toWrite = serializeJson(json, response, sizeof(response));

    if (!toWrite || toWrite >= sizeof(response))
    {
      Serial.println("too much");
      return false; 
    }

    Serial.print("Writing: ");
    Serial.println(response);

    File configFile = SPIFFS.open("/config/presentation", "w");
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
    File configFile = SPIFFS.open("/config/presentation", "r");
    if (!configFile) {
      Serial.println("not found");
      return false;
    }
  
    size_t size = configFile.size();
    if (size > 1024) {
      Serial.println("too large");
      return false;
    }
  
    String buf = configFile.readString();
    configFile.close();
  
    StaticJsonDocument<512> json;
    DeserializationError error = deserializeJson(json, buf.c_str());
    if (error) {
      Serial.println("deserialize fail");
      return false;
    }
    {
      char const* keys[] = {"ymin", "ymax", "yincrement", "unit", nullptr};
      char const** it = keys;
      while (*it) {
        if (!json.containsKey(*it)) {
          Serial.printf("missing key %s", *it);
          return false;
        }
        it++;
      }
    }
    
    ConfigPresentation next;
    if (json["unit"].as<char*>() != '\0' && next.setPresentationUnit(json["unit"].as<char*>()[0]) &&
        next.setYMax(json["ymax"].as<float>()) &&
        next.setYMin(json["ymin"].as<float>()) &&
        next.setYIncrement(json["yincrement"].as<float>()))
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
    Serial.printf("patching presentation using '%s'\n", jsonString);
    StaticJsonDocument<512> root;
    DeserializationError error = deserializeJson(root, jsonString);
    if (error) {
      Serial.println("deserial err");
      return false;
    }
    
    char const* keys[] = {"ymin", "ymax", "yincrement", "unit", nullptr};
    for (JsonPair const & kv : root.as<JsonObject>())
    {
      char const** it = keys;
      bool currentKeyValid = false;
      while (*it) {
        bool keyMatch = strcmp(kv.key().c_str(), *it) == 0;
        bool isString = kv.value().is<char const*>() || kv.value().is<char*>();
        bool isInt = kv.value().is<int>();
        bool isFloat = kv.value().is<float>();

        if (kv.key() == "unit" && isString && (
          strcmp("C", kv.value().as<const char*>()) == 0 ||
          strcmp("F", kv.value().as<const char*>()) == 0 ||
          strcmp("K", kv.value().as<const char*>()) == 0))
        {
          currentKeyValid = true;
          break;
        }

        if (keyMatch && isInt || isFloat)
        {
          currentKeyValid = true;
          break;
        }
        it++;
      }
      if (!currentKeyValid) {
        Serial.printf("invalid key %s: ", kv.key().c_str());
        return false;
      }
    }

    ConfigPresentation next = *this;
    if (root.containsKey("ymin") && !next.setYMin(root["ymin"].as<float>())) { return false; }
    if (root.containsKey("ymax") && !next.setYMax(root["ymax"].as<float>())) { return false; }
    if (root.containsKey("yincrement") && !next.setYIncrement(root["yincrement"].as<float>())) { return false; }
    if (root.containsKey("unit") && !next.setPresentationUnit(root["unit"].as<char*>()[0])) { return false; }

    next._modified = _modified;
    if (next == *this)
    {
      return true;
    }
    *this = next;
    _modified = true;
    return true;
  }

  bool operator==(ConfigPresentation const& other) const {
    return other._presentationUnit == _presentationUnit &&
           other._ymax == _ymax &&
           other._ymin == _ymin &&
           other._yincrement == _yincrement;
  }

private:
  bool _modified;
  TemperatureUnit _presentationUnit;
  float _ymax;
  float _ymin;
  float _yincrement;
};
