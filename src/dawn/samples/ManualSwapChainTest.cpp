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

// This is an example to manually test swapchain code. Controls are the following, scoped to the
// currently focused window:
//  - W: creates a new window.
//  - L: Latches the current swapchain, to check what happens when the window changes but not the
//    swapchain.
//  - R: switches the rendering mode, between "The Red Triangle" and color-cycling clears that's
//    (WARNING) likely seizure inducing.
//  - D: cycles the divisor for the swapchain size.
//  - P: switches present modes.
//
// Closing all the windows exits the example. ^C also works.
//
// Things to test manually:
//
//  - Basic tests (with the triangle render mode):
//    - Check the triangle is red on a black background and with the pointy side up.
//    - Cycle render modes a bunch and check that the triangle background is always solid black.
//    - Check that rendering triangles to multiple windows works.
//
//  - Present mode single-window tests (with cycling color render mode):
//    - Check that Fifo cycles at about 1 cycle per second and has no tearing.
//    - Check that Mailbox cycles faster than Fifo and has no tearing.
//    - Check that Immediate cycles faster than Fifo, it is allowed to have tearing. (dragging
//      between two monitors can help see tearing)
//
//  - Present mode multi-window tests, it should have the same results as single-window tests when
//    all windows are in the same present mode. In mixed present modes only Immediate windows are
//    allowed to tear.
//
//  - Resizing tests (with the triangle render mode):
//    - Check that cycling divisors on the triangle produces lower and lower resolution triangles.
//    - Check latching the swapchain config and resizing the window a bunch (smaller, bigger, and
//      diagonal aspect ratio).
//
//  - Config change tests:
//    - Check that cycling between present modes works.
//    - TODO can't be tested yet: check cycling the same window over multiple devices.
//    - TODO can't be tested yet: check cycling the same window over multiple formats.

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GLFW/glfw3.h"
#include "dawn/common/Assert.h"
#include "dawn/common/Log.h"
#include "dawn/dawn_proc.h"
#include "dawn/native/DawnNative.h"
#include "dawn/utils/ComboRenderPipelineDescriptor.h"
#include "dawn/utils/ScopedAutoreleasePool.h"
#include "dawn/utils/WGPUHelpers.h"
#include "dawn/webgpu_cpp.h"
#include "webgpu/webgpu_glfw.h"

struct WindowData {
    GLFWwindow* window = nullptr;
    uint64_t serial = 0;

    float clearCycle = 1.0f;
    bool latched = false;
    bool renderTriangle = true;
    uint32_t divisor = 1;

    wgpu::Surface surface = nullptr;
    wgpu::SwapChain swapchain = nullptr;

    wgpu::SwapChainDescriptor currentDesc;
    wgpu::SwapChainDescriptor targetDesc;
};

static std::unordered_map<GLFWwindow*, std::unique_ptr<WindowData>> windows;
static uint64_t windowSerial = 0;

static std::unique_ptr<dawn::native::Instance> instance;
static wgpu::Device device;
static wgpu::Queue queue;
static wgpu::RenderPipeline trianglePipeline;

bool IsSameDescriptor(const wgpu::SwapChainDescriptor& a, const wgpu::SwapChainDescriptor& b) {
    return a.usage == b.usage && a.format == b.format && a.width == b.width &&
           a.height == b.height && a.presentMode == b.presentMode;
}

void OnKeyPress(GLFWwindow* window, int key, int, int action, int);

void SyncFromWindow(WindowData* data) {
    int width;
    int height;
    glfwGetFramebufferSize(data->window, &width, &height);

    data->targetDesc.width = std::max(1u, width / data->divisor);
    data->targetDesc.height = std::max(1u, height / data->divisor);
}

