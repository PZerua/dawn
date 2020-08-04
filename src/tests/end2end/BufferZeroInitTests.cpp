// Copyright 2020 The Dawn Authors
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

#include "tests/DawnTest.h"

#include "utils/ComboRenderPipelineDescriptor.h"
#include "utils/WGPUHelpers.h"

#define EXPECT_LAZY_CLEAR(N, statement)                                                       \
    do {                                                                                      \
        if (UsesWire()) {                                                                     \
            statement;                                                                        \
        } else {                                                                              \
            size_t lazyClearsBefore = dawn_native::GetLazyClearCountForTesting(device.Get()); \
            statement;                                                                        \
            size_t lazyClearsAfter = dawn_native::GetLazyClearCountForTesting(device.Get());  \
            EXPECT_EQ(N, lazyClearsAfter - lazyClearsBefore);                                 \
        }                                                                                     \
    } while (0)

namespace {

    struct BufferZeroInitInCopyT2BSpec {
        wgpu::Extent3D textureSize;
        uint64_t bufferOffset;
        uint64_t extraBytes;
        uint32_t bytesPerRow;
        uint32_t rowsPerImage;
        uint32_t lazyClearCount;
    };

}  // anonymous namespace

class BufferZeroInitTest : public DawnTest {
  public:
    wgpu::Buffer CreateBuffer(uint64_t size,
                              wgpu::BufferUsage usage,
                              bool mappedAtCreation = false) {
        wgpu::BufferDescriptor descriptor;
        descriptor.size = size;
        descriptor.usage = usage;
        descriptor.mappedAtCreation = mappedAtCreation;
        return device.CreateBuffer(&descriptor);
    }

    void MapAsyncAndWait(wgpu::Buffer buffer,
                         wgpu::MapMode mapMode,
                         uint64_t offset,
                         uint64_t size) {
        ASSERT(mapMode == wgpu::MapMode::Read || mapMode == wgpu::MapMode::Write);

        bool done = false;
        buffer.MapAsync(
            mapMode, offset, size,
            [](WGPUBufferMapAsyncStatus status, void* userdata) {
                ASSERT_EQ(WGPUBufferMapAsyncStatus_Success, status);
                *static_cast<bool*>(userdata) = true;
            },
            &done);

        while (!done) {
            WaitABit();
        }
    }

    wgpu::Texture CreateAndInitializeTexture(const wgpu::Extent3D& size,
                                             wgpu::TextureFormat format,
                                             wgpu::Color color = {0.f, 0.f, 0.f, 0.f}) {
        wgpu::TextureDescriptor descriptor;
        descriptor.size = size;
        descriptor.format = format;
        descriptor.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc |
                           wgpu::TextureUsage::OutputAttachment | wgpu::TextureUsage::Storage;
        wgpu::Texture texture = device.CreateTexture(&descriptor);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

        for (uint32_t arrayLayer = 0; arrayLayer < size.depth; ++arrayLayer) {
            wgpu::TextureViewDescriptor viewDescriptor;
            viewDescriptor.format = format;
            viewDescriptor.dimension = wgpu::TextureViewDimension::e2D;
            viewDescriptor.baseArrayLayer = arrayLayer;
            viewDescriptor.arrayLayerCount = 1u;

            utils::ComboRenderPassDescriptor renderPassDescriptor(
                {texture.CreateView(&viewDescriptor)});
            renderPassDescriptor.cColorAttachments[0].clearColor = color;
            wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);
            renderPass.EndPass();
        }

        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        queue.Submit(1, &commandBuffer);

