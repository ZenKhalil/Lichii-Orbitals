// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/exception.h"

namespace VideoCommon {

// One compiler invocation owns a recorder. Replays own only immutable answers;
// neither serialization nor the underlying environment's caches are copied.
class GraphicsInputCapture final : public Shader::Environment {
    using Pair = std::pair<u32, u32>;
    using Triple = std::tuple<u32, u32, u32>;

    struct Data {
        Shader::ProgramHeader header;
        std::array<u32, 8> passthrough;
        Shader::Stage stage;
        u32 start;
        bool proprietary;
        bool native;
        bool native_instructions;
        bool hle;
        u32 texture_bound;
        u32 local_size;
        u32 shared_size;
        std::array<u32, 3> workgroup;
        std::map<u32, u64> instructions;
        std::map<u32, u32> addresses;
        std::map<Pair, u32> cbuf;
        std::map<u32, Shader::TextureType> texture_types;
        std::map<u32, Shader::TexturePixelFormat> texture_formats;
        std::map<u32, bool> integer_formats;
        std::map<Pair, bool> pointers;
        std::map<Triple, bool> indirect_pointers;
        std::map<Pair, std::optional<Shader::ReplaceConstant>> replacements;
        std::optional<u32> viewport;
        std::optional<std::vector<Pair>> null_pointers;
        std::optional<std::vector<u64>> mapped_pointers;
        bool conflict{};
    };

    template <typename Map, typename Key, typename Value>
    static Value Record(Map& map, const Key& key, Value value, bool& conflict) {
        constexpr size_t MaxQueriesPerKind = 32768;
        if (map.size() >= MaxQueriesPerKind && !map.contains(key)) {
            conflict = true;
            return value;
        }
        const auto [it, inserted] = map.emplace(key, value);
        conflict |= !inserted && it->second != value;
        return value; // Preserve the actual answer, including a conflicting repeat.
    }

    template <typename T>
    static T Record(std::optional<T>& first, T value, bool& conflict) {
        if (first) {
            conflict |= *first != value;
        } else {
            first = value;
        }
        return value;
    }

    template <typename Map, typename Key>
    static auto Lookup(const Map& map, const Key& key) {
        const auto found = map.find(key);
        if (found == map.end()) {
            throw Shader::LogicError("Missing captured graphics compiler query");
        }
        return found->second;
    }

    template <typename T>
    static T Lookup(const std::optional<T>& value) {
        if (!value) {
            throw Shader::LogicError("Missing captured graphics compiler property");
        }
        return *value;
    }

public:
    class Replay final : public Shader::Environment {
    public:
        explicit Replay(std::shared_ptr<const Data> data_) : data{std::move(data_)} {
            sph = data->header;
            gp_passthrough_mask = data->passthrough;
            stage = data->stage;
            start_address = data->start;
            is_proprietary_driver = data->proprietary;
        }

        u64 ReadInstruction(u32 address) override {

            return Lookup(data->instructions, address);

        }
        u32 TranslateProgramAddress(u32 address) const override {
            return Lookup(data->addresses, address);
        }
        bool IsNativeProgram() const override {
            return data->native;
        }
        bool HasNativeInstructions() const override {
            return data->native_instructions;
        }
        bool HasHLEMacroState() const override {
            return data->hle;
        }
        u32 TextureBoundBuffer() const override {
            return data->texture_bound;
        }
        u32 LocalMemorySize() const override {
            return data->local_size;
        }
        u32 SharedMemorySize() const override {
            return data->shared_size;
        }
        std::array<u32, 3> WorkgroupSize() const override {
            return data->workgroup;
        }
        u32 ReadCbufValue(u32 index, u32 offset) override {
            return Lookup(data->cbuf, Pair{index, offset});
        }
        Shader::TextureType ReadTextureType(u32 handle) override {
            return Lookup(data->texture_types, handle);
        }
        Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
            return Lookup(data->texture_formats, handle);
        }
        bool IsTexturePixelFormatInteger(u32 handle) override {
            return Lookup(data->integer_formats, handle);
        }
        bool IsCbufStorageBufferPointer(u32 index, u32 offset) override {
            return Lookup(data->pointers, Pair{index, offset});
        }
        bool IsCbufIndirectStorageBufferPointer(u32 index, u32 offset, u32 indirect) override {
            return Lookup(data->indirect_pointers, Triple{index, offset, indirect});
        }
        std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32 index,
                                                                  u32 offset) override {
            return Lookup(data->replacements, Pair{index, offset});
        }
        u32 ReadViewportTransformState() override {
            return Lookup(data->viewport);
        }
        std::vector<Pair> NullStoragePointerSlots() const override {
            return Lookup(data->null_pointers);
        }
        std::vector<u64> MappedStoragePointerKeys() const override {
            return Lookup(data->mapped_pointers);
        }
        void Dump(u64, u64) override {
            // Replaying an owned compiler input produces no new dump.
        }

