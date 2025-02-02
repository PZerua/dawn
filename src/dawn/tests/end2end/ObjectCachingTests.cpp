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

#include <vector>

#include "dawn/tests/DawnTest.h"

#include "dawn/utils/ComboRenderPipelineDescriptor.h"
#include "dawn/utils/WGPUHelpers.h"

class ObjectCachingTest : public DawnTest {};

// Test that BindGroupLayouts are correctly deduplicated.
TEST_P(ObjectCachingTest, BindGroupLayoutDeduplication) {
    wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform}});
    wgpu::BindGroupLayout sameBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform}});
    wgpu::BindGroupLayout otherBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::Uniform}});

    EXPECT_NE(bgl.Get(), otherBgl.Get());
    EXPECT_EQ(bgl.Get() == sameBgl.Get(), !UsesWire());
}

// Test that two similar bind group layouts won't refer to the same one if they differ by dynamic.
TEST_P(ObjectCachingTest, BindGroupLayoutDynamic) {
    wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true}});
    wgpu::BindGroupLayout sameBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true}});
    wgpu::BindGroupLayout otherBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, false}});

    EXPECT_NE(bgl.Get(), otherBgl.Get());
    EXPECT_EQ(bgl.Get() == sameBgl.Get(), !UsesWire());
}

// Test that two similar bind group layouts won't refer to the same one if they differ by
// textureComponentType
TEST_P(ObjectCachingTest, BindGroupLayoutTextureComponentType) {
    wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float}});
    wgpu::BindGroupLayout sameBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float}});
    wgpu::BindGroupLayout otherBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Uint}});

    EXPECT_NE(bgl.Get(), otherBgl.Get());
    EXPECT_EQ(bgl.Get() == sameBgl.Get(), !UsesWire());
}

// Test that two similar bind group layouts won't refer to the same one if they differ by
// viewDimension
TEST_P(ObjectCachingTest, BindGroupLayoutViewDimension) {
    wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float}});
    wgpu::BindGroupLayout sameBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float}});
    wgpu::BindGroupLayout otherBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float,
                  wgpu::TextureViewDimension::e2DArray}});

    EXPECT_NE(bgl.Get(), otherBgl.Get());
    EXPECT_EQ(bgl.Get() == sameBgl.Get(), !UsesWire());
}

// Test that an error object doesn't try to uncache itself
TEST_P(ObjectCachingTest, ErrorObjectDoesntUncache) {
    DAWN_TEST_UNSUPPORTED_IF(HasToggleEnabled("skip_validation"));

    ASSERT_DEVICE_ERROR(
        wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
            device, {{0, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform},
                     {0, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform}}));
}

// Test that PipelineLayouts are correctly deduplicated.
TEST_P(ObjectCachingTest, PipelineLayoutDeduplication) {
    wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform}});
    wgpu::BindGroupLayout otherBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::Uniform}});

    wgpu::PipelineLayout pl = utils::MakeBasicPipelineLayout(device, &bgl);
    wgpu::PipelineLayout samePl = utils::MakeBasicPipelineLayout(device, &bgl);
    wgpu::PipelineLayout otherPl1 = utils::MakeBasicPipelineLayout(device, nullptr);
    wgpu::PipelineLayout otherPl2 = utils::MakeBasicPipelineLayout(device, &otherBgl);

    EXPECT_NE(pl.Get(), otherPl1.Get());
    EXPECT_NE(pl.Get(), otherPl2.Get());
    EXPECT_EQ(pl.Get() == samePl.Get(), !UsesWire());
}

// Test that ShaderModules are correctly deduplicated.
TEST_P(ObjectCachingTest, ShaderModuleDeduplication) {
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        @fragment fn main() -> @location(0) vec4f {
            return vec4f(0.0, 1.0, 0.0, 1.0);
        })");
    wgpu::ShaderModule sameModule = utils::CreateShaderModule(device, R"(
        @fragment fn main() -> @location(0) vec4f {
            return vec4f(0.0, 1.0, 0.0, 1.0);
        })");
    wgpu::ShaderModule otherModule = utils::CreateShaderModule(device, R"(
        @fragment fn main() -> @location(0) vec4f {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        })");

    EXPECT_NE(module.Get(), otherModule.Get());
    EXPECT_EQ(module.Get() == sameModule.Get(), !UsesWire());
}

