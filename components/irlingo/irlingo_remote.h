// ESPHome glue: an irlingo remote sent through a remote_transmitter. The
// remote is encoded by irlingo's portable C code in this directory.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::irlingo {

using FeatureValues = std::vector<std::pair<std::string, std::string>>;

class IrlingoRemote : public Component {
 public:
  void set_transmitter(remote_transmitter::RemoteTransmitterComponent *transmitter) {
    this->transmitter_ = transmitter;
  }
  void set_remote(const char *json) { this->json_ = json; }
  void set_name(const char *name) { this->name_ = name; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  /// Send the code for these feature values, as `irlingo --remote ... --feature name=value`
  /// does. Features not given keep the remote's template. Returns false on errors (logged).
  bool send(const FeatureValues &features);

 protected:
  remote_transmitter::RemoteTransmitterComponent *transmitter_{nullptr};
  const char *json_{nullptr};
  const char *name_{""};
  bool has_toggle_{false};
  bool toggled_{false};
  ESPPreferenceObject toggle_pref_;
};

template<typename... Ts> class SendAction : public Action<Ts...>, public Parented<IrlingoRemote> {
 public:
  template<typename V> void add_feature(const char *name, V value) {
    this->features_.emplace_back(name, TemplatableValue<std::string, Ts...>(value));
  }

  void play(const Ts &...x) override {
    FeatureValues values;
    values.reserve(this->features_.size());
    for (auto &feature : this->features_)
      values.emplace_back(feature.first, feature.second.value(x...));
    this->parent_->send(values);
  }

 protected:
  std::vector<std::pair<std::string, TemplatableValue<std::string, Ts...>>> features_;
};

}  // namespace esphome::irlingo
