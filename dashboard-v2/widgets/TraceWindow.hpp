//
// Created by ki608 on 2022-03-18.
//

#pragma once
#include "cpph/thread/locked.hxx"
#include "cpph/timer.hxx"
#include "interfaces/RpcSessionOwner.hpp"
#include "perfkit/extension/net/protocol.hpp"

namespace proto = perfkit::net::message;

namespace cpph::rpc {
class service_builder;
}

namespace widgets {
class TraceWindow
{
   private:
    struct TraceNodeContext
    {
        proto::trace_info_t info;
        proto::trace_update_t data;

        vector<int> children;

        // [transient]
        steady_clock::time_point _updateAt = {};
    };

    struct TracerContext
    {
        proto::tracer_descriptor_t info;
        vector<unique_ptr<TraceNodeContext>> nodes;

        // Index of root nodes
        vector<int> rootNodeIndices;

        // [state]
        uint64_t _fencePrev = 0;
        float _updateGap = 0;
        uint64_t fence = 0;

        // [timers]
        poll_timer tmNextPublish{100ms};
        steady_clock::time_point _waitExpiry = {};
        stopwatch _tmActualDeltaUpdate;
        float _actualDeltaUpdateSec = 0;

        // [transient]
        bool bIsTracingCached = false;
    };

   private:
    IRpcSessionOwner* _host;
    vector<TracerContext> _tracers;

    // [transient]
    steady_clock::time_point _cachedTpNow;

   public:
    explicit TraceWindow(IRpcSessionOwner* host) : _host(host)
    {
    }

   public:
    void BuildService(rpc::service_builder&);
    void Tick();
    void Render(bool* bKeepOpen);

   private:
    void _fnOnNewTracer(proto::tracer_descriptor_t&);
    void _fnOnDestroyTracer(uint64_t);
    void _fnOnNewTraceNode(uint64_t, vector<proto::trace_info_t>&);
    void _fnOnTraceUpdate(uint64_t, vector<proto::trace_update_t>&);

   private:
    size_t _findTracerIndex(uint64_t id) const;
    auto _findTracer(uint64_t id) -> TracerContext*;
    void _recurseRootTraceNode(TracerContext*, TraceNodeContext*);
};
}  // namespace widgets