void AddWindow() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(400, 400, "", nullptr, nullptr);
    glfwSetKeyCallback(window, OnKeyPress);

    wgpu::SwapChainDescriptor descriptor;
    descriptor.usage = wgpu::TextureUsage::RenderAttachment;
    descriptor.format = wgpu::TextureFormat::BGRA8Unorm;
    descriptor.width = 0;
    descriptor.height = 0;
    descriptor.presentMode = wgpu::PresentMode::Fifo;

    std::unique_ptr<WindowData> data = std::make_unique<WindowData>();
    data->window = window;
    data->serial = windowSerial++;
    data->surface = wgpu::glfw::CreateSurfaceForWindow(instance->Get(), window);
    data->currentDesc = descriptor;
    data->targetDesc = descriptor;
    SyncFromWindow(data.get());

    windows[window] = std::move(data);
}

void DoRender(WindowData* data) {
    wgpu::TextureView view = data->swapchain.GetCurrentTextureView();
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

    if (data->renderTriangle) {
        utils::ComboRenderPassDescriptor desc({view});
        // Use Load to check the swapchain is lazy cleared (we shouldn't see garbage from previous
        // frames).
        desc.cColorAttachments[0].loadOp = wgpu::LoadOp::Load;

        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&desc);
        pass.SetPipeline(trianglePipeline);
        pass.Draw(3);
        pass.End();
    } else {
        data->clearCycle -= 1.0 / 60.f;
        if (data->clearCycle < 0.0) {
            data->clearCycle = 1.0f;
        }

        utils::ComboRenderPassDescriptor desc({view});
        desc.cColorAttachments[0].loadOp = wgpu::LoadOp::Clear;
        desc.cColorAttachments[0].clearValue = {data->clearCycle, 1.0f - data->clearCycle, 0.0f,
                                                1.0f};

        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&desc);
        pass.End();
    }

    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);

    data->swapchain.Present();
}

std::ostream& operator<<(std::ostream& o, const wgpu::SwapChainDescriptor& desc) {
    // For now only render attachment is possible.
    ASSERT(desc.usage == wgpu::TextureUsage::RenderAttachment);
    o << "RenderAttachment ";
    o << desc.width << "x" << desc.height << " ";

    // For now only BGRA is allowed
    ASSERT(desc.format == wgpu::TextureFormat::BGRA8Unorm);
    o << "BGRA8Unorm ";

    switch (desc.presentMode) {
        case wgpu::PresentMode::Immediate:
            o << "Immediate";
            break;
        case wgpu::PresentMode::Fifo:
            o << "Fifo";
            break;
        case wgpu::PresentMode::Mailbox:
            o << "Mailbox";
            break;
    }
    return o;
}

void UpdateTitle(WindowData* data) {
    std::ostringstream o;

    o << data->serial << " ";
    if (data->divisor != 1) {
        o << "Divisor:" << data->divisor << " ";
    }

    if (data->latched) {
        o << "Latched: (" << data->currentDesc << ") ";
        o << "Target: (" << data->targetDesc << ")";
    } else {
        o << "(" << data->currentDesc << ")";
    }

    glfwSetWindowTitle(data->window, o.str().c_str());
}

void OnKeyPress(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }

    ASSERT(windows.count(window) == 1);

    WindowData* data = windows[window].get();
    switch (key) {
        case GLFW_KEY_W:
            AddWindow();
            break;

        case GLFW_KEY_L:
            data->latched = !data->latched;
            UpdateTitle(data);
            break;

        case GLFW_KEY_R:
            data->renderTriangle = !data->renderTriangle;
            UpdateTitle(data);
            break;

        case GLFW_KEY_D:
            data->divisor *= 2;
            if (data->divisor > 32) {
                data->divisor = 1;
            }
            break;

        case GLFW_KEY_P:
            switch (data->targetDesc.presentMode) {
                case wgpu::PresentMode::Immediate:
                    data->targetDesc.presentMode = wgpu::PresentMode::Fifo;
                    break;
                case wgpu::PresentMode::Fifo:
                    data->targetDesc.presentMode = wgpu::PresentMode::Mailbox;
                    break;
                case wgpu::PresentMode::Mailbox:
                    data->targetDesc.presentMode = wgpu::PresentMode::Immediate;
                    break;
            }
            break;

        default:
            break;
    }
}

