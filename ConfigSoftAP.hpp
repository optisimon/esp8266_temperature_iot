#pragma once

struct ConfigSoftAP
{
  ConfigSoftAP() : 
    _ip(192,168,0,1),
    _gateway(192,168,0,1),
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
      return true;
    }
    *this = next;
    _modified = true;
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
};
