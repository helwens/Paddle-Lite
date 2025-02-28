// Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "driver/fake_device/engine.h"

namespace nnadapter {
namespace fake_device {

class Validator {
 public:
  explicit Validator(Context* context) : context_(context) {}
  ~Validator() {}

  // Traverse each operation in the model and check if it is supported by the
  // device, and return a list of whether it is supported or not
  int Apply(const core::Model* model, bool* supported_operations);
  // Check each the operation is supported according to the device context and
  // custom settings
  Context* context() { return context_; }

 private:
  Context* context_{nullptr};
};

}  // namespace fake_device
}  // namespace nnadapter
