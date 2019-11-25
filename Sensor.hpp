#pragma once

#include <DallasTemperature.h>

struct Sensor {
  enum class Type {
    Undefined,
    OneWire,
    NTC
  };

  Type type;
  
  /** 
   * sensor index as determined by
   * - DallasTemperature class (if type = OneWire)
   * - Analog NTC channel (if type = NTC)
   */
  int16_t index;
  DeviceAddress deviceAddress;
  char id[17];
  char name[17];
  bool active;
  float lastValue; ///< not persisted
  Sensor() : type{}, index(0), deviceAddress{}, id{}, name{}, active(false), lastValue{}
  { /* no code */ }
};

String toString(Sensor::Type t) {
  switch (t)
  {
    case Sensor::Type::NTC:
    return "NTC";
    case Sensor::Type::OneWire:
    return "OneWire";
    default:
    return "Unknown";
  }
}
