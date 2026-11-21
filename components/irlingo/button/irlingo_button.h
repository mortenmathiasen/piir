// A button sending an irlingo remote's code for fixed feature values
#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/irlingo/irlingo_remote.h"

namespace esphome::irlingo {

class IrlingoButton : public button::Button, public Parented<IrlingoRemote> {
 public:
  void add_feature(const char *name, const char *value) { this->features_.emplace_back(name, value); }
  void set_send_times(uint32_t send_times) { this->send_times_ = send_times; }
  void set_send_wait(uint32_t send_wait) { this->send_wait_ = send_wait; }

 protected:
  void press_action() override { this->parent_->send(this->features_, this->send_times_, this->send_wait_); }

  FeatureValues features_;
  uint32_t send_times_{1};
  uint32_t send_wait_{0};
};

}  // namespace esphome::irlingo
