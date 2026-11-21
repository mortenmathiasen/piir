// A button sending an irlingo remote's code for fixed feature values
#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/irlingo/irlingo_remote.h"

namespace esphome::irlingo {

class IrlingoButton : public button::Button, public Parented<IrlingoRemote> {
 public:
  void add_feature(const char *name, const char *value) { this->features_.emplace_back(name, value); }

 protected:
  void press_action() override { this->parent_->send(this->features_); }

  FeatureValues features_;
};

}  // namespace esphome::irlingo
