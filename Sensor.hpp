#pragma once

#include <DallasTemperature.h>

struct Sensor {
  int16_t index; ///< sensor index as determined by DallasTemperature class
  DeviceAddress deviceAddress;
  char id[17];
  char name[17];
  bool active;
  float lastValue; ///< not persisted
  Sensor() : index(0), deviceAddress{}, id{}, name{}, active(false), lastValue{}
  { /* no code */ }
};
