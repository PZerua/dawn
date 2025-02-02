// Copyright 2019 The Dawn Authors
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

#ifndef SRC_DAWN_NATIVE_VULKAN_ADAPTERVK_H_
#define SRC_DAWN_NATIVE_VULKAN_ADAPTERVK_H_

#include "dawn/native/PhysicalDevice.h"

#include "dawn/common/RefCounted.h"
#include "dawn/common/vulkan_platform.h"
#include "dawn/native/vulkan/VulkanInfo.h"

#include "dawn/native/VulkanBackend.h"

namespace dawn::native::vulkan {

class VulkanInstance;

class PhysicalDevice : public PhysicalDeviceBase {
  public:
    PhysicalDevice(InstanceBase* instance,
                   VulkanInstance* vulkanInstance,
                   VkPhysicalDevice physicalDevice,
                   const OpenXRConfig& config);
    ~PhysicalDevice() override;

    // PhysicalDeviceBase Implementation
    bool SupportsExternalImages() const override;
    bool SupportsFeatureLevel(FeatureLevel featureLevel) const override;

    const VulkanDeviceInfo& GetDeviceInfo() const;
    VkPhysicalDevice GetVkPhysicalDevice() const;
    VulkanInstance* GetVulkanInstance() const;
    const OpenXRConfig& GetOpenXRConfig() const;

    bool IsDepthStencilFormatSupported(VkFormat format) const;

    bool IsAndroidQualcomm() const;
    bool IsIntelMesa() const;

  private:
    MaybeError InitializeImpl() override;
    void InitializeSupportedFeaturesImpl() override;
    MaybeError InitializeSupportedLimitsImpl(CombinedLimits* limits) override;

    MaybeError ValidateFeatureSupportedWithTogglesImpl(wgpu::FeatureName feature,
                                                       const TogglesState& toggles) const override;

    void SetupBackendDeviceToggles(TogglesState* deviceToggles) const override;
    ResultOrError<Ref<DeviceBase>> CreateDeviceImpl(AdapterBase* adapter,
                                                    const DeviceDescriptor* descriptor,
                                                    const TogglesState& deviceToggles) override;

    VkPhysicalDevice mVkPhysicalDevice;
    Ref<VulkanInstance> mVulkanInstance;
    VulkanDeviceInfo mDeviceInfo = {};
    OpenXRConfig mOpenXRConfig;
};

}  // namespace dawn::native::vulkan

#endif  // SRC_DAWN_NATIVE_VULKAN_ADAPTERVK_H_
