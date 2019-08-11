struct ConfigNetwork
{
  enum class Assignment { STATIC = 0, DHCP = 1 };
  ConfigNetwork() : 
    _enabled(false),
    _assignment(Assignment::DHCP),
    _staticIp(192,168,1,1),
    _staticGateway(192,168,1,1),
    _staticSubnet(255,255,255,0),
    _ssid{"HouseNetwork"},
    _password{"testtest"},
    _modified(false)
  { /* no code */ }

  bool getEnabled() const { return _enabled; }
  bool setEnabled(bool enabled) { _modified = true; _enabled = enabled; return true;}

  Assignment getAssignment() const { return _assignment; };
  bool setAssignment(Assignment assignment) {
    _modified = true;
    _assignment = assignment;
    return true;
  }
  bool setAssignment(const char* str) {
    if (strcmp(str, "static") == 0) {
      _assignment = Assignment::STATIC;
    } else if (strcmp(str, "dhcp") == 0) {
      _assignment = Assignment::DHCP;
    } else {
      return false;
    }
    _modified = true;
    return true;
  }

  IPAddress const & getStaticIp() const { return _staticIp; }
  bool setStaticIp(String const & newIP) { return setIpIfValid(newIP, _staticIp); }

  IPAddress const & getStaticGateway() const { return _staticGateway; }
  bool setStaticGateway(String const & newIP) { return setIpIfValid(newIP, _staticGateway); }

  IPAddress const & getStaticSubnet() const { return _staticSubnet; }
  bool setStaticSubnet(String const & newIP) { return setIpIfValid(newIP, _staticSubnet); }

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
    String ip = _staticIp.toString();
    String gateway = _staticGateway.toString();
    String subnet = _staticSubnet.toString();
    
    StaticJsonDocument<512> json;
    json["enabled"] = _enabled;
    json["assignment"] = (_assignment == Assignment::DHCP) ? "dhcp":"static";
    json["ssid"] = _ssid;
    json["password"] = _password;
    JsonObject staticStuff = json.createNestedObject("static");
    staticStuff["ip"] = ip.c_str();
    staticStuff["gateway"] = gateway.c_str();
    staticStuff["subnet"] = subnet.c_str();

    char response[512] = {};
    size_t toWrite = serializeJson(json, response, sizeof(response));

    if (!toWrite || toWrite >= sizeof(response))
    {
      Serial.println("too much");
      return false; 
    }

    Serial.print("Writing: ");
    Serial.println(response);

    File configFile = SPIFFS.open("/config/wifi/network", "w");
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
    File configFile = SPIFFS.open("/config/wifi/network", "r");
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
    {
      char const* keys[] = {"enabled", "assignment", "ssid", "password", "static", nullptr};
      char const** it = keys;
      while (*it) {
        if (!json.containsKey(*it)) {
          Serial.printf("missing key %s", *it);
          return false;
        }
        it++;
      }
    }

    JsonObject staticStuff = json["static"];
    {
      char const* keys[] = {"ip", "gateway", "subnet", nullptr};
      char const** it = keys;
      while (*it) {
        if (!staticStuff.containsKey(*it)) {
          Serial.printf("missing key 'static.%s'\n", *it);
          return false;
        }
        it++;
      }
    }
    
    ConfigNetwork next;
    if (next.setEnabled(json["enabled"].as<int>()) &&
        next.setAssignment(json["assignment"].as<const char*>()) &&
        next.setSsid(json["ssid"]) &&
        next.setPassword(json["password"]) &&
        next.setStaticIp(staticStuff["ip"]) &&
        next.setStaticGateway(staticStuff["gateway"]) &&
        next.setStaticSubnet(staticStuff["subnet"]))
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
    Serial.printf("patching network using '%s'\n", jsonString);
    StaticJsonDocument<512> root;
    DeserializationError error = deserializeJson(root, jsonString);
    if (error) {
      Serial.println("deserial err");
      return false;
    }
    
    char const* keys[] = {"enabled", "assignment", "ssid", "password", "static", nullptr};
    for (JsonPair const & kv : root.as<JsonObject>())
    {
      char const** it = keys;
      bool currentKeyValid = false;
      while (*it) {
        bool keyMatch = strcmp(kv.key().c_str(), *it) == 0;
        bool isString = kv.value().is<char const*>() || kv.value().is<char*>();
        bool isInt = kv.value().is<int>();
        bool isObject = kv.value().is<JsonObject>();

        // exceptions
        bool isEnableKeyValid = (strcmp(kv.key().c_str(), "enabled") == 0) && isInt;
        if ((strcmp(kv.key().c_str(), "static") == 0) && isObject) {
          char const* keys[] = {"ip", "gateway", "subnet", nullptr};
          JsonObject staticStuff = root["static"];
          for (JsonPair const & kv : staticStuff)
          {
            char const** it = keys;
            bool currentStaticKeyValid = false;
            while (*it) {
              bool keyMatch = strcmp(kv.key().c_str(), *it) == 0;
              bool isString = kv.value().is<char const*>() || kv.value().is<char*>();
              if (keyMatch && isString)
              {
                currentStaticKeyValid = true;
                break;
              }
              it++;
            }
            if (!currentStaticKeyValid) {
              Serial.printf("invalid key static.%s: ", kv.key().c_str());
              return false;
            }
          }
          currentKeyValid = true;
          break;
        }
        
        if (isEnableKeyValid || (keyMatch && isString))
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

    ConfigNetwork next = *this;
    if (root.containsKey("enabled") && !next.setEnabled(root["enabled"].as<int>())) { return false; }
    if (root.containsKey("assignment") && !next.setAssignment(root["assignment"].as<char*>())) { return false; }
    if (root.containsKey("ssid") && !next.setSsid(root["ssid"].as<char*>())) { return false; }
    if (root.containsKey("password") && !next.setPassword(root["password"].as<char*>())) { return false; }
    if (root.containsKey("static")) {
      JsonObject const staticStuff = root["static"].as<JsonObject>();
      if (staticStuff.containsKey("ip") && !next.setStaticIp(staticStuff["ip"].as<char*>())) { return false; }
      if (staticStuff.containsKey("gateway") && !next.setStaticGateway(staticStuff["gateway"].as<char*>())) { return false; }
      if (staticStuff.containsKey("subnet") && !next.setStaticSubnet(staticStuff["subnet"].as<char*>())) { return false; }
    }

    next._modified = _modified;
    if (next == *this)
    {
      return true;
    }
    *this = next;
    _modified = true;
    return true;
  }

  bool operator==(ConfigNetwork const& other) const {
    return other._enabled == _enabled &&
           other._assignment == _assignment &&
           other._staticIp == _staticIp &&
           other._staticGateway == _staticGateway &&
           other._staticSubnet == _staticSubnet &&
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

  bool _enabled;
  Assignment _assignment;

  IPAddress _staticIp;
  IPAddress _staticGateway; ///< Quite useless, since we don't route traffic to another net
  IPAddress _staticSubnet;

  /** WPA2 standard allows a maximum of 32 chars (although some routers only allow 31 char) */
  char _ssid[33];

  /** Passwords are by the WPA2 standard limited to be at least 8 characters long,
      and are not allowed to be longer than 63 characters. Leave empty for an open network. */
  char _password[64];

  bool _modified;
};