        return texture;
    }

    void TestBufferZeroInitInCopyTextureToBuffer(const BufferZeroInitInCopyT2BSpec& spec) {
        constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::R32Float;
        ASSERT(utils::GetTexelBlockSizeInBytes(kTextureFormat) * spec.textureSize.width %
                   kTextureBytesPerRowAlignment ==
               0);

        constexpr wgpu::Color kClearColor = {0.5f, 0.5f, 0.5f, 0.5f};
        wgpu::Texture texture =
            CreateAndInitializeTexture(spec.textureSize, kTextureFormat, kClearColor);

        const wgpu::TextureCopyView textureCopyView =
            utils::CreateTextureCopyView(texture, 0, {0, 0, 0});

        const uint64_t bufferSize = spec.bufferOffset + spec.extraBytes +
                                    utils::RequiredBytesInCopy(spec.bytesPerRow, spec.rowsPerImage,
                                                               spec.textureSize, kTextureFormat);
        wgpu::Buffer buffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst);
        const wgpu::BufferCopyView bufferCopyView = utils::CreateBufferCopyView(
            buffer, spec.bufferOffset, spec.bytesPerRow, spec.rowsPerImage);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyTextureToBuffer(&textureCopyView, &bufferCopyView, &spec.textureSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(spec.lazyClearCount, queue.Submit(1, &commandBuffer));

        const uint64_t expectedValueCount = bufferSize / sizeof(float);
        std::vector<float> expectedValues(expectedValueCount, 0.f);

        for (uint32_t slice = 0; slice < spec.textureSize.depth; ++slice) {
            const uint64_t baseOffsetBytesPerSlice =
                spec.bufferOffset + spec.bytesPerRow * spec.rowsPerImage * slice;
            for (uint32_t y = 0; y < spec.textureSize.height; ++y) {
                const uint64_t baseOffsetBytesPerRow =
                    baseOffsetBytesPerSlice + spec.bytesPerRow * y;
                const uint64_t baseOffsetFloatCountPerRow = baseOffsetBytesPerRow / sizeof(float);
                for (uint32_t x = 0; x < spec.textureSize.width; ++x) {
                    expectedValues[baseOffsetFloatCountPerRow + x] = 0.5f;
                }
            }
        }

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_FLOAT_RANGE_EQ(expectedValues.data(), buffer, 0,
                                                           expectedValues.size()));
    }

    void TestBufferZeroInitInBindGroup(const char* computeShader,
                                       uint64_t bufferOffset,
                                       uint64_t boundBufferSize,
                                       const std::vector<uint32_t>& expectedBufferData) {
        wgpu::ComputePipelineDescriptor pipelineDescriptor;
        pipelineDescriptor.layout = nullptr;
        pipelineDescriptor.computeStage.module =
            utils::CreateShaderModule(device, utils::SingleShaderStage::Compute, computeShader);
        pipelineDescriptor.computeStage.entryPoint = "main";
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pipelineDescriptor);

        const uint64_t bufferSize = expectedBufferData.size() * sizeof(uint32_t);
        wgpu::Buffer buffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc |
                                         wgpu::BufferUsage::Storage | wgpu::BufferUsage::Uniform);
        wgpu::Texture outputTexture =
            CreateAndInitializeTexture({1u, 1u, 1u}, wgpu::TextureFormat::RGBA8Unorm);

        wgpu::BindGroup bindGroup = utils::MakeBindGroup(
            device, pipeline.GetBindGroupLayout(0),
            {{0, buffer, bufferOffset, boundBufferSize}, {1u, outputTexture.CreateView()}});

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
        computePass.SetBindGroup(0, bindGroup);
        computePass.SetPipeline(pipeline);
        computePass.Dispatch(1u);
        computePass.EndPass();
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(expectedBufferData.data(), buffer, 0,
                                                         expectedBufferData.size()));

        constexpr RGBA8 kExpectedColor = {0, 255, 0, 255};
        EXPECT_PIXEL_RGBA8_EQ(kExpectedColor, outputTexture, 0u, 0u);
    }

    wgpu::RenderPipeline CreateRenderPipelineForTest(const char* vertexShader) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;

        wgpu::ShaderModule vsModule =
            utils::CreateShaderModule(device, utils::SingleShaderStage::Vertex, vertexShader);

        wgpu::ShaderModule fsModule =
            utils::CreateShaderModule(device, utils::SingleShaderStage::Fragment, R"(
                #version 450
                layout(location = 0) in vec4 i_color;
                layout(location = 0) out vec4 fragColor;
                void main() {
                    fragColor = i_color;
                })");

        utils::ComboRenderPipelineDescriptor descriptor(device);
        descriptor.vertexStage.module = vsModule;
        descriptor.cFragmentStage.module = fsModule;
        descriptor.primitiveTopology = wgpu::PrimitiveTopology::PointList;
        descriptor.cVertexState.vertexBufferCount = 1;
        descriptor.cVertexState.indexFormat = wgpu::IndexFormat::Uint16;
        descriptor.cVertexState.cVertexBuffers[0].arrayStride = 4 * sizeof(float);
        descriptor.cVertexState.cVertexBuffers[0].attributeCount = 1;
        descriptor.cVertexState.cAttributes[0].format = wgpu::VertexFormat::Float4;
        descriptor.cColorStates[0].format = kColorAttachmentFormat;
        return device.CreateRenderPipeline(&descriptor);
    }

    void TestBufferZeroInitAsVertexBuffer(uint64_t vertexBufferOffset) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;

        const char* vertexShader = R"(
            #version 450
            layout(location = 0) in vec4 pos;
            layout(location = 0) out vec4 o_color;
            void main() {
                if (pos == vec4(0.f, 0.f, 0.f, 0.f)) {
                    o_color = vec4(0.f, 1.f, 0.f, 1.f);
                } else {
                    o_color = vec4(1.f, 0.f, 0.f, 1.f);
                }
                gl_Position = vec4(0.f, 0.f, 0.f, 1.f);
                gl_PointSize = 1.0f;
            })";
        wgpu::RenderPipeline renderPipeline = CreateRenderPipelineForTest(vertexShader);

        constexpr uint64_t kVertexAttributeSize = sizeof(float) * 4;
        const uint64_t vertexBufferSize = kVertexAttributeSize + vertexBufferOffset;
        wgpu::Buffer vertexBuffer =
            CreateBuffer(vertexBufferSize, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopySrc |
                                               wgpu::BufferUsage::CopyDst);
        wgpu::Texture colorAttachment =
            CreateAndInitializeTexture({1, 1, 1}, kColorAttachmentFormat);
        utils::ComboRenderPassDescriptor renderPassDescriptor({colorAttachment.CreateView()});

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);

        // Bind the buffer with offset == vertexBufferOffset and size kVertexAttributeSize as the
        // vertex buffer.
        renderPass.SetVertexBuffer(0, vertexBuffer, vertexBufferOffset, kVertexAttributeSize);
        renderPass.SetPipeline(renderPipeline);
        renderPass.Draw(1);
        renderPass.EndPass();

        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        // Although we just bind a part of the buffer, we still expect the whole buffer to be
        // lazily initialized to 0.
        const std::vector<float> expectedVertexBufferData(vertexBufferSize / sizeof(float), 0);
        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_FLOAT_RANGE_EQ(expectedVertexBufferData.data(), vertexBuffer, 0,
                                             expectedVertexBufferData.size()));

        const RGBA8 kExpectedPixelValue = {0, 255, 0, 255};
        EXPECT_PIXEL_RGBA8_EQ(kExpectedPixelValue, colorAttachment, 0, 0);
    }

    void TestBufferZeroInitAsIndexBuffer(uint64_t indexBufferOffset) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;

        const char* vertexShader = R"(
            #version 450
            layout(location = 0) out vec4 o_color;
            void main() {
                if (gl_VertexIndex == 0u) {
                    o_color = vec4(0.f, 1.f, 0.f, 1.f);
                } else {
                    o_color = vec4(1.f, 0.f, 0.f, 1.f);
                }
                gl_Position = vec4(0.f, 0.f, 0.f, 1.f);
                gl_PointSize = 1.0f;
            })";
        wgpu::RenderPipeline renderPipeline = CreateRenderPipelineForTest(vertexShader);

        // It is not allowed to use an index buffer without a vertex buffer although the vertex
        // buffer is not used. So here we use an initialized dummy buffer as vertex buffer.
        constexpr std::array<float, 4> kVertexBufferData = {0, 0, 0, 0};
        constexpr uint64_t kVertexBufferSize = sizeof(kVertexBufferData);
        wgpu::Buffer vertexBuffer =
            utils::CreateBufferFromData(device, kVertexBufferData.data(), kVertexBufferSize,
                                        wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst);

        // The buffer size cannot be less than 4
        const uint64_t indexBufferSize = sizeof(uint32_t) + indexBufferOffset;
        wgpu::Buffer indexBuffer =
            CreateBuffer(indexBufferSize, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopySrc |
                                              wgpu::BufferUsage::CopyDst);

        wgpu::Texture colorAttachment =
            CreateAndInitializeTexture({1, 1, 1}, kColorAttachmentFormat);
        utils::ComboRenderPassDescriptor renderPassDescriptor({colorAttachment.CreateView()});

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);
        renderPass.SetVertexBuffer(0, vertexBuffer, 0, kVertexBufferSize);
        renderPass.SetPipeline(renderPipeline);

        // Bind the buffer with offset == indexBufferOffset and size sizeof(uint32_t) as the index
        // buffer.
        renderPass.SetIndexBuffer(indexBuffer, indexBufferOffset, sizeof(uint32_t));
        renderPass.DrawIndexed(1);
        renderPass.EndPass();

        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        // Although we just bind a part of the buffer, we still expect the whole buffer to be
        // lazily initialized to 0.
        const std::vector<uint32_t> expectedIndexBufferData(indexBufferSize / sizeof(uint32_t), 0);
        EXPECT_LAZY_CLEAR(0u,
                          EXPECT_BUFFER_U32_RANGE_EQ(expectedIndexBufferData.data(), indexBuffer, 0,
                                                     expectedIndexBufferData.size()));

        const RGBA8 kExpectedPixelValue = {0, 255, 0, 255};
        EXPECT_PIXEL_RGBA8_EQ(kExpectedPixelValue, colorAttachment, 0, 0);
    }
};