int main(int argc, const char* argv[]) {
    // Setup GLFW
    glfwSetErrorCallback([](int code, const char* message) {
        dawn::ErrorLog() << "GLFW error " << code << " " << message;
    });
    if (!glfwInit()) {
        return 1;
    }

    // Choose an adapter we like.
    // TODO(dawn:269): allow switching the window between devices.
    DawnProcTable procs = dawn::native::GetProcs();
    dawnProcSetProcs(&procs);

    instance = std::make_unique<dawn::native::Instance>();
    instance->DiscoverDefaultAdapters();

    std::vector<dawn::native::Adapter> adapters = instance->GetAdapters();
    dawn::native::Adapter chosenAdapter;
    for (dawn::native::Adapter& adapter : adapters) {
        wgpu::AdapterProperties properties;
        adapter.GetProperties(&properties);
        if (properties.backendType != wgpu::BackendType::Null) {
            chosenAdapter = adapter;
            break;
        }
    }
    ASSERT(chosenAdapter);

    // Setup the device on that adapter.
    device = wgpu::Device::Acquire(chosenAdapter.CreateDevice());
    device.SetUncapturedErrorCallback(
        [](WGPUErrorType errorType, const char* message, void*) {
            const char* errorTypeName = "";
            switch (errorType) {
                case WGPUErrorType_Validation:
                    errorTypeName = "Validation";
                    break;
                case WGPUErrorType_OutOfMemory:
                    errorTypeName = "Out of memory";
                    break;
                case WGPUErrorType_Unknown:
                    errorTypeName = "Unknown";
                    break;
                case WGPUErrorType_DeviceLost:
                    errorTypeName = "Device lost";
                    break;
                default:
                    UNREACHABLE();
                    return;
            }
            dawn::ErrorLog() << errorTypeName << " error: " << message;
        },
        nullptr);
    queue = device.GetQueue();

    // The hacky pipeline to render a triangle.
    utils::ComboRenderPipelineDescriptor pipelineDesc;
    pipelineDesc.vertex.module = utils::CreateShaderModule(device, R"(
        @vertex fn main(@builtin(vertex_index) VertexIndex : u32)
                            -> @builtin(position) vec4f {
            var pos = array(
                vec2f( 0.0,  0.5),
                vec2f(-0.5, -0.5),
                vec2f( 0.5, -0.5)
            );
            return vec4f(pos[VertexIndex], 0.0, 1.0);
        })");
    pipelineDesc.cFragment.module = utils::CreateShaderModule(device, R"(
        @fragment fn main() -> @location(0) vec4f {
            return vec4f(1.0, 0.0, 0.0, 1.0);
        })");
    // BGRA shouldn't be hardcoded. Consider having a map[format -> pipeline].
    pipelineDesc.cTargets[0].format = wgpu::TextureFormat::BGRA8Unorm;
    trianglePipeline = device.CreateRenderPipeline(&pipelineDesc);

    // Craete the first window, since the example exits when there are no windows.
    AddWindow();

    while (windows.size() != 0) {
        utils::ScopedAutoreleasePool pool;
        glfwPollEvents();
        wgpuInstanceProcessEvents(instance->Get());

        for (auto it = windows.begin(); it != windows.end();) {
            GLFWwindow* window = it->first;

            if (glfwWindowShouldClose(window)) {
                glfwDestroyWindow(window);
                it = windows.erase(it);
            } else {
                it++;
            }
        }

        for (auto& it : windows) {
            WindowData* data = it.second.get();

            SyncFromWindow(data);
            if (!IsSameDescriptor(data->currentDesc, data->targetDesc) && !data->latched) {
                data->swapchain = device.CreateSwapChain(data->surface, &data->targetDesc);
                data->currentDesc = data->targetDesc;
            }
            UpdateTitle(data);
            DoRender(data);
        }
    }
}
