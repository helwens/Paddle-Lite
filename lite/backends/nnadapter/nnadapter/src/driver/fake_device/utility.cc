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

#include "driver/fake_device/utility.h"
#include "utility/debug.h"
#include "utility/logging.h"

namespace nnadapter {
namespace fake_device {

fake_ddk::nn::PrecisionType ConvertToFakeDevicePrecisionType(
    NNAdapterOperandPrecisionCode input_precision) {
  fake_ddk::nn::PrecisionType output_precision =
      fake_ddk::nn::PrecisionType::UNKNOWN;
  switch (input_precision) {
    case NNADAPTER_BOOL8:
      output_precision = fake_ddk::nn::PrecisionType::BOOL8;
      break;
    case NNADAPTER_INT8:
    case NNADAPTER_QUANT_INT8_SYMM_PER_LAYER:
    case NNADAPTER_QUANT_INT8_SYMM_PER_CHANNEL:
      output_precision = fake_ddk::nn::PrecisionType::INT8;
      break;
    case NNADAPTER_INT16:
      output_precision = fake_ddk::nn::PrecisionType::INT16;
      break;
    case NNADAPTER_INT32:
    case NNADAPTER_QUANT_INT32_SYMM_PER_LAYER:
    case NNADAPTER_QUANT_INT32_SYMM_PER_CHANNEL:
      output_precision = fake_ddk::nn::PrecisionType::INT32;
      break;
    case NNADAPTER_INT64:
      output_precision = fake_ddk::nn::PrecisionType::INT64;
      break;
    case NNADAPTER_UINT8:
    case NNADAPTER_QUANT_UINT8_ASYMM_PER_LAYER:
      output_precision = fake_ddk::nn::PrecisionType::UINT8;
      break;
    case NNADAPTER_UINT16:
      output_precision = fake_ddk::nn::PrecisionType::UINT16;
      break;
    case NNADAPTER_QUANT_UINT32_ASYMM_PER_LAYER:
    case NNADAPTER_UINT32:
      output_precision = fake_ddk::nn::PrecisionType::UINT32;
      break;
    case NNADAPTER_UINT64:
      output_precision = fake_ddk::nn::PrecisionType::UINT64;
      break;
    case NNADAPTER_FLOAT16:
      output_precision = fake_ddk::nn::PrecisionType::FLOAT16;
      break;
    case NNADAPTER_FLOAT32:
      output_precision = fake_ddk::nn::PrecisionType::FLOAT32;
      break;
    case NNADAPTER_FLOAT64:
      output_precision = fake_ddk::nn::PrecisionType::FLOAT64;
      break;
    default:
      NNADAPTER_LOG(FATAL)
          << "Failed to convert the NNAdapter operand precision code("
          << OperandPrecisionCodeToString(input_precision)
          << ") to fake_ddk::nn::PrecisionType !";
      break;
  }
  return output_precision;
}

fake_ddk::nn::DataLayoutType ConvertToFakeDeviceDataLayoutType(
    NNAdapterOperandLayoutCode input_layout) {
  fake_ddk::nn::DataLayoutType output_layout =
      fake_ddk::nn::DataLayoutType::UNKNOWN;
  switch (input_layout) {
    case NNADAPTER_NCHW:
      output_layout = fake_ddk::nn::DataLayoutType::NCHW;
      break;
    case NNADAPTER_NHWC:
      output_layout = fake_ddk::nn::DataLayoutType::NHWC;
      break;
    default:
      NNADAPTER_LOG(FATAL)
          << "Failed to convert the NNAdapter operand layout code("
          << OperandLayoutCodeToString(input_layout)
          << ") to fake_ddk::nn::DataLayoutType !";
      break;
  }
  return output_layout;
}

std::vector<uint32_t> ConvertToFakeDeviceDimensions(
    int32_t* input_dimensions, uint32_t input_dimensions_count) {
  std::vector<uint32_t> output_dimensions(input_dimensions_count);
  memcpy(&output_dimensions[0],
         input_dimensions,
         input_dimensions_count * sizeof(uint32_t));
  return output_dimensions;
}

std::shared_ptr<fake_ddk::nn::Tensor> CreateFakeDeviceTensor(
    fake_ddk::nn::Graph* graph,
    const std::string& name,
    int32_t* dimensions_data,
    uint32_t dimensions_count,
    fake_ddk::nn::PrecisionType precision,
    const float* quant_scale,
    const int32_t* zero_point,
    void* buffer,
    fake_ddk::nn::DataLayoutType layout) {
  auto attr = std::make_shared<fake_ddk::nn::TensorAttr>();
  attr->name = name;
  attr->dims = ConvertToFakeDeviceDimensions(dimensions_data, dimensions_count);
  attr->precision = precision;
  attr->layout = layout;
  if (quant_scale) {
    // Quantization types
    attr->qntParamAffineAsymmetric.scale.resize(1);
    attr->qntParamAffineAsymmetric.scale[0] = *quant_scale;
    if (zero_point) {
      attr->qntParamAffineAsymmetric.zero_point.resize(1);
      attr->qntParamAffineAsymmetric.zero_point[0] = *zero_point;
    }
  } else {
    // TODO(hong19860320) Supports the normal types, such as float etc.
    NNADAPTER_LOG(FATAL) << "Only quantizaion types are supported.";
  }
  auto tensor = graph->CreateTensor(attr, buffer);
  NNADAPTER_CHECK(tensor);
  return tensor;
}

std::shared_ptr<fake_ddk::nn::Tensor> CreateFakeDeviceTensor(
    fake_ddk::nn::Graph* graph,
    const std::string& name,
    const NNAdapterOperandType* type,
    void* buffer,
    std::vector<int32_t> dimensions) {
  if (dimensions.empty()) {
    for (uint32_t i = 0; i < type->dimensions.count; i++) {
      dimensions.push_back(type->dimensions.data[i]);
    }
  }
  auto precision = ConvertToFakeDevicePrecisionType(type->precision);
  auto layout = ConvertToFakeDeviceDataLayoutType(type->layout);
  const float* quant_scale = nullptr;
  const int32_t* zero_point = nullptr;
  switch (type->precision) {
    case NNADAPTER_QUANT_UINT8_ASYMM_PER_LAYER:
      quant_scale = &type->asymm_per_layer_params.scale;
      zero_point = &type->asymm_per_layer_params.zero_point;
      break;
    case NNADAPTER_QUANT_INT32_SYMM_PER_LAYER:
      quant_scale = &type->symm_per_layer_params.scale;
      break;
    default:
      NNADAPTER_LOG(FATAL)
          << "Can not add a fake_ddk::nn::Tensor with precision="
          << OperandPrecisionCodeToString(type->precision) << " !";
      break;
  }
  return CreateFakeDeviceTensor(graph,
                                name,
                                dimensions.data(),
                                dimensions.size(),
                                precision,
                                quant_scale,
                                zero_point,
                                buffer,
                                layout);
}

}  // namespace fake_device
}  // namespace nnadapter