// Test that calling writeBuffer to overwrite the entire buffer doesn't need to lazily initialize
// the destination buffer.
TEST_P(BufferZeroInitTest, WriteBufferToEntireBuffer) {
    constexpr uint32_t kBufferSize = 8u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {
        {0x02020202u, 0x02020202u}};
    EXPECT_LAZY_CLEAR(0u, queue.WriteBuffer(buffer, 0, kExpectedData.data(), kBufferSize));

    EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), buffer, 0,
                                                     kBufferSize / sizeof(uint32_t)));
}

// Test that calling writeBuffer to overwrite a part of buffer needs to lazily initialize the
// destination buffer.
TEST_P(BufferZeroInitTest, WriteBufferToSubBuffer) {
    constexpr uint32_t kBufferSize = 8u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;

    constexpr uint32_t kCopyValue = 0x02020202u;

    // offset == 0
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint32_t kCopyOffset = 0u;
        EXPECT_LAZY_CLEAR(1u,
                          queue.WriteBuffer(buffer, kCopyOffset, &kCopyValue, sizeof(kCopyValue)));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(kCopyValue, buffer, kCopyOffset));
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(0, buffer, kBufferSize - sizeof(kCopyValue)));
    }

    // offset > 0
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint32_t kCopyOffset = 4u;
        EXPECT_LAZY_CLEAR(1u,
                          queue.WriteBuffer(buffer, kCopyOffset, &kCopyValue, sizeof(kCopyValue)));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(0, buffer, 0));
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(kCopyValue, buffer, kCopyOffset));
    }
}

