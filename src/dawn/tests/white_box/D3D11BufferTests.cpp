// Copyright 2023 The Dawn Authors
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

#include "dawn/native/D3D11Backend.h"
#include "dawn/native/d3d11/BufferD3D11.h"
#include "dawn/native/d3d11/DeviceD3D11.h"
#include "dawn/tests/DawnTest.h"
#include "dawn/utils/ComboRenderPipelineDescriptor.h"
#include "dawn/utils/WGPUHelpers.h"

class D3D11BufferTests : public DawnTest {
  protected:
    void SetUp() override {
        DawnTest::SetUp();
        DAWN_TEST_UNSUPPORTED_IF(UsesWire());
    }

    wgpu::Buffer CreateBuffer(uint32_t bufferSize, wgpu::BufferUsage usage) {
        wgpu::BufferDescriptor descriptor;

        descriptor.size = bufferSize;
        descriptor.usage = usage;

        return device.CreateBuffer(&descriptor);
    }

    ID3D11Device* GetD3D11Device() {
        return dawn::native::d3d11::ToBackend(dawn::native::FromAPI((device.Get())))
            ->GetD3D11Device();
    }

    template <typename T>
    void CheckBuffer(ID3D11Buffer* buffer, std::vector<T> expectedData, uint32_t offset = 0) {
        D3D11_BUFFER_DESC bufferDesc;
        buffer->GetDesc(&bufferDesc);
        EXPECT_GE(bufferDesc.ByteWidth, (expectedData.size() + offset) * sizeof(T));

        // Create D3D11 staging buffer
        D3D11_BUFFER_DESC desc;
        desc.ByteWidth = expectedData.size() * sizeof(T);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        ComPtr<ID3D11Buffer> stagingBuffer;
        ASSERT_HRESULT_SUCCEEDED(GetD3D11Device()->CreateBuffer(&desc, nullptr, &stagingBuffer));

        ID3D11DeviceContext* deviceContext;
        GetD3D11Device()->GetImmediateContext(&deviceContext);

        // Copy buffer to staging buffer
        D3D11_BOX srcBox;
        srcBox.left = offset * sizeof(T);
        srcBox.right = (offset + expectedData.size()) * sizeof(T);
        srcBox.top = 0;
        srcBox.bottom = 1;
        srcBox.front = 0;
        srcBox.back = 1;
        deviceContext->CopySubresourceRegion(stagingBuffer.Get(), 0, 0, 0, 0, buffer, 0, &srcBox);

        // Map staging buffer
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        ASSERT_HRESULT_SUCCEEDED(
            deviceContext->Map(stagingBuffer.Get(), 0, D3D11_MAP_READ, 0, &mappedResource));

        // Check data
        const T* actualData = reinterpret_cast<const T*>(mappedResource.pData);
        for (size_t i = 0; i < expectedData.size(); ++i) {
            EXPECT_EQ(expectedData[i], actualData[i]);
        }

        // Unmap staging buffer
        deviceContext->Unmap(stagingBuffer.Get(), 0);
    }
};

