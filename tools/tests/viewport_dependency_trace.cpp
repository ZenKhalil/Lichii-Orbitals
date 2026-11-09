// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic checks against the actual optional observer header.
#include "video_core/renderer_vulkan/viewport_dependency_trace.h"
#include <cstdio>
#include <thread>
using namespace Vulkan::ViewportDependencyTrace;
int main(int argc,char** argv) {
    unsigned checks{},failed{},computations{};
    auto check=[&](bool ok){++checks;if(!ok)++failed;};
    const bool expected=argc>1 && std::string_view{argv[1]}=="1";
    check(Enabled()==expected);
    _putenv_s("EDEN_NX2_VIEWPORT_DEPENDENCY_TRACE",expected?"0":"1");
    check(Enabled()==expected); // cached exact opt-in, not a live mutable switch
    Owner gated_owner;State gated_state;
    if(auto guard=Enabled()?gated_owner.TryAccess():Owner::Access{};guard)++gated_state.frame;
    check(gated_state.frame==(expected?1u:0u));
    check((gated_owner.identity.load()!=0)==expected);
    State s;s.Channel(3);
    const auto old_depth=[](unsigned){return Depth{0x3f000000,0x3f800000};};
    const auto new_depth=[&](unsigned){++computations;return Depth{0,0x3f800000};};
    s.Observe(0,true,true,1,old_depth);s.Emitted(0,true,old_depth);
    s.Observe(0,true,false,1,new_depth);check(computations==0);
    s.Observe(1,true,false,1,new_depth);
    check(s.cached_mode==0);check(s.depths[0].minimum==0x3f000000);
    check(computations==16);check(s.mode_changes==1);check(s.clean_mode_changes==1);
    check(s.stale_draws==1);check(s.depth_mismatches==1);check(s.stored==1);
    check(s.records[0].channel==3);check(s.records[0].mask==0xffff);
    check(s.records[0].old_depth.minimum==0x3f000000);
    check(s.records[0].needed_depth.minimum==0);
    s.Observe(1,true,false,1,new_depth);check(s.stored==1);check(s.depth_mismatches==2);
    for(unsigned frame=2;frame<100;++frame)s.Observe(1,true,false,frame,new_depth);
    check(s.stored==32);check(s.last.frame==99);check(s.depth_mismatches==100);
    s.Observe(1,true,true,100,new_depth);s.Emitted(1,true,new_depth);
    const auto before=computations;s.Observe(1,true,false,101,new_depth);check(computations==before);
    s.Emitted(1,false,old_depth);s.Observe(0,false,false,102,new_depth);check(computations==before);
    auto mode_changes=s.mode_changes;s.Channel(9);s.Observe(1,true,true,103,new_depth);
    check(s.mode_changes==mode_changes);
    check(!s.ReportDue(100));check(!s.ReportDue(5'000'000'099ULL));
    check(s.ReportDue(5'000'000'100ULL));check(!s.ReportDue(5'000'000'100ULL));
    State limited;limited.Channel(1);limited.Emitted(0,true,old_depth,2);
    const auto count_before=computations;limited.Observe(1,true,false,1,new_depth,2);
    check(computations-count_before==2);check(limited.last.mask==3);
    Owner same;
    { auto guard=same.TryAccess();check(bool(guard)); }
    { auto guard=same.TryAccess();check(bool(guard)); }
    Owner held;
    { auto guard=held.TryAccess();check(bool(guard));
      std::thread contender([&] { auto denied=held.TryAccess(); if(denied)std::abort(); });
      contender.join();check(held.skipped.load()==1);check(held.unknown.load()); }
    { auto denied=held.TryAccess();check(!denied); }
    check(held.WarnOnce());check(!held.WarnOnce());
    Owner migrated;
    { auto guard=migrated.TryAccess();check(bool(guard)); }
    std::thread foreign([&] { auto denied=migrated.TryAccess(); if(denied)std::abort(); });
    foreign.join();check(migrated.mismatches.load()==1);check(migrated.unknown.load());
    Owner throwing;
    try { auto guard=throwing.TryAccess();check(bool(guard));throw 7; } catch(int) {}
    { auto guard=throwing.TryAccess();check(bool(guard)); }
    Owner nested;
    { auto outer=nested.TryAccess();check(bool(outer));
      { auto inner=nested.TryAccess();check(!inner); }
      check(nested.skipped.load()==1);check(nested.unknown.load()); }
    const bool was_busy=nested.busy.test_and_set(std::memory_order_acquire);
    check(!was_busy);nested.busy.clear(std::memory_order_release);
    State max_count;max_count.Emitted(0,true,old_depth,100);
    const auto before_max=computations;max_count.Observe(1,true,false,1,new_depth,100);
    check(computations-before_max==16);check(max_count.last.mask==0xffff);
    State zero;zero.Emitted(0,true,old_depth,0);
    const auto before_zero=computations;zero.Observe(1,true,false,1,new_depth,0);
    check(computations==before_zero);check(!zero.has_last);check(zero.stored==0);
    State nan;const auto nan_depth=[](unsigned){return Depth{0x7fc00001,0x7f800000};};
    nan.Emitted(0,true,nan_depth,1);nan.Observe(1,true,false,1,nan_depth,1);
    check(nan.depth_mismatches==0);check(nan.stored==0);
    check(s.ReportDue(10'000'000'100ULL));
    for(unsigned n=0;n<100;++n)check(!s.ReportDue(10'000'000'100ULL+n));
    std::printf("checks=%u failed=%u\n",checks,failed);return failed?1:0;
}