// Test that the code path of CopyBufferToBuffer clears the source buffer correctly when it is the
// first use of the source buffer.
TEST_P(BufferZeroInitTest, CopyBufferToBufferSource) {
    constexpr uint64_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
    wgpu::BufferDescriptor bufferDescriptor;
    bufferDescriptor.size = kBufferSize;
    bufferDescriptor.usage = kBufferUsage;

    constexpr std::array<uint8_t, kBufferSize> kInitialData = {
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};

    wgpu::Buffer dstBuffer =
        utils::CreateBufferFromData(device, kInitialData.data(), kBufferSize, kBufferUsage);

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {{0, 0, 0, 0}};

    // Full copy from the source buffer
    {
        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, 0, kBufferSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }

    // Partial copy from the source buffer
    // srcOffset == 0
    {
        constexpr uint64_t kSrcOffset = 0;
        constexpr uint64_t kCopySize = kBufferSize / 2;

        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, kSrcOffset, dstBuffer, 0, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }

    // srcOffset > 0 and srcOffset + copySize == srcBufferSize
    {
        constexpr uint64_t kSrcOffset = kBufferSize / 2;
        constexpr uint64_t kCopySize = kBufferSize - kSrcOffset;

        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, kSrcOffset, dstBuffer, 0, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }

    // srcOffset > 0 and srcOffset + copySize < srcBufferSize
    {
        constexpr uint64_t kSrcOffset = kBufferSize / 4;
        constexpr uint64_t kCopySize = kBufferSize / 2;

        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, kSrcOffset, dstBuffer, 0, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }
}

