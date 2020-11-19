// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <gtest/gtest.h>
#include <core/framework/kernel_registry.h>
#include "core/providers/cpu/cpu_execution_provider.h"
#include <test/onnx/test_filters.h>

namespace onnxruntime {
namespace test {
// Test whether all onnx ops have kernel registrations for CPU EP. This is
// important because ORT promises compliance with onnx opsets.
TEST(ONNXOpKernelRegistryTests, kernels_registered_for_all_onnx_ops) {
  auto cpu_execution_provider = onnxruntime::make_unique<CPUExecutionProvider>(CPUExecutionProviderInfo(false));
  const auto registry = cpu_execution_provider->GetKernelRegistry();
  const auto& kernel_creator_fn_map = registry->GetKernelCreateMap();

  auto last_released_onnx_version = ONNX_NAMESPACE::OpSchemaRegistry::DomainToVersionRange().LastReleaseVersionMap().at(ONNX_NAMESPACE::ONNX_DOMAIN);
  const std::vector<ONNX_NAMESPACE::OpSchema> schemas = ONNX_NAMESPACE::OpSchemaRegistry::get_all_schemas();

  bool missing_kernel_registrations_for_op = false;
  std::string err_string = "Kernel registrations missing for following operators ";
  auto total_schemas = schemas.size();
  for (const auto& schema : schemas) {
    const std::string& op_type = schema.Name();
    const std::string& domain = schema.domain().empty() ? kOnnxDomainAlias : schema.domain();
    int op_since_version = schema.SinceVersion();

    // Only check for onnx domain, valid (non deprecated and not experimental) ops.
    if (domain != kOnnxDomainAlias || schema.Deprecated() ||
        schema.support_level() == ONNX_NAMESPACE::OpSchema::SupportType::EXPERIMENTAL) {
      total_schemas--;
      continue;
    }

    std::string key(op_type);
    key.append(1, ' ').append(domain).append(1, ' ').append(kCpuExecutionProvider);
    auto range = kernel_creator_fn_map.equal_range(key);
    bool valid_version = false;
    // walk through all registered versions for this op
    // untill we find the one which matches the version.
    for (auto i = range.first; i != range.second; ++i) {
      auto kernel_def = *i->second.kernel_def;
      int kernel_start_version;
      int kernel_end_version;
      kernel_def.SinceVersion(&kernel_start_version, &kernel_end_version);

      valid_version = (kernel_start_version == op_since_version) || (kernel_start_version < op_since_version &&
                                                                   kernel_end_version != INT_MAX &&
                                                                   kernel_end_version >= op_since_version);
      if (valid_version) {
        break;
      }
    }
    if (!valid_version) {
      if (!schema.HasFunction() && !schema.HasContextDependentFunction() &&
          schema.SinceVersion() <= last_released_onnx_version) {
        if (std::find(expected_not_registered_ops.begin(), expected_not_registered_ops.end(), op_type) == expected_not_registered_ops.end()) {
          // some unit tests register schema for testing and those op show up here as CPU EP does not
          // kernels for this schema. These test schemas have empty domain which is resolved to onnx therefore
          // using the location of the schema definition to distinguish between legit onnx schemas vs test schemas
          if (schema.file().find("test") != std::string::npos) {
            break;
          }
          missing_kernel_registrations_for_op = true;
          err_string.append(op_type).append(" version ").append(std::to_string(op_since_version).append(1, ' '));
        }
      }
    }
  }

  EXPECT_FALSE(missing_kernel_registrations_for_op) << err_string;
}

}  // namespace test
}  // namespace onnxruntime