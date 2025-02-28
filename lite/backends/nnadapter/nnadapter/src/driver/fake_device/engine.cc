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

#include "driver/fake_device/engine.h"
#include <unistd.h>
#include <algorithm>
#include <vector>
#include "driver/fake_device/converter/converter.h"
#include "driver/fake_device/converter/validator.h"
#include "optimizer/convert_quantization_symm_to_asymm.h"
#include "optimizer/fuse_matmul_add_into_fully_connected.h"
#include "utility/debug.h"
#include "utility/logging.h"
#include "utility/modeling.h"
#include "utility/string.h"
#include "utility/utility.h"

namespace nnadapter {
namespace fake_device {

Context::Context(void* device, const char* properties) : device_(device) {
  // TODO(hong19860320) create the raw context from fake_ddk
}

Context::~Context() {}

Program::~Program() { Clear(); }

void Program::Clear() {
  tensors_.clear();
  input_types_.clear();
  output_types_.clear();
  dump_graph_path_ = "";
  dump_graph_buffer_ = nullptr;
}

int Program::Validate(const core::Model* model, bool* supported_operations) {
  Validator validator(context_);
  return validator.Apply(model, supported_operations);
}

int Program::Build(core::Model* model, core::Cache* cache) {
  Clear();
  dump_graph_buffer_ = &cache->buffer;
  return cache->buffer.empty() ? BuildFromModel(model, cache)
                               : BuildFromCache(cache);
}

int Program::BuildFromModel(core::Model* model, core::Cache* cache) {
  // Convert the quantization parameters of the operands in the NNAdapter model
  NNADAPTER_VLOG(5) << "Origin model:" << std::endl << Visualize(model);
  FuseMatMulAddIntoFullyConnected(model);
  ConvertQuantizationSymmToAsymm(model);
  NNADAPTER_VLOG(5) << "Optimized model:" << std::endl << Visualize(model);
  // Convert a NNAdapter model to a fake_ddk graph
  graph_ = std::make_shared<fake_ddk::nn::Graph>();
  if (!graph_) {
    return NNADAPTER_OUT_OF_MEMORY;
  }
  Converter converter(graph_.get(), &tensors_);
  NNADAPTER_CHECK_EQ(converter.Apply(model), NNADAPTER_NO_ERROR);
  // Indentify the inputs and outputs
  auto input_count = model->input_operands.size();
  NNADAPTER_VLOG(3) << "Model input count: " << input_count;
  std::vector<std::shared_ptr<fake_ddk::nn::Tensor>> input_tensors;
  if (input_count > 0) {
    input_tensors.resize(input_count);
    input_types_.resize(input_count);
    for (size_t i = 0; i < input_count; i++) {
      auto operand = model->input_operands[i];
      const auto& type = operand->type;
      NNADAPTER_CHECK(tensors_.find(operand) != tensors_.end());
      input_tensors[i] = tensors_[operand].front();
      NNADAPTER_CHECK(input_tensors[i]);
      input_types_[i] = type;
    }
  }
  auto output_count = model->output_operands.size();
  NNADAPTER_VLOG(3) << "Model output count: " << output_count;
  NNADAPTER_CHECK_GT(output_count, 0);
  std::vector<std::shared_ptr<fake_ddk::nn::Tensor>> output_tensors(
      output_count);
  output_types_.resize(output_count);
  for (size_t i = 0; i < output_count; i++) {
    auto operand = model->output_operands[i];
    const auto& type = operand->type;
    NNADAPTER_CHECK(tensors_.find(operand) != tensors_.end());
    output_tensors[i] = tensors_[operand].back();
    NNADAPTER_CHECK(output_tensors[i]);
    output_types_[i] = type;
  }
  graph_->SetInputsOutputs(input_tensors, output_tensors);
  // Create an execution to build the graph to the device-related program.
  execution_ = std::make_shared<fake_ddk::nn::Exection>(graph_.get());
  if ((cache->token && cache->dir)) {
    if (graph_->EnableCache() == fake_ddk::nn::FAKE_DDK_SUCCESS) {
      std::vector<uint8_t>* model_buffer = nullptr;
      fake_ddk::nn::fakedevice_model_buffer* fm;
      model_buffer = &cache->buffer;
      execution_->Build(fm);
      NNADAPTER_VLOG(3) << "Dump the graph to buffer when the first run";
      model_buffer->resize(fm->length);
      memcpy(reinterpret_cast<void*>(model_buffer->data()),
             reinterpret_cast<void*>(fm->data),
             fm->length);
    } else {
      NNADAPTER_LOG(WARNING) << "DDK doesn't support model-cache";
      execution_->Build();
    }
  } else {
    execution_->Build();
  }
  NNADAPTER_VLOG(3) << "Build success.";
  return NNADAPTER_NO_ERROR;
}

int Program::BuildFromCache(core::Cache* cache) {
  graph_ = std::make_shared<fake_ddk::nn::Graph>();
  if (!graph_) {
    return NNADAPTER_OUT_OF_MEMORY;
  }
  // Load graph from cache buffer
  if (graph_->LoadCache(reinterpret_cast<char*>(cache->buffer.data()),
                        cache->buffer.size()) !=
      fake_ddk::nn::FAKE_DDK_SUCCESS) {
    NNADAPTER_LOG(FATAL) << "Failed to load cache graph from buffer!";
    return NNADAPTER_DEVICE_INTERNAL_ERROR;
  }
  NNADAPTER_VLOG(3) << "Load cache graph from buffer success.";
  // Indentify the inputs and outputs
  auto input_count = cache->input_types.size();
  NNADAPTER_VLOG(3) << "Model input count: " << input_count;
  std::vector<std::shared_ptr<fake_ddk::nn::Tensor>> input_tensors;
  if (input_count > 0) {
    input_tensors.resize(input_count);
    input_types_ = cache->input_types;
    for (size_t i = 0; i < input_count; i++) {
      const auto& type = cache->input_types[i];
      input_tensors[i] = CreateFakeDeviceTensor(
          graph_.get(), string_format("model_input_%d", i), &type);
      NNADAPTER_CHECK(input_tensors[i]);
    }
  }
  auto output_count = cache->output_types.size();
  NNADAPTER_VLOG(3) << "Model output count: " << output_count;
  NNADAPTER_CHECK_GT(output_count, 0);
  std::vector<std::shared_ptr<fake_ddk::nn::Tensor>> output_tensors(
      output_count);
  output_types_ = cache->output_types;
  for (size_t i = 0; i < output_count; i++) {
    const auto& type = cache->output_types[i];
    output_tensors[i] = CreateFakeDeviceTensor(
        graph_.get(), string_format("model_output_%d", i), &type);
    NNADAPTER_CHECK(output_tensors[i]);
  }
  graph_->SetInputsOutputs(input_tensors, output_tensors);
  // Create an execution to build the graph to the device-specific program.
  execution_ = std::make_shared<fake_ddk::nn::Exection>(graph_.get());
  execution_->Build();
  NNADAPTER_VLOG(3) << "Build success.";
  return NNADAPTER_NO_ERROR;
}

int Program::CheckInputsAndOutputs(uint32_t input_count,
                                   core::Argument* input_arguments,
                                   uint32_t output_count,
                                   core::Argument* output_arguments) {
  // Check inputs
  for (uint32_t i = 0; i < input_count; i++) {
    // Get the new dimensions
    auto& arg = input_arguments[i];
    NNAdapterOperandType new_type;
    arg.access(arg.memory, &new_type);
    // Check whether the count and data of dimensions have been changed
    const NNAdapterOperandType& old_type = input_types_[arg.index];
    bool matched = MatchDimensions(new_type.dimensions.data,
                                   new_type.dimensions.count,
                                   old_type.dimensions.data,
                                   old_type.dimensions.count);
    if (!matched) {
      return NNADAPTER_INVALID_DIMENSIONS;
    }
  }
  return NNADAPTER_NO_ERROR;
}

int Program::Execute(uint32_t input_count,
                     core::Argument* input_arguments,
                     uint32_t output_count,
                     core::Argument* output_arguments) {
  int ret = CheckInputsAndOutputs(
      input_count, input_arguments, output_count, output_arguments);
  if (ret != NNADAPTER_NO_ERROR) return ret;
  NNADAPTER_CHECK_EQ(input_types_.size(), input_count);
  NNADAPTER_CHECK_EQ(output_types_.size(), output_count);
  std::vector<fake_ddk::nn::InputInfo> input_info(input_count);
  std::vector<fake_ddk::nn::OutputInfo> output_info(output_count);
  for (uint32_t i = 0; i < input_count; i++) {
    auto& arg = input_arguments[i];
    NNADAPTER_CHECK_GE(arg.index, 0);
    NNADAPTER_CHECK_LT(arg.index, input_count);
    NNADAPTER_CHECK(arg.memory);
    NNADAPTER_CHECK(arg.access);
    auto type = input_types_[arg.index];
    auto buffer = arg.access(arg.memory, &type);
    NNADAPTER_CHECK(buffer);
    auto length = GetOperandTypeBufferLength(type);
    if (IsUInt8AsymmPerLayerQuantType(type.precision)) {
      Symm2AsymmData(reinterpret_cast<const int8_t*>(buffer),
                     length,
                     type.asymm_per_layer_params.zero_point,
                     reinterpret_cast<uint8_t*>(buffer));
    }
    // Initialize the input info for the execution
    input_info[arg.index].index = arg.index;
    input_info[arg.index].buf = buffer;
    input_info[arg.index].size = length;
    input_info[arg.index].type =
        static_cast<int>(ConvertToFakeDevicePrecisionType(type.precision));
    input_info[arg.index].layout =
        static_cast<int>(ConvertToFakeDeviceDataLayoutType(type.layout));
  }
  for (uint32_t i = 0; i < output_count; i++) {
    auto& arg = output_arguments[i];
    NNADAPTER_CHECK_GE(arg.index, 0);
    NNADAPTER_CHECK_LT(arg.index, output_count);
    NNADAPTER_CHECK(arg.memory);
    NNADAPTER_CHECK(arg.access);
    auto type = &output_types_[arg.index];
    // TODO(hong19860320) Get the dimensions of the outputs from fake_ddk
    // according to the dynamic dimensions of the inputs, fill them to 'type'
    // and call the 'access' function to re-allocate the host output memory
    auto buffer = arg.access(arg.memory, type);
    NNADAPTER_CHECK(buffer);
    auto length = GetOperandTypeBufferLength(*type);
    // Initialize the output info for the execution
    output_info[arg.index].index = arg.index;
    output_info[arg.index].buf = buffer;
    output_info[arg.index].size = length;
    output_info[arg.index].type =
        static_cast<int>(ConvertToFakeDevicePrecisionType(type->precision));
    output_info[arg.index].layout =
        static_cast<int>(ConvertToFakeDeviceDataLayoutType(type->layout));
  }
  auto start_time = GetCurrentUS();
  NNADAPTER_CHECK_EQ(execution_->SetInputs(input_info),
                     fake_ddk::nn::FAKE_DDK_SUCCESS);
  NNADAPTER_CHECK_EQ(execution_->Run(), fake_ddk::nn::FAKE_DDK_SUCCESS);
  NNADAPTER_CHECK_EQ(execution_->GetOutputs(output_info),
                     fake_ddk::nn::FAKE_DDK_SUCCESS);
  NNADAPTER_VLOG(3) << "Process cost " << GetCurrentUS() - start_time << " us";
  for (uint32_t i = 0; i < output_count; i++) {
    auto type = &output_types_[i];
    auto buffer = output_info[i].buf;
    auto length = output_info[i].size;
    if (IsUInt8AsymmPerLayerQuantType(type->precision)) {
      Asymm2SymmData(reinterpret_cast<const uint8_t*>(buffer),
                     length,
                     type->asymm_per_layer_params.zero_point,
                     reinterpret_cast<int8_t*>(buffer));
    }
  }
  // Read data from the dump graph file and fill to cache
  if (!dump_graph_path_.empty()) {
    if (ReadFile(dump_graph_path_, dump_graph_buffer_)) {
      NNADAPTER_LOG(INFO) << "Read the dump graph file " << dump_graph_path_
                          << " success.";
    } else {
      NNADAPTER_LOG(INFO) << "Failed to read the dump graph file "
                          << dump_graph_path_ << "!";
    }
    dump_graph_path_ = "";
  }
  return NNADAPTER_NO_ERROR;
}

}  // namespace fake_device
}  // namespace nnadapter