// Test that creating a uniform buffer
TEST_P(D3D11BufferTests, CreateUniformBuffer) {
    {
        wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform;
        wgpu::Buffer buffer = CreateBuffer(4, usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_EQ(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);
    }
    {
        wgpu::BufferUsage usage =
            wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer buffer = CreateBuffer(4, usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_EQ(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);
    }
    {
        wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Vertex;
        wgpu::Buffer buffer = CreateBuffer(4, usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_NE(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);
    }
    {
        wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Index;
        wgpu::Buffer buffer = CreateBuffer(4, usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_NE(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);
    }
    {
        wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Indirect;
        wgpu::Buffer buffer = CreateBuffer(4, usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_NE(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);
    }
    {
        wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Storage;
        wgpu::Buffer buffer = CreateBuffer(4, usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_NE(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);
    }
    {
        wgpu::BufferUsage usage = wgpu::BufferUsage::Storage;
        wgpu::Buffer buffer = CreateBuffer(4, usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_NE(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_EQ(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);
    }
}

// Test Buffer::Write()
TEST_P(D3D11BufferTests, WriteUniformBuffer) {
    {
        std::vector<uint8_t> data = {0x12, 0x34, 0x56, 0x78};
        wgpu::BufferUsage usage =
            wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer buffer = CreateBuffer(data.size(), usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_EQ(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);

        queue.WriteBuffer(buffer, 0, data.data(), data.size());
        EXPECT_BUFFER_U8_RANGE_EQ(data.data(), buffer, 0, data.size());

        CheckBuffer(d3d11Buffer->GetD3D11ConstantBuffer(), data);
    }
    {
        std::vector<uint8_t> data = {0x12, 0x34, 0x56, 0x78};
        wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Vertex |
                                  wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer buffer = CreateBuffer(data.size(), usage);
        dawn::native::d3d11::Buffer* d3d11Buffer =
            dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

        EXPECT_NE(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
        EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);

        queue.WriteBuffer(buffer, 0, data.data(), data.size());
        EXPECT_BUFFER_U8_RANGE_EQ(data.data(), buffer, 0, data.size());

        // both buffers should be updated.
        CheckBuffer(d3d11Buffer->GetD3D11NonConstantBuffer(), data);
        CheckBuffer(d3d11Buffer->GetD3D11ConstantBuffer(), data);
    }
}

// Test UAV write
TEST_P(D3D11BufferTests, WriteUniformBufferWithComputeShader) {
    constexpr size_t kNumValues = 100;
    std::vector<uint32_t> data(kNumValues, 0x12345678);
    uint64_t bufferSize = static_cast<uint64_t>(data.size() * sizeof(uint32_t));
    wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Storage |
                              wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer buffer = CreateBuffer(bufferSize, usage);
    dawn::native::d3d11::Buffer* d3d11Buffer =
        dawn::native::d3d11::ToBackend(dawn::native::FromAPI(buffer.Get()));

    EXPECT_NE(d3d11Buffer->GetD3D11NonConstantBuffer(), nullptr);
    EXPECT_NE(d3d11Buffer->GetD3D11ConstantBuffer(), nullptr);

    queue.WriteBuffer(buffer, 0, data.data(), bufferSize);
    EXPECT_BUFFER_U32_RANGE_EQ(data.data(), buffer, 0, data.size());

    CheckBuffer(d3d11Buffer->GetD3D11NonConstantBuffer(), data);
    CheckBuffer(d3d11Buffer->GetD3D11ConstantBuffer(), data);

    // Fill the buffer with 0x11223344 with a compute shader
    {
        wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
            struct Buf {
                data : array<vec4u, 25>
            }

            @group(0) @binding(0) var<storage, read_write> buf : Buf;

            @compute @workgroup_size(1)
            fn main(@builtin(global_invocation_id) GlobalInvocationID : vec3u) {
                buf.data[GlobalInvocationID.x] =
                    vec4u(0x11223344u, 0x11223344u, 0x11223344u, 0x11223344u);
            }
        )");

        wgpu::ComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = "main";
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pipelineDesc);

        wgpu::BindGroup bindGroupA = utils::MakeBindGroup(device, pipeline.GetBindGroupLayout(0),
                                                          {
                                                              {0, buffer, 0, bufferSize},
                                                          });

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bindGroupA);
        pass.DispatchWorkgroups(kNumValues / 4);
        pass.End();

        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);

        std::vector<uint32_t> expectedData(kNumValues, 0x11223344);
        EXPECT_BUFFER_U32_RANGE_EQ(expectedData.data(), buffer, 0, expectedData.size());
        // The non-constant buffer should be updated.
        CheckBuffer(d3d11Buffer->GetD3D11NonConstantBuffer(), expectedData);
        // The constant buffer should not be updated, until the constant buffer is used a pipeline
        CheckBuffer(d3d11Buffer->GetD3D11ConstantBuffer(), data);
    }

    // Copy the uniform buffer content to a new buffer with Compute shader
    {
        wgpu::Buffer newBuffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
        wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
            struct Buf {
                data : array<vec4u, 25>
            }

            @group(0) @binding(0) var<uniform> src : Buf;
            @group(0) @binding(1) var<storage, read_write> dst : Buf;

            @compute @workgroup_size(1)
            fn main(@builtin(global_invocation_id) GlobalInvocationID : vec3u) {
                dst.data[GlobalInvocationID.x] = src.data[GlobalInvocationID.x];
            }
        )");

        wgpu::ComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = "main";
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pipelineDesc);

        wgpu::BindGroup bindGroupA = utils::MakeBindGroup(device, pipeline.GetBindGroupLayout(0),
                                                          {
                                                              {0, buffer, 0, bufferSize},
                                                              {1, newBuffer, 0, bufferSize},
                                                          });

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bindGroupA);
        pass.DispatchWorkgroups(kNumValues / 4);
        pass.End();

        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);

        std::vector<uint32_t> expectedData(kNumValues, 0x11223344);
        EXPECT_BUFFER_U32_RANGE_EQ(expectedData.data(), buffer, 0, expectedData.size());
        EXPECT_BUFFER_U32_RANGE_EQ(expectedData.data(), newBuffer, 0, expectedData.size());

        // The non-constant buffer should be updated.
        CheckBuffer(d3d11Buffer->GetD3D11NonConstantBuffer(), expectedData);
        // The constant buffer should be updated too.
        CheckBuffer(d3d11Buffer->GetD3D11ConstantBuffer(), expectedData);
    }
}

DAWN_INSTANTIATE_TEST(D3D11BufferTests, D3D11Backend());
