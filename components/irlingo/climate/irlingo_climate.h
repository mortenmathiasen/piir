// An air conditioner remote as a climate entity: every change sends one code
// with the features of the mode, the target temperature and the fan speed
#pragma once

#include <utility>
#include <vector>

#include "esphome/components/climate/climate.h"
#include "esphome/components/irlingo/irlingo_remote.h"
#include "esphome/core/component.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

namespace esphome::irlingo {

class IrlingoClimate : public climate::Climate, public Component, public Parented<IrlingoRemote> {
 public:
  void add_mode_feature(climate::ClimateMode mode, const char *name, const char *value);
  void set_temperature_feature(const char *name) { this->temperature_feature_ = name; }
  void add_temperature(float temperature, const char *value) { this->temperatures_.emplace_back(temperature, value); }
  void set_fan_feature(const char *name) { this->fan_feature_ = name; }
  void add_fan_value(const char *value);
  void add_fixed(const char *name, const char *value) { this->fixed_.emplace_back(name, value); }
#ifdef USE_SENSOR
  void set_sensor(sensor::Sensor *sensor) { this->sensor_ = sensor; }
#endif

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
  void transmit_();
  const char *nearest_temperature_();

  std::vector<std::pair<climate::ClimateMode, FeatureValues>> modes_;
  const char *temperature_feature_{nullptr};
  std::vector<std::pair<float, const char *>> temperatures_;  // sorted
  const char *fan_feature_{nullptr};
  const char *fan_auto_{nullptr};
  std::vector<const char *> fan_custom_;
  FeatureValues fixed_;
#ifdef USE_SENSOR
  sensor::Sensor *sensor_{nullptr};
#endif
};

}  // namespace esphome::irlingo