// Test that the code path of CopyBufferToBuffer clears the destination buffer correctly when it is
// the first use of the destination buffer.
TEST_P(BufferZeroInitTest, CopyBufferToBufferDestination) {
    constexpr uint64_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
    wgpu::BufferDescriptor bufferDescriptor;
    bufferDescriptor.size = kBufferSize;
    bufferDescriptor.usage = kBufferUsage;

    const std::array<uint8_t, kBufferSize> kInitialData = {
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    wgpu::Buffer srcBuffer =
        utils::CreateBufferFromData(device, kInitialData.data(), kBufferSize, kBufferUsage);

    // Full copy from the source buffer doesn't need lazy initialization at all.
    {
        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, 0, kBufferSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(0u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<const uint32_t*>(kInitialData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }

    // Partial copy from the source buffer needs lazy initialization.
    // offset == 0
    {
        constexpr uint32_t kDstOffset = 0;
        constexpr uint32_t kCopySize = kBufferSize / 2;

        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, kDstOffset, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        std::array<uint8_t, kBufferSize> expectedData;
        expectedData.fill(0);
        for (uint32_t index = kDstOffset; index < kDstOffset + kCopySize; ++index) {
            expectedData[index] = kInitialData[index - kDstOffset];
        }

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<uint32_t*>(expectedData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }

    // offset > 0 and dstOffset + CopySize == kBufferSize
    {
        constexpr uint32_t kDstOffset = kBufferSize / 2;
        constexpr uint32_t kCopySize = kBufferSize - kDstOffset;

        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, kDstOffset, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        std::array<uint8_t, kBufferSize> expectedData;
        expectedData.fill(0);
        for (uint32_t index = kDstOffset; index < kDstOffset + kCopySize; ++index) {
            expectedData[index] = kInitialData[index - kDstOffset];
        }

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<uint32_t*>(expectedData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }

    // offset > 0 and dstOffset + CopySize < kBufferSize
    {
        constexpr uint32_t kDstOffset = kBufferSize / 4;
        constexpr uint32_t kCopySize = kBufferSize / 2;

        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, kDstOffset, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        std::array<uint8_t, kBufferSize> expectedData;
        expectedData.fill(0);
        for (uint32_t index = kDstOffset; index < kDstOffset + kCopySize; ++index) {
            expectedData[index] = kInitialData[index - kDstOffset];
        }

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<uint32_t*>(expectedData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }
}

// Test that the code path of readable buffer mapping clears the buffer correctly when it is the
// first use of the buffer.
TEST_P(BufferZeroInitTest, MapReadAsync) {
    constexpr uint32_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;

    constexpr wgpu::MapMode kMapMode = wgpu::MapMode::Read;

    // Map the whole buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, 0, kBufferSize));

        const uint32_t* mappedDataUint = static_cast<const uint32_t*>(buffer.GetConstMappedRange());
        for (uint32_t i = 0; i < kBufferSize / sizeof(uint32_t); ++i) {
            EXPECT_EQ(0u, mappedDataUint[i]);
        }
        buffer.Unmap();
    }

    // Map a range of a buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint64_t kOffset = 4u;
        constexpr uint64_t kSize = 8u;
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, kOffset, kSize));

        const uint32_t* mappedDataUint =
            static_cast<const uint32_t*>(buffer.GetConstMappedRange(kOffset));
        for (uint32_t i = 0; i < kSize / sizeof(uint32_t); ++i) {
            EXPECT_EQ(0u, mappedDataUint[i]);
        }
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(0u, MapAsyncAndWait(buffer, kMapMode, 0, kBufferSize));
        mappedDataUint = static_cast<const uint32_t*>(buffer.GetConstMappedRange());
        for (uint32_t i = 0; i < kBufferSize / sizeof(uint32_t); ++i) {
            EXPECT_EQ(0u, mappedDataUint[i]);
        }
        buffer.Unmap();
    }
}

// Test that the code path of writable buffer mapping clears the buffer correctly when it is the
// first use of the buffer.
TEST_P(BufferZeroInitTest, MapWriteAsync) {
    constexpr uint32_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

    constexpr wgpu::MapMode kMapMode = wgpu::MapMode::Write;

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {{0, 0, 0, 0}};

    // Map the whole buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, 0, kBufferSize));
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<const uint32_t*>(kExpectedData.data()),
                                           buffer, 0, kExpectedData.size()));
    }

    // Map a range of a buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint64_t kOffset = 4u;
        constexpr uint64_t kSize = 8u;
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, kOffset, kSize));
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<const uint32_t*>(kExpectedData.data()),
                                           buffer, 0, kExpectedData.size()));
    }
}

