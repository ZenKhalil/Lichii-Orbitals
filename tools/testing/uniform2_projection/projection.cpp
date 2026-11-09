// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Exact-method state/ownership projection; no Vulkan Device or Scheduler is fabricated.
// Driver creation results and completion timeline are explicit fixture inputs.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "video_core/renderer_vulkan/compute_uniform2.h"
#include "video_core/renderer_vulkan/pipeline_helper.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"

using namespace Vulkan;
unsigned checks{};
void Check(bool pass) {
    ++checks;
    if (!pass) {
        throw std::runtime_error("two-uniform projection check failed");
    }
}
template <class F>
void Throws(F&& call) {
    bool threw{};
    try {
        call();
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw);
}
template <class T>
T Handle(uintptr_t value) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(value);
    } else {
        return static_cast<T>(value);
    }
}

class QueueProjection {
public:
    using PreparedTexelBuffer = UpdateDescriptorQueue::PreparedTexelBuffer;
    using TexelBufferObservation = UpdateDescriptorQueue::TexelBufferObservation;
    explicit QueueProjection(bool address) : use_descriptor_buffer{address} {}
    void Reset() {
        tuple_observation.Reset();
        payload_cursor = upload_start;
    }
#include "projection-queue.inc"
private:
    std::array<DescriptorUpdateEntry, 12> payload{};
    DescriptorUpdateEntry* upload_start{payload.data()};
    DescriptorUpdateEntry* payload_cursor{upload_start};
    bool use_descriptor_buffer;
    // The unchanged single-observation branch in AddTexelBuffer stays inactive.
    std::optional<size_t> observed_texel_entry;
    std::optional<PreparedTexelBuffer> observed_texel_format;
    TexelBufferObservation tuple_observation;
};

class Projection {
public:
    inline static int live{};
    explicit Projection(QueueProjection& queue, size_t index)
        : guest_descriptor_queue{queue}, uniform2_types{Uniform2TypesAt(index)}, pipeline{index + 1} {
        ++live;
    }
    ~Projection() {
        --live;
    }
    void SetUniform2Family(std::array<std::unique_ptr<Projection>, Uniform2MemberCount> members,
                           std::array<bool, UniformTupleFormats.size()> formats);
    Projection* SelectUniform2Pipeline(const std::array<VkFormat, 2>& formats) noexcept;
    struct PreparedUniform2 {
        Projection* pipeline;
        UpdateDescriptorQueue::TexelBufferObservation::Result bindings;
    };
    PreparedUniform2 ResolveUniform2Pipeline();
#include "projection-inline.inc"
    QueueProjection& guest_descriptor_queue;
    Shader::Info info;
    std::optional<Shader::ImageBufferStoreType> store_type;
    std::array<std::unique_ptr<Projection>, 3> store_family;
    std::optional<Shader::UniformTexelType> uniform_type;
    std::array<std::unique_ptr<Projection>, 3> uniform_family;
    std::optional<Uniform2Types> uniform2_types;
    std::array<std::unique_ptr<Projection>, Uniform2MemberCount> uniform2_family;
    std::array<bool, UniformTupleFormats.size()> uniform2_formats{};
    size_t num_descriptor_entries{3};
    bool uses_descriptor_buffer{};
    bool uses_push_descriptor{};
    DescriptorBufferLayout descriptor_buffer_layout;
    // Nonzero is a successful driver result; zero is injected driver-null/rejection.
    size_t pipeline{};
    std::atomic_bool build_task_done{true};
};
#include "projection-methods.inc"

