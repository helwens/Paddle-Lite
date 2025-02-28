// Copyright (c) 2020 PaddlePaddle Authors. All Rights Reserved.
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

#include <stdint.h>
#include <vector>

namespace fake_ddk {
namespace nn {

/* error code */
enum {
  FAKE_DDK_SUCCESS = 0,
  FAKE_DDK_FAILURE = -1,
  FAKE_DDK_INVALID_INPUTS = -2,
  FAKE_DDK_INVALID_OUTPUTS = -3,
  FAKE_DDK_INVALID_MODEL = -4,
  FAKE_DDK_INVALID_PARAM = -5,
  FAKE_DDK_NO_MEMORY = -6,
  FAKE_DDK_DEVICE_UNAVAILABLE = -7,
  FAKE_DDK_INVALID_TENSOR = -8,
  FAKE_DDK_INVALID_OP = -9,
};

/* The precision of Tensor */
enum class PrecisionType : int {
  UNKNOWN = 0,
  INT8 = 1,
  INT16,
  INT32,
  INT64,
  UINT8 = 5,
  UINT16,
  UINT32,
  UINT64,
  FLOAT16 = 9,
  FLOAT32,
  FLOAT64,
  BOOL8 = 12,
};

/* The data layout of Tensor */
enum class DataLayoutType : int {
  UNKNOWN = 0,
  NCHW = 1,
  NHWC = 2,
  ANY = 3, /* Any data layout */
  NUM = 4, /* Number of fields */
};

/* Pad type enum */
enum class PadType : int {
  AUTO = 0, /* Decide by driver */
            /*
             * VALID Padding: it means no padding and it assumes that all the dimensions
             * are valid
             * So that the input image gets fully covered by a filter and the stride
             * specified by you.
            */
  VALID,
  /*
   * SAME Padding: it applies padding to the input image so that the input image
   * gets fully covered by the filter and specified stride
   * It is called SAME because,
   * for stride 1, the output will be the same as the input.
  */
  SAME
};
}  // namespace nn
}  // namespace fake_ddk