// Test that the code path of creating a buffer with BufferDescriptor.mappedAtCreation == true
// clears the buffer correctly at the creation of the buffer.
TEST_P(BufferZeroInitTest, MapAtCreation) {
    constexpr uint32_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

    wgpu::Buffer buffer;
    EXPECT_LAZY_CLEAR(1u, buffer = CreateBuffer(kBufferSize, kBufferUsage, true));
    buffer.Unmap();

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {{0, 0, 0, 0}};
    EXPECT_LAZY_CLEAR(
        0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<const uint32_t*>(kExpectedData.data()),
                                       buffer, 0, kExpectedData.size()));
}

// Test that the code path of CopyBufferToTexture clears the source buffer correctly when it is the
// first use of the buffer.
TEST_P(BufferZeroInitTest, CopyBufferToTexture) {
    constexpr wgpu::Extent3D kTextureSize = {16u, 16u, 1u};

    constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::R32Uint;

    wgpu::Texture texture = CreateAndInitializeTexture(kTextureSize, kTextureFormat);
    const wgpu::TextureCopyView textureCopyView =
        utils::CreateTextureCopyView(texture, 0, {0, 0, 0});

    const uint32_t requiredBufferSizeForCopy = utils::GetBytesInBufferTextureCopy(
        kTextureFormat, kTextureSize.width, kTextureBytesPerRowAlignment, kTextureSize.width,
        kTextureSize.depth);

    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;

    // bufferOffset == 0
    {
        constexpr uint64_t kOffset = 0;
        const uint32_t totalBufferSize = requiredBufferSizeForCopy + kOffset;
        wgpu::Buffer buffer = CreateBuffer(totalBufferSize, kBufferUsage);
        const wgpu::BufferCopyView bufferCopyView = utils::CreateBufferCopyView(
            buffer, kOffset, kTextureBytesPerRowAlignment, kTextureSize.height);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToTexture(&bufferCopyView, &textureCopyView, &kTextureSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        const std::vector<uint32_t> expectedValues(totalBufferSize / sizeof(uint32_t), 0);
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(expectedValues.data(), buffer, 0,
                                                         totalBufferSize / sizeof(uint32_t)));
    }

    // bufferOffset > 0
    {
        constexpr uint64_t kOffset = 8u;
        const uint32_t totalBufferSize = requiredBufferSizeForCopy + kOffset;
        wgpu::Buffer buffer = CreateBuffer(totalBufferSize, kBufferUsage);
        const wgpu::BufferCopyView bufferCopyView = utils::CreateBufferCopyView(
            buffer, kOffset, kTextureBytesPerRowAlignment, kTextureSize.height);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToTexture(&bufferCopyView, &textureCopyView, &kTextureSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        const std::vector<uint32_t> expectedValues(totalBufferSize / sizeof(uint32_t), 0);
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(expectedValues.data(), buffer, 0,
                                                         totalBufferSize / sizeof(uint32_t)));
    }
}

// Test that the code path of CopyTextureToBuffer clears the destination buffer correctly when it is
// the first use of the buffer and the texture is a 2D non-array texture.
TEST_P(BufferZeroInitTest, Copy2DTextureToBuffer) {
    constexpr wgpu::Extent3D kTextureSize = {64u, 8u, 1u};

    // bytesPerRow == texelBlockSizeInBytes * copySize.width && bytesPerRow * copySize.height ==
    // buffer.size
    {
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kTextureBytesPerRowAlignment, kTextureSize.height, 0u});
    }

    // bytesPerRow > texelBlockSizeInBytes * copySize.width
    {
        constexpr uint64_t kBytesPerRow = kTextureBytesPerRowAlignment * 2;
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kBytesPerRow, kTextureSize.height, 1u});
    }

    // bufferOffset > 0
    {
        constexpr uint64_t kBufferOffset = 16u;
        TestBufferZeroInitInCopyTextureToBuffer({kTextureSize, kBufferOffset, 0u,
                                                 kTextureBytesPerRowAlignment, kTextureSize.height,
                                                 1u});
    }

    // bytesPerRow * copySize.height < buffer.size
    {
        constexpr uint64_t kExtraBufferSize = 16u;
        TestBufferZeroInitInCopyTextureToBuffer({kTextureSize, 0u, kExtraBufferSize,
                                                 kTextureBytesPerRowAlignment, kTextureSize.height,
                                                 1u});
    }
}