    private:
        std::shared_ptr<const Data> data;
    };

    class Snapshot {
    public:
        explicit Snapshot(std::shared_ptr<const Data> data_) : data{std::move(data_)} {}

        [[nodiscard]] Replay CreateReplay() const {
            return Replay{data};
        }

    private:
        std::shared_ptr<const Data> data;
    };

    explicit GraphicsInputCapture(Shader::Environment& source_) : source{source_} {
        if (source.ShaderStage() != Shader::Stage::VertexB || !source.IsNativeProgram()) {
            throw Shader::LogicError("Graphics replay requires a native VertexB environment");
        }
        sph = data.header = source.SPH();
        gp_passthrough_mask = data.passthrough = source.GpPassthroughMask();
        stage = data.stage = source.ShaderStage();
        start_address = data.start = source.StartAddress();
        is_proprietary_driver = data.proprietary = source.IsProprietaryDriver();
        data.native = source.IsNativeProgram();
        data.native_instructions = source.HasNativeInstructions();
        data.hle = source.HasHLEMacroState();
        data.texture_bound = source.TextureBoundBuffer();
        data.local_size = source.LocalMemorySize();
        data.shared_size = source.SharedMemorySize();
        data.workgroup = source.WorkgroupSize();
    }

    [[nodiscard]] Snapshot CreateSnapshot() const {
        if (data.conflict || data.instructions.empty()) {
            throw Shader::LogicError("Inconsistent or oversized graphics compiler input capture");
        }
        return Snapshot{std::make_shared<const Data>(data)};
    }

    [[nodiscard]] Replay CreateReplay() const {
        return CreateSnapshot().CreateReplay();
    }

    u64 ReadInstruction(u32 address) override {
        return Record(data.instructions, address, source.ReadInstruction(address), data.conflict);
    }
    u32 TranslateProgramAddress(u32 address) const override {
        return Record(data.addresses, address, source.TranslateProgramAddress(address), data.conflict);
    }
    bool IsNativeProgram() const override {
        return data.native;
    }
    bool HasNativeInstructions() const override {
        return data.native_instructions;
    }
    bool HasHLEMacroState() const override {
        return data.hle;
    }
    u32 TextureBoundBuffer() const override {
        return data.texture_bound;
    }
    u32 LocalMemorySize() const override {
        return data.local_size;
    }
    u32 SharedMemorySize() const override {
        return data.shared_size;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return data.workgroup;
    }
    u32 ReadCbufValue(u32 index, u32 offset) override {
        return Record(data.cbuf, Pair{index, offset}, source.ReadCbufValue(index, offset), data.conflict);
    }
    Shader::TextureType ReadTextureType(u32 handle) override {
        return Record(data.texture_types, handle, source.ReadTextureType(handle), data.conflict);
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        return Record(data.texture_formats, handle, source.ReadTexturePixelFormat(handle), data.conflict);
    }
    bool IsTexturePixelFormatInteger(u32 handle) override {
        return Record(data.integer_formats, handle, source.IsTexturePixelFormatInteger(handle),
                      data.conflict);
    }
    bool IsCbufStorageBufferPointer(u32 index, u32 offset) override {
        return Record(data.pointers, Pair{index, offset},
                      source.IsCbufStorageBufferPointer(index, offset), data.conflict);
    }
    bool IsCbufIndirectStorageBufferPointer(u32 index, u32 offset, u32 indirect) override {
        return Record(data.indirect_pointers, Triple{index, offset, indirect},
                      source.IsCbufIndirectStorageBufferPointer(index, offset, indirect), data.conflict);
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32 index, u32 offset) override {
        return Record(data.replacements, Pair{index, offset},
                      source.GetReplaceConstBuffer(index, offset), data.conflict);
    }
    u32 ReadViewportTransformState() override {
        return Record(data.viewport, source.ReadViewportTransformState(), data.conflict);
    }
    std::vector<Pair> NullStoragePointerSlots() const override {
        return Record(data.null_pointers, source.NullStoragePointerSlots(), data.conflict);
    }
    std::vector<u64> MappedStoragePointerKeys() const override {
        return Record(data.mapped_pointers, source.MappedStoragePointerKeys(), data.conflict);
    }
    void Dump(u64 pipeline_hash, u64 shader_hash) override {
        source.Dump(pipeline_hash, shader_hash);
    }

private:
    Shader::Environment& source;
    mutable Data data{};
};

} // namespace VideoCommon