// Test that ComputePipeline are correctly deduplicated wrt. their ShaderModule
TEST_P(ObjectCachingTest, ComputePipelineDeduplicationOnShaderModule) {
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        var<workgroup> i : u32;
        @compute @workgroup_size(1) fn main() {
            i = 0u;
        })");
    wgpu::ShaderModule sameModule = utils::CreateShaderModule(device, R"(
        var<workgroup> i : u32;
        @compute @workgroup_size(1) fn main() {
            i = 0u;
        })");
    wgpu::ShaderModule otherModule = utils::CreateShaderModule(device, R"(
        @compute @workgroup_size(1) fn main() {
        })");

    EXPECT_NE(module.Get(), otherModule.Get());
    EXPECT_EQ(module.Get() == sameModule.Get(), !UsesWire());

    wgpu::PipelineLayout layout = utils::MakeBasicPipelineLayout(device, nullptr);

    wgpu::ComputePipelineDescriptor desc;
    desc.compute.entryPoint = "main";
    desc.layout = layout;

    desc.compute.module = module;
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&desc);

    desc.compute.module = sameModule;
    wgpu::ComputePipeline samePipeline = device.CreateComputePipeline(&desc);

    desc.compute.module = otherModule;
    wgpu::ComputePipeline otherPipeline = device.CreateComputePipeline(&desc);

    EXPECT_NE(pipeline.Get(), otherPipeline.Get());
    EXPECT_EQ(pipeline.Get() == samePipeline.Get(), !UsesWire());
}

// Test that ComputePipeline are correctly deduplicated wrt. their constants override values
TEST_P(ObjectCachingTest, ComputePipelineDeduplicationOnOverrides) {
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        override x: u32 = 1u;
        var<workgroup> i : u32;
        @compute @workgroup_size(x) fn main() {
            i = 0u;
        })");

    wgpu::PipelineLayout layout = utils::MakeBasicPipelineLayout(device, nullptr);

    wgpu::ComputePipelineDescriptor desc;
    desc.compute.entryPoint = "main";
    desc.layout = layout;
    desc.compute.module = module;

    std::vector<wgpu::ConstantEntry> constants{{nullptr, "x", 16}};
    desc.compute.constantCount = constants.size();
    desc.compute.constants = constants.data();
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&desc);

    std::vector<wgpu::ConstantEntry> sameConstants{{nullptr, "x", 16}};
    desc.compute.constantCount = sameConstants.size();
    desc.compute.constants = sameConstants.data();
    wgpu::ComputePipeline samePipeline = device.CreateComputePipeline(&desc);

    desc.compute.constantCount = 0;
    desc.compute.constants = nullptr;
    wgpu::ComputePipeline otherPipeline1 = device.CreateComputePipeline(&desc);

    std::vector<wgpu::ConstantEntry> otherConstants{{nullptr, "x", 4}};
    desc.compute.constantCount = otherConstants.size();
    desc.compute.constants = otherConstants.data();
    wgpu::ComputePipeline otherPipeline2 = device.CreateComputePipeline(&desc);

    EXPECT_NE(pipeline.Get(), otherPipeline1.Get());
    EXPECT_NE(pipeline.Get(), otherPipeline2.Get());
    EXPECT_EQ(pipeline.Get() == samePipeline.Get(), !UsesWire());
}

