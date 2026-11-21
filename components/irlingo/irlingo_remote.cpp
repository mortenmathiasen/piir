#include "irlingo_remote.h"

#include <cstdio>
#include <cstring>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

extern "C" {
#include "checksum.h"
#include "config.h"
#include "log.h"
#include "plugins.h"
#include "pulses.h"
}

namespace esphome::irlingo {

static const char *const TAG = "irlingo";

// irlingo's own log messages go to ESPHome's log
static void log_to_esphome(log_Event *ev) {
  char message[200];
  vsnprintf(message, sizeof(message), ev->fmt, ev->ap);
  if (ev->level >= LOG_ERROR) {
    ESP_LOGE(TAG, "%s", message);
  } else if (ev->level == LOG_WARN) {
    ESP_LOGW(TAG, "%s", message);
  } else if (ev->level == LOG_INFO) {
    ESP_LOGD(TAG, "%s", message);
  } else {
    ESP_LOGV(TAG, "%s", message);
  }
}

void IrlingoRemote::setup() {
  static bool log_installed = false;
  if (!log_installed) {
    log_set_quiet(true);
    log_add_callback(log_to_esphome, nullptr, LOG_TRACE);
    log_installed = true;
  }

  // Remotes with a toggle bit send a different code on every other press;
  // remember which one is next across restarts
  loadConfigString(this->json_);
  if (!isConfigLoaded()) {
    ESP_LOGE(TAG, "Remote %s cannot be loaded", this->name_);
    this->mark_failed();
    return;
  }
  this->has_toggle_ = hasToggle();
  unloadConfig();
  if (this->has_toggle_) {
    this->toggle_pref_ = global_preferences->make_preference<bool>(fnv1_hash(std::string("irlingo_") + this->name_));
    this->toggle_pref_.load(&this->toggled_);
  }
}

void IrlingoRemote::dump_config() {
  ESP_LOGCONFIG(TAG, "irlingo remote %s:", this->name_);
  ESP_LOGCONFIG(TAG, "  Size: %u bytes", (unsigned) strlen(this->json_));
  if (this->has_toggle_) {
    ESP_LOGCONFIG(TAG, "  Toggle bit: YES, next press sends the %s code", this->toggled_ ? "toggled" : "normal");
  } else {
    ESP_LOGCONFIG(TAG, "  Toggle bit: NO");
  }
}

bool IrlingoRemote::send(const FeatureValues &features, uint32_t send_times, uint32_t send_wait) {
  loadConfigString(this->json_);
  if (!isConfigLoaded()) {
    ESP_LOGE(TAG, "Remote %s cannot be loaded", this->name_);
    return false;
  }

  // The requested values, at the index of their feature in the remote
  size_t count = getAllFeaturesCount();
  std::vector<featureT> requested(count ? count : 1, featureT{nullptr, nullptr});
  for (const auto &feature : features) {
    size_t index;
    if (!hasFeatureNameAndValue(feature.first.c_str(), feature.second.c_str()) ||
        !getFeatureIndex(feature.first.c_str(), &index)) {
      ESP_LOGE(TAG, "Remote %s has no feature %s=%s", this->name_, feature.first.c_str(), feature.second.c_str());
      unloadConfig();
      return false;
    }
    requested[index] = featureT{feature.first.c_str(), feature.second.c_str()};
  }

  // The same steps as the irlingo command: features (with overrides and
  // variants), checksums, plugin, then pulses
  char *code = nullptr;
  loadToggledFeaturedCode(requested.data(), this->toggled_ ? 1 : 0, &code);
  const char *plugin = nullptr;
  loadPlugin(&plugin);
  bool ok = true;
  if (applyConfigChecksums(code)) {
    ESP_LOGE(TAG, "Remote %s has an invalid checksum", this->name_);
    ok = false;
  } else if (applyRemotePlugin(plugin, code)) {
    ESP_LOGE(TAG, "Remote %s needs the unknown plugin %s", this->name_, plugin);
    ok = false;
  }

  std::vector<unsigned int> pulses;
  size_t pulse_count = 0;
  int frequency = 38000;
  if (ok) {
    size_t symbol_count = getSymboldefinitionsCount();
    std::vector<symbolDefinition> symbols(symbol_count);
    loadConfigSymbols(symbols.data());
    loadFrequency(&frequency);
    pulses.resize(MAX_PULSE_VALUES);
    ok = irlingoBuildPulses(symbol_count, symbols.data(), code, pulses.data(), &pulse_count) == 0;
  }
  ESP_LOGD(TAG, "%s: %s", this->name_, code ? code : "");
  free(code);
  unloadConfig();
  if (!ok)
    return false;

  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(frequency);
  data->reserve(pulse_count);
  for (size_t i = 0; i < pulse_count; i++) {
    if (i % 2 == 0) {
      data->mark(pulses[i]);
    } else {
      data->space(pulses[i]);
    }
  }
  call.set_send_times(send_times);
  call.set_send_wait(send_wait);
  call.perform();
  ESP_LOGI(TAG, "Sent %s (%u pulses and spaces at %d Hz, %u times)", this->name_, (unsigned) pulse_count, frequency,
           (unsigned) send_times);

  if (this->has_toggle_) {
    this->toggled_ = !this->toggled_;
    this->toggle_pref_.save(&this->toggled_);
  }
  return true;
}

}  // namespace esphome::irlingo