int main(int argc, char** argv) {
    if (argc == 2) {
        const bool wanted = std::string_view{argv[1]} == "1";
        Check(MultiUniform2Enabled() == wanted);
#ifdef _WIN32
        _putenv_s("EDEN_NX2_MULTI_UNIFORM2", wanted ? "0" : "1");
#else
        setenv("EDEN_NX2_MULTI_UNIFORM2", wanted ? "0" : "1", 1);
#endif
        Check(MultiUniform2Enabled() == wanted);
        std::printf("selector checks=%u\n", checks);
        return 0;
    }
    std::array<bool, UniformTupleFormats.size()> supported;
    supported.fill(true);
    for (bool address : {false, true}) {
        QueueProjection queue{address};
        for (size_t failure = 0; failure < Uniform2MemberCount; ++failure) {
            for (unsigned mode = 0; mode < 10; ++mode) {
                unsigned entered{}, left{};
                Throws([&] {
                    auto family = BuildUniform2Members<Projection>([&](size_t i) {
                        ++entered;
                        struct Leave { unsigned& count; ~Leave() { ++count; } } leave{left};
                        if (i == failure && mode == 0) {
                            throw std::runtime_error("injected construction failure");
                        }
                        auto member = std::make_unique<Projection>(queue, i);
                        member->uses_descriptor_buffer = address;
                        if (i == failure) {
                            switch (mode) {
                            case 1: member.reset(); break;
                            case 2: member->pipeline = 0; break;
                            case 3: member->build_task_done = false; break;
                            case 4: member->uniform2_types = Uniform2TypesAt((i + 1) % 9); break;
                            case 5: member->info.stores_global_memory = true; break;
                            case 6: member->num_descriptor_entries = 4; break;
                            case 7: member->descriptor_buffer_layout.size = 16; break;
                            case 8:
                                member->descriptor_buffer_layout.bindings.push_back(
                                    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, 8, 16});
                                break;
                            case 9: member->uses_push_descriptor = true; break;
                            }
                        }
                        return member;
                    }, supported);
                    // Mutating the base's metadata is detected by the other eight members.
                });
                Check(entered == left);
                Check(Projection::live == 0);
            }
        }
        auto family = BuildUniform2Members<Projection>([&](size_t i) {
            auto member = std::make_unique<Projection>(queue, i);
            member->uses_descriptor_buffer = address;
            return member;
        }, supported);
        Check(Projection::live == 9);
        std::vector<std::function<void()>> commands;
        std::vector<size_t> observed;
        const auto resolve = [&](VkFormat first, VkFormat second) {
            queue.Reset();
            queue.AddBuffer(Handle<VkBuffer>(1), 8, 32);
            Check(queue.ObserveTexelBufferTuple(std::array<size_t, 2>{1, 2}));
            queue.AddTexelBuffer(Handle<VkBufferView>(21), 0x2000, 13, 17, first);
            queue.AddTexelBuffer(Handle<VkBufferView>(22), 0x3000, 42, 10, second);
            queue.AddTexelBuffer(Handle<VkBufferView>(23), 0x4000, 64, 80, VK_FORMAT_R32_SINT);
            return family->ResolveUniform2Pipeline();
        };
        const auto a = resolve(VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R8_UNORM);
        Check(a.pipeline == family->uniform2_family[3].get());
        Check(a.bindings.bindings[0].view == Handle<VkBufferView>(21));
        Check(a.bindings.bindings[0].offset == 13 && a.bindings.bindings[0].size == 17);
        Check(a.bindings.bindings[1].format == VK_FORMAT_R8_UNORM);
        commands.emplace_back([selected = a.pipeline, &observed] {
            observed.push_back(selected->pipeline);
        });
        const auto b = resolve(VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16_UINT);
        Check(b.pipeline == family->uniform2_family[1].get());
        Check(b.bindings.bindings[1].offset == 42 && b.bindings.bindings[1].size == 10);
        commands.emplace_back([selected = b.pipeline, &observed] {
            observed.push_back(selected->pipeline);
        });
        queue.Reset();
        Throws([&] { family->ResolveUniform2Pipeline(); });
        Throws([&] { resolve(VK_FORMAT_UNDEFINED, VK_FORMAT_R32_UINT); });
        Throws([&] { resolve(VK_FORMAT_R8_UINT, VK_FORMAT_R32_UINT); });
        Check(family->IsBuildTaskDone());
        family->uniform2_family[3]->build_task_done = false;
        Check(!family->IsBuildTaskDone());
        // Explicit timeline double: the real cache's CPU + GPU retirement condition
        // is independently source-reviewed. This does not invoke a Scheduler.
        auto retired = std::move(family);
        bool gpu_free{};
        if (gpu_free && retired->IsBuildTaskDone()) {
            retired.reset();
        }
        Check(Projection::live == 9);
        retired->uniform2_family[3]->build_task_done = true;
        for (auto& command : commands) {
            command();
        }
        Check(observed == std::vector<size_t>{4, 2});
        commands.clear();
        gpu_free = true;
        if (gpu_free && retired->IsBuildTaskDone()) {
            retired.reset();
        }
        Check(Projection::live == 0);
    }
    std::printf("two-uniform projection checks=%u\n", checks);
}
