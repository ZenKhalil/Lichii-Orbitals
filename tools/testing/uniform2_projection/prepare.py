# SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Extract exact changed methods for a bounded state/ownership projection."""
from pathlib import Path
import hashlib
import json
import argparse
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[3])
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
REPO = args.source.resolve()
HERE = args.output.resolve()
HERE.mkdir(parents=True, exist_ok=False)
shutil.copyfile(Path(__file__).with_name('projection.cpp'), HERE / 'projection.cpp')
DIR = REPO / 'src/video_core/renderer_vulkan'
queue = (DIR / 'vk_update_descriptor.h').read_text()
header = (DIR / 'vk_compute_pipeline.h').read_text()
source = (DIR / 'vk_compute_pipeline.cpp').read_text()
rasterizer = (DIR / 'vk_rasterizer.cpp').read_text()
cache = (DIR / 'vk_pipeline_cache.cpp').read_text()


def method(text, signature):
    start = text.index(signature)
    at = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at]


queue_signatures = [
    'const DescriptorUpdateEntry* UpdateData()', 'size_t UpdateCount()',
    'bool ObserveTexelBufferTuple(',
    '[[nodiscard]] std::optional<TexelBufferObservation::Result> TakeTexelBufferTuple()',
    'void AddBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)',
    'void AddTexelBuffer(VkBufferView texel_buffer)',
    'void AddTexelBuffer(VkBufferView texel_buffer, VkDeviceAddress base_address,',
]
queue_methods = '\n\n'.join(method(queue, sig) for sig in queue_signatures)
(HERE / 'projection-queue.inc').write_text(queue_methods + '\n')
inline = '\n\n'.join(method(header, sig) for sig in ['bool IsBound()', '[[nodiscard]] bool IsBuildTaskDone()'])
(HERE / 'projection-inline.inc').write_text(inline + '\n')
signatures = ['void ComputePipeline::SetUniform2Family(',
              'ComputePipeline* ComputePipeline::SelectUniform2Pipeline(',
              'ComputePipeline::PreparedUniform2 ComputePipeline::ResolveUniform2Pipeline()']
original = '\n\n'.join(method(source, sig) for sig in signatures)
# Only substitute the class name; preserve every actual method body and type.
(HERE / 'projection-methods.inc').write_text(original.replace('ComputePipeline', 'Projection') + '\n')
guards = {
    'configure_uses_resolver': 'const auto prepared = ResolveUniform2Pipeline();\n        selected = prepared.pipeline;' in source,
    'bind_captures_selected': 'auto bind = [this, selected, descriptor_data,' in source,
    'bind_uses_selected': 'cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *selected->pipeline);' in source,
    'caller_gets_selection': '*selected_pipeline = selected;' in source,
    'indirect_capture': '[this, pipeline, selected_pipeline, indirect_buffer = buffer->Handle(),' in rasterizer,
    'direct_capture': '[this, pipeline, selected_pipeline, dim, byte_addressed_shadow, bind = std::move(deferred_bind)' in rasterizer,
    'deferred_bind_before_check': 'if (bind) {\n            bind(cmdbuf);\n        }\n        if (!selected_pipeline->IsBound())' in rasterizer,
    'factory_uses_assembly': 'auto base = BuildUniform2Members<ComputePipeline>' in cache,
    'synchronous_members': 'descriptor_buffer_ring, nullptr, statistics, nullptr, infos[i]' in cache,
    'retirement_checks_both': 'scheduler.IsFree(retired_compute_pipelines.front().first) &&\n           retired_compute_pipelines.front().second->IsBuildTaskDone()' in cache,
}
assert all(guards.values()), guards
files = [DIR / name for name in ('vk_compute_pipeline.cpp', 'vk_compute_pipeline.h',
    'vk_update_descriptor.h', 'vk_rasterizer.cpp', 'vk_pipeline_cache.cpp', 'compute_uniform2.h')]
files += [HERE / name for name in ('projection-queue.inc', 'projection-inline.inc', 'projection-methods.inc', 'projection.cpp')]
(HERE / 'projection-provenance.json').write_text(json.dumps({
    'source_only': True, 'native_run': False,
    'files': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
    'guards': guards, 'method_signatures': signatures,
    'semantics': 'Exact changed family/selection methods and actual queue observation. Projected Vulkan handles, constructor results, payload backing and timeline. Unchanged bind/direct/indirect/deferred captures are source-guarded, not full Configure/Scheduler execution.'
}, indent=2) + '\n')
print('Projection generated and capture guards verified; no native execution.')