// Test that ComputePipeline are correctly deduplicated wrt. their layout
TEST_P(ObjectCachingTest, ComputePipelineDeduplicationOnLayout) {
    wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform}});
    wgpu::BindGroupLayout otherBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::Uniform}});

    wgpu::PipelineLayout pl = utils::MakeBasicPipelineLayout(device, &bgl);
    wgpu::PipelineLayout samePl = utils::MakeBasicPipelineLayout(device, &bgl);
    wgpu::PipelineLayout otherPl = utils::MakeBasicPipelineLayout(device, nullptr);

    EXPECT_NE(pl.Get(), otherPl.Get());
    EXPECT_EQ(pl.Get() == samePl.Get(), !UsesWire());

    wgpu::ComputePipelineDescriptor desc;
    desc.compute.entryPoint = "main";
    desc.compute.module = utils::CreateShaderModule(device, R"(
            var<workgroup> i : u32;
            @compute @workgroup_size(1) fn main() {
                i = 0u;
            })");

    desc.layout = pl;
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&desc);

    desc.layout = samePl;
    wgpu::ComputePipeline samePipeline = device.CreateComputePipeline(&desc);

    desc.layout = otherPl;
    wgpu::ComputePipeline otherPipeline = device.CreateComputePipeline(&desc);

    EXPECT_NE(pipeline.Get(), otherPipeline.Get());
    EXPECT_EQ(pipeline.Get() == samePipeline.Get(), !UsesWire());
}

// Test that RenderPipelines are correctly deduplicated wrt. their layout
TEST_P(ObjectCachingTest, RenderPipelineDeduplicationOnLayout) {
    wgpu::BindGroupLayout bgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform}});
    wgpu::BindGroupLayout otherBgl = utils::MakeBindGroupLayout(
        device, {{1, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::Uniform}});

    wgpu::PipelineLayout pl = utils::MakeBasicPipelineLayout(device, &bgl);
    wgpu::PipelineLayout samePl = utils::MakeBasicPipelineLayout(device, &bgl);
    wgpu::PipelineLayout otherPl = utils::MakeBasicPipelineLayout(device, nullptr);

    EXPECT_NE(pl.Get(), otherPl.Get());
    EXPECT_EQ(pl.Get() == samePl.Get(), !UsesWire());

    utils::ComboRenderPipelineDescriptor desc;
    desc.cTargets[0].writeMask = wgpu::ColorWriteMask::None;
    desc.vertex.module = utils::CreateShaderModule(device, R"(
        @vertex fn main() -> @builtin(position) vec4f {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        })");
    desc.cFragment.module = utils::CreateShaderModule(device, R"(
        @fragment fn main() {
        })");

    desc.layout = pl;
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);

    desc.layout = samePl;
    wgpu::RenderPipeline samePipeline = device.CreateRenderPipeline(&desc);

    desc.layout = otherPl;
    wgpu::RenderPipeline otherPipeline = device.CreateRenderPipeline(&desc);

    EXPECT_NE(pipeline.Get(), otherPipeline.Get());
    EXPECT_EQ(pipeline.Get() == samePipeline.Get(), !UsesWire());
}

// Test that RenderPipelines are correctly deduplicated wrt. their vertex module
TEST_P(ObjectCachingTest, RenderPipelineDeduplicationOnVertexModule) {
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        @vertex fn main() -> @builtin(position) vec4f {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        })");
    wgpu::ShaderModule sameModule = utils::CreateShaderModule(device, R"(
        @vertex fn main() -> @builtin(position) vec4f {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        })");
    wgpu::ShaderModule otherModule = utils::CreateShaderModule(device, R"(
        @vertex fn main() -> @builtin(position) vec4f {
            return vec4f(1.0, 1.0, 1.0, 1.0);
        })");

    EXPECT_NE(module.Get(), otherModule.Get());
    EXPECT_EQ(module.Get() == sameModule.Get(), !UsesWire());

    utils::ComboRenderPipelineDescriptor desc;
    desc.cTargets[0].writeMask = wgpu::ColorWriteMask::None;
    desc.cFragment.module = utils::CreateShaderModule(device, R"(
            @fragment fn main() {
            })");

    desc.vertex.module = module;
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);

    desc.vertex.module = sameModule;
    wgpu::RenderPipeline samePipeline = device.CreateRenderPipeline(&desc);

    desc.vertex.module = otherModule;
    wgpu::RenderPipeline otherPipeline = device.CreateRenderPipeline(&desc);

    EXPECT_NE(pipeline.Get(), otherPipeline.Get());
    EXPECT_EQ(pipeline.Get() == samePipeline.Get(), !UsesWire());
}