// Test that the code path of CopyTextureToBuffer clears the destination buffer correctly when it is
// the first use of the buffer and the texture is a 2D array texture.
TEST_P(BufferZeroInitTest, Copy2DArrayTextureToBuffer) {
    constexpr wgpu::Extent3D kTextureSize = {64u, 4u, 3u};

    // bytesPerRow == texelBlockSizeInBytes * copySize.width && rowsPerImage == copySize.height &&
    // bytesPerRow * (rowsPerImage * (copySize.depth - 1) + copySize.height) == buffer.size
    {
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kTextureBytesPerRowAlignment, kTextureSize.height, 0u});
    }

    // rowsPerImage > copySize.height
    {
        constexpr uint64_t kRowsPerImage = kTextureSize.height + 1u;
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kTextureBytesPerRowAlignment, kRowsPerImage, 1u});
    }

    // bytesPerRow * rowsPerImage * copySize.depth < buffer.size
    {
        constexpr uint64_t kExtraBufferSize = 16u;
        TestBufferZeroInitInCopyTextureToBuffer({kTextureSize, 0u, kExtraBufferSize,
                                                 kTextureBytesPerRowAlignment, kTextureSize.height,
                                                 1u});
    }
}

// Test that the buffer will be lazy initialized correctly when its first use is to be bound as a
// uniform buffer.
TEST_P(BufferZeroInitTest, BoundAsUniformBuffer) {
    // See https://github.com/google/shaderc/issues/1123 for more details.
    // TODO(jiawei.shao@intel.com): enable this test when the related SPVC issue is fixed.
    DAWN_SKIP_TEST_IF(IsSpvcParserBeingUsed());

    const char* computeShader = R"(
        #version 450
        layout(set = 0, binding = 0, std140) uniform UBO {
            uvec4 value;
        } ubo;
        layout(set = 0, binding = 1, rgba8) uniform writeonly image2D outImage;
        void main() {
            if (ubo.value == uvec4(0, 0, 0, 0)) {
                imageStore(outImage, ivec2(0, 0), vec4(0.f, 1.f, 0.f, 1.f));
            } else {
                imageStore(outImage, ivec2(0, 0), vec4(1.f, 0.f, 0.f, 1.f));
            }
        }
    )";

    constexpr uint32_t kBoundBufferSize = 16u;

    // Bind the whole buffer
    {
        const std::vector<uint32_t> expected(kBoundBufferSize / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(computeShader, 0, kBoundBufferSize, expected);
    }

    // Bind a range of a buffer
    {
        constexpr uint32_t kOffset = 256u;
        constexpr uint32_t kExtraBytes = 16u;
        const std::vector<uint32_t> expected(
            (kBoundBufferSize + kOffset + kExtraBytes) / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(computeShader, kOffset, kBoundBufferSize, expected);
    }
}

// Test that the buffer will be lazy initialized correctly when its first use is to be bound as a
// read-only storage buffer.
TEST_P(BufferZeroInitTest, BoundAsReadonlyStorageBuffer) {
    // See https://github.com/google/shaderc/issues/1123 for more details.
    // TODO(jiawei.shao@intel.com): enable this test when the related SPVC issue is fixed.
    DAWN_SKIP_TEST_IF(IsSpvcParserBeingUsed());

    const char* computeShader = R"(
        #version 450
        layout(set = 0, binding = 0, std140) readonly buffer SSBO {
            uvec4 value;
        } ssbo;
        layout(set = 0, binding = 1, rgba8) uniform writeonly image2D outImage;
        void main() {
            if (ssbo.value == uvec4(0, 0, 0, 0)) {
                imageStore(outImage, ivec2(0, 0), vec4(0.f, 1.f, 0.f, 1.f));
            } else {
                imageStore(outImage, ivec2(0, 0), vec4(1.f, 0.f, 0.f, 1.f));
            }
        }
    )";

    constexpr uint32_t kBoundBufferSize = 16u;

    // Bind the whole buffer
    {
        const std::vector<uint32_t> expected(kBoundBufferSize / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(computeShader, 0, kBoundBufferSize, expected);
    }

    // Bind a range of a buffer
    {
        constexpr uint32_t kOffset = 256u;
        constexpr uint32_t kExtraBytes = 16u;
        const std::vector<uint32_t> expected(
            (kBoundBufferSize + kOffset + kExtraBytes) / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(computeShader, kOffset, kBoundBufferSize, expected);
    }
}

// Test that the buffer will be lazy initialized correctly when its first use is to be bound as a
// storage buffer.
TEST_P(BufferZeroInitTest, BoundAsStorageBuffer) {
    // See https://github.com/google/shaderc/issues/1123 for more details.
    // TODO(jiawei.shao@intel.com): enable this test when the related SPVC issue is fixed.
    DAWN_SKIP_TEST_IF(IsSpvcParserBeingUsed());

    const char* computeShader = R"(
        #version 450
        layout(set = 0, binding = 0, std140) buffer SSBO {
            uvec4 value[2];
        } ssbo;
        layout(set = 0, binding = 1, rgba8) uniform writeonly image2D outImage;
        void main() {
            if (ssbo.value[0] == uvec4(0, 0, 0, 0) && ssbo.value[1] == uvec4(0, 0, 0, 0)) {
                imageStore(outImage, ivec2(0, 0), vec4(0.f, 1.f, 0.f, 1.f));
            } else {
                imageStore(outImage, ivec2(0, 0), vec4(1.f, 0.f, 0.f, 1.f));
            }

            memoryBarrier();
            barrier();

            ssbo.value[0].x = 10u;
            ssbo.value[1].y = 20u;
        }
    )";

    constexpr uint32_t kBoundBufferSize = 32u;

    // Bind the whole buffer
    {
        std::vector<uint32_t> expected(kBoundBufferSize / sizeof(uint32_t), 0u);
        expected[0] = 10u;
        expected[5] = 20u;
        TestBufferZeroInitInBindGroup(computeShader, 0, kBoundBufferSize, expected);
    }

    // Bind a range of a buffer
    {
        constexpr uint32_t kOffset = 256u;
        constexpr uint32_t kExtraBytes = 16u;
        std::vector<uint32_t> expected(
            (kBoundBufferSize + kOffset + kExtraBytes) / sizeof(uint32_t), 0u);
        expected[kOffset / sizeof(uint32_t)] = 10u;
        expected[kOffset / sizeof(uint32_t) + 5u] = 20u;
        TestBufferZeroInitInBindGroup(computeShader, kOffset, kBoundBufferSize, expected);
    }
}

// Test the buffer will be lazy initialized correctly when its first use is in SetVertexBuffer.
TEST_P(BufferZeroInitTest, SetVertexBuffer) {
    // Bind the whole buffer as a vertex buffer.
    {
        constexpr uint64_t kVertexBufferOffset = 0u;
        TestBufferZeroInitAsVertexBuffer(kVertexBufferOffset);
    }

    // Bind the buffer as a vertex buffer with a non-zero offset.
    {
        constexpr uint64_t kVertexBufferOffset = 16u;
        TestBufferZeroInitAsVertexBuffer(kVertexBufferOffset);
    }
}

// Test the buffer will be lazy initialized correctly when its first use is in SetIndexBuffer.
TEST_P(BufferZeroInitTest, SetIndexBuffer) {
    // Bind the whole buffer as an index buffer.
    {
        constexpr uint64_t kIndexBufferOffset = 0u;
        TestBufferZeroInitAsIndexBuffer(kIndexBufferOffset);
    }

    // Bind the buffer as an index buffer with a non-zero offset.
    {
        constexpr uint64_t kIndexBufferOffset = 16u;
        TestBufferZeroInitAsIndexBuffer(kIndexBufferOffset);
    }
}

DAWN_INSTANTIATE_TEST(BufferZeroInitTest,
                      D3D12Backend({"nonzero_clear_resources_on_creation_for_testing",
                                    "lazy_clear_buffer_on_first_use"}),
                      MetalBackend({"nonzero_clear_resources_on_creation_for_testing",
                                    "lazy_clear_buffer_on_first_use"}),
                      OpenGLBackend({"nonzero_clear_resources_on_creation_for_testing",
                                     "lazy_clear_buffer_on_first_use"}),
                      VulkanBackend({"nonzero_clear_resources_on_creation_for_testing",
                                     "lazy_clear_buffer_on_first_use"}));
