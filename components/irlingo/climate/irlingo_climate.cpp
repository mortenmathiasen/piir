#include "irlingo_climate.h"

#include <cmath>
#include <strings.h>

#include "esphome/core/log.h"

namespace esphome::irlingo {

static const char *const TAG = "irlingo.climate";

void IrlingoClimate::add_mode_feature(climate::ClimateMode mode, const char *name, const char *value) {
  for (auto &entry : this->modes_) {
    if (entry.first == mode) {
      entry.second.emplace_back(name, value);
      return;
    }
  }
  this->modes_.push_back({mode, FeatureValues{{name, value}}});
}

void IrlingoClimate::add_fan_value(const char *value) {
  if (strcasecmp(value, "AUTO") == 0) {
    this->fan_auto_ = value;
  } else {
    this->fan_custom_.push_back(value);
  }
}

void IrlingoClimate::setup() {
  if (!this->fan_custom_.empty())
    this->set_supported_custom_fan_modes(this->fan_custom_);

#ifdef USE_SENSOR
  if (this->sensor_ != nullptr) {
    this->sensor_->add_on_state_callback([this](float state) {
      this->current_temperature = state;
      this->publish_state();
    });
    this->current_temperature = this->sensor_->state;
  }
#endif

  // Restore the last state without sending it: a restart must not switch
  // the air conditioner
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->target_temperature = 21;
    if (this->fan_auto_ != nullptr) {
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
    } else if (!this->fan_custom_.empty()) {
      this->set_custom_fan_mode_(this->fan_custom_.front());
    }
  }
  if (!this->temperatures_.empty())
    this->target_temperature = std::atof(this->nearest_temperature_());
  this->publish_state();
}

void IrlingoClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "irlingo climate:");
  ESP_LOGCONFIG(TAG, "  Modes: %u", (unsigned) this->modes_.size());
  if (this->temperature_feature_ != nullptr && !this->temperatures_.empty()) {
    ESP_LOGCONFIG(TAG, "  Temperature: %s, %.1f to %.1f", this->temperature_feature_, this->temperatures_.front().first,
                  this->temperatures_.back().first);
  }
  if (this->fan_feature_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Fan: %s, %u values", this->fan_feature_,
                  (unsigned) (this->fan_custom_.size() + (this->fan_auto_ != nullptr)));
}

climate::ClimateTraits IrlingoClimate::traits() {
  auto traits = climate::ClimateTraits();
  for (auto &entry : this->modes_)
    traits.add_supported_mode(entry.first);
  if (!this->temperatures_.empty()) {
    traits.set_visual_min_temperature(this->temperatures_.front().first);
    traits.set_visual_max_temperature(this->temperatures_.back().first);
    float step = 1.0f;
    for (auto &entry : this->temperatures_) {
      if (entry.first != std::floor(entry.first))
        step = 0.5f;
    }
    traits.set_visual_temperature_step(step);
  }
  if (this->fan_auto_ != nullptr)
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
#ifdef USE_SENSOR
  if (this->sensor_ != nullptr)
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
#endif
  return traits;
}

void IrlingoClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value())
    this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value())
    this->set_fan_mode_(*call.get_fan_mode());
  if (!call.get_custom_fan_mode().empty())
    this->set_custom_fan_mode_(call.get_custom_fan_mode());
  this->transmit_();
  this->publish_state();
}

// The remote's temperature value nearest to the target
const char *IrlingoClimate::nearest_temperature_() {
  const char *best = this->temperatures_.front().second;
  float best_distance = INFINITY;
  for (auto &entry : this->temperatures_) {
    float distance = std::fabs(entry.first - this->target_temperature);
    if (distance < best_distance) {
      best_distance = distance;
      best = entry.second;
    }
  }
  return best;
}

void IrlingoClimate::transmit_() {
  FeatureValues values = this->fixed_;
  for (auto &entry : this->modes_) {
    if (entry.first == this->mode)
      values.insert(values.end(), entry.second.begin(), entry.second.end());
  }
  if (this->temperature_feature_ != nullptr && !this->temperatures_.empty()) {
    const char *value = this->nearest_temperature_();
    this->target_temperature = std::atof(value);
    values.emplace_back(this->temperature_feature_, value);
  }
  if (this->fan_feature_ != nullptr) {
    if (this->fan_mode.has_value() && *this->fan_mode == climate::CLIMATE_FAN_AUTO && this->fan_auto_ != nullptr) {
      values.emplace_back(this->fan_feature_, this->fan_auto_);
    } else if (!this->get_custom_fan_mode().empty()) {
      values.emplace_back(this->fan_feature_, this->get_custom_fan_mode().c_str());
    }
  }
  this->parent_->send(values);
}

}  // namespace esphome::irlingo