// Test that RenderPipelines are correctly deduplicated wrt. their fragment module
TEST_P(ObjectCachingTest, RenderPipelineDeduplicationOnFragmentModule) {
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        @fragment fn main() {
        })");
    wgpu::ShaderModule sameModule = utils::CreateShaderModule(device, R"(
        @fragment fn main() {
        })");
    wgpu::ShaderModule otherModule = utils::CreateShaderModule(device, R"(
        @fragment fn main() -> @location(0) vec4f {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        })");

    EXPECT_NE(module.Get(), otherModule.Get());
    EXPECT_EQ(module.Get() == sameModule.Get(), !UsesWire());

    utils::ComboRenderPipelineDescriptor desc;
    desc.vertex.module = utils::CreateShaderModule(device, R"(
        @vertex fn main() -> @builtin(position) vec4f {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        })");

    desc.cFragment.module = module;
    desc.cTargets[0].writeMask = wgpu::ColorWriteMask::None;
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);

    desc.cFragment.module = sameModule;
    wgpu::RenderPipeline samePipeline = device.CreateRenderPipeline(&desc);

    desc.cFragment.module = otherModule;
    wgpu::RenderPipeline otherPipeline = device.CreateRenderPipeline(&desc);

    EXPECT_NE(pipeline.Get(), otherPipeline.Get());
    EXPECT_EQ(pipeline.Get() == samePipeline.Get(), !UsesWire());
}

// Test that Renderpipelines are correctly deduplicated wrt. their constants override values
TEST_P(ObjectCachingTest, RenderPipelineDeduplicationOnOverrides) {
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        override a: f32 = 1.0;
        @vertex fn vertexMain() -> @builtin(position) vec4f {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        }
        @fragment fn fragmentMain() -> @location(0) vec4f {
            return vec4f(0.0, 0.0, 0.0, a);
        })");

    utils::ComboRenderPipelineDescriptor desc;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vertexMain";
    desc.cFragment.module = module;
    desc.cFragment.entryPoint = "fragmentMain";
    desc.cTargets[0].writeMask = wgpu::ColorWriteMask::None;

    std::vector<wgpu::ConstantEntry> constants{{nullptr, "a", 0.5}};
    desc.cFragment.constantCount = constants.size();
    desc.cFragment.constants = constants.data();
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);

    std::vector<wgpu::ConstantEntry> sameConstants{{nullptr, "a", 0.5}};
    desc.cFragment.constantCount = sameConstants.size();
    desc.cFragment.constants = sameConstants.data();
    wgpu::RenderPipeline samePipeline = device.CreateRenderPipeline(&desc);

    std::vector<wgpu::ConstantEntry> otherConstants{{nullptr, "a", 1.0}};
    desc.cFragment.constantCount = otherConstants.size();
    desc.cFragment.constants = otherConstants.data();
    wgpu::RenderPipeline otherPipeline1 = device.CreateRenderPipeline(&desc);

    desc.cFragment.constantCount = 0;
    desc.cFragment.constants = nullptr;
    wgpu::RenderPipeline otherPipeline2 = device.CreateRenderPipeline(&desc);

    EXPECT_NE(pipeline.Get(), otherPipeline1.Get());
    EXPECT_NE(pipeline.Get(), otherPipeline2.Get());
    EXPECT_EQ(pipeline.Get() == samePipeline.Get(), !UsesWire());
}

// Test that Samplers are correctly deduplicated.
TEST_P(ObjectCachingTest, SamplerDeduplication) {
    wgpu::SamplerDescriptor samplerDesc;
    wgpu::Sampler sampler = device.CreateSampler(&samplerDesc);

    wgpu::SamplerDescriptor sameSamplerDesc;
    wgpu::Sampler sameSampler = device.CreateSampler(&sameSamplerDesc);

    wgpu::SamplerDescriptor otherSamplerDescAddressModeU;
    otherSamplerDescAddressModeU.addressModeU = wgpu::AddressMode::Repeat;
    wgpu::Sampler otherSamplerAddressModeU = device.CreateSampler(&otherSamplerDescAddressModeU);

    wgpu::SamplerDescriptor otherSamplerDescAddressModeV;
    otherSamplerDescAddressModeV.addressModeV = wgpu::AddressMode::Repeat;
    wgpu::Sampler otherSamplerAddressModeV = device.CreateSampler(&otherSamplerDescAddressModeV);

    wgpu::SamplerDescriptor otherSamplerDescAddressModeW;
    otherSamplerDescAddressModeW.addressModeW = wgpu::AddressMode::Repeat;
    wgpu::Sampler otherSamplerAddressModeW = device.CreateSampler(&otherSamplerDescAddressModeW);

    wgpu::SamplerDescriptor otherSamplerDescMagFilter;
    otherSamplerDescMagFilter.magFilter = wgpu::FilterMode::Linear;
    wgpu::Sampler otherSamplerMagFilter = device.CreateSampler(&otherSamplerDescMagFilter);

    wgpu::SamplerDescriptor otherSamplerDescMinFilter;
    otherSamplerDescMinFilter.minFilter = wgpu::FilterMode::Linear;
    wgpu::Sampler otherSamplerMinFilter = device.CreateSampler(&otherSamplerDescMinFilter);

    wgpu::SamplerDescriptor otherSamplerDescMipmapFilter;
    otherSamplerDescMipmapFilter.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    wgpu::Sampler otherSamplerMipmapFilter = device.CreateSampler(&otherSamplerDescMipmapFilter);

    wgpu::SamplerDescriptor otherSamplerDescLodMinClamp;
    otherSamplerDescLodMinClamp.lodMinClamp += 1;
    wgpu::Sampler otherSamplerLodMinClamp = device.CreateSampler(&otherSamplerDescLodMinClamp);

    wgpu::SamplerDescriptor otherSamplerDescLodMaxClamp;
    otherSamplerDescLodMaxClamp.lodMaxClamp += 1;
    wgpu::Sampler otherSamplerLodMaxClamp = device.CreateSampler(&otherSamplerDescLodMaxClamp);

    wgpu::SamplerDescriptor otherSamplerDescCompareFunction;
    otherSamplerDescCompareFunction.compare = wgpu::CompareFunction::Always;
    wgpu::Sampler otherSamplerCompareFunction =
        device.CreateSampler(&otherSamplerDescCompareFunction);

    EXPECT_NE(sampler.Get(), otherSamplerAddressModeU.Get());
    EXPECT_NE(sampler.Get(), otherSamplerAddressModeV.Get());
    EXPECT_NE(sampler.Get(), otherSamplerAddressModeW.Get());
    EXPECT_NE(sampler.Get(), otherSamplerMagFilter.Get());
    EXPECT_NE(sampler.Get(), otherSamplerMinFilter.Get());
    EXPECT_NE(sampler.Get(), otherSamplerMipmapFilter.Get());
    EXPECT_NE(sampler.Get(), otherSamplerLodMinClamp.Get());
    EXPECT_NE(sampler.Get(), otherSamplerLodMaxClamp.Get());
    EXPECT_NE(sampler.Get(), otherSamplerCompareFunction.Get());
    EXPECT_EQ(sampler.Get() == sameSampler.Get(), !UsesWire());
}

DAWN_INSTANTIATE_TEST(ObjectCachingTest,
                      D3D11Backend(),
                      D3D12Backend(),
                      MetalBackend(),
                      OpenGLBackend(),
                      OpenGLESBackend(),
                      VulkanBackend());
