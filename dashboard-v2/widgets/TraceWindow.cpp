//
// Created by ki608 on 2022-03-18.
//

#include "TraceWindow.hpp"

#include "imgui.h"
#include "imgui_extension.h"
#include "perfkit/common/macros.hxx"
#include "perfkit/common/refl/object.hxx"
#include "perfkit/common/refl/rpc/rpc.hxx"
#include "perfkit/common/refl/rpc/service.hxx"
#include "perfkit/common/utility/cleanup.hxx"

void widgets::TraceWindow::BuildService(rpc::service_builder& s)
{
    using proto::notify;
    using Self = TraceWindow;

    s.route(notify::new_tracer, bind_front(&Self::_fnOnNewTracer, this));
    s.route(notify::new_trace_node, bind_front(&Self::_fnOnNewTraceNode, this));
    s.route(notify::deleted_tracer, bind_front(&Self::_fnOnDestroyTracer, this));
    s.route(notify::trace_node_update, bind_front(&Self::_fnOnTraceUpdate, this));
}

void widgets::TraceWindow::Render(bool* bKeepOpen)
{
    ImGui::SetNextWindowSize({480, 272}, ImGuiCond_Once);

    CPPH_CALL_ON_EXIT(ImGui::End());
    if (not ImGui::Begin(usprintf("traces [%s]###%s", _host->DisplayString().c_str(), _host->KeyString().c_str()), bKeepOpen))
        return;

    /// Render all tracer roots recursively
    _cachedTpNow = steady_clock::now();

    // Render traces recursively
    for (auto& tracer : _tracers)
    {
        if (tracer.bIsTracingCached)
            ImGui::PushStyleColor(ImGuiCol_Header, ColorRefs::BackOkayDim);
        else
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));

        /// Draw header
        bool bVisibleCross = true;
        tracer.bIsTracingCached = ImGui::CollapsingHeader(
                tracer.info.name.c_str(),
                tracer.bIsTracingCached ? &bVisibleCross : nullptr,
                ImGuiTreeNodeFlags_AllowItemOverlap);
        ImGui::PopStyleColor();

        if (not tracer.bIsTracingCached)
            continue;

        ImGui::PushID(&tracer);
        CPPH_CALL_ON_EXIT(ImGui::PopID());

        /// Draw spinner
        auto requestIntervalMs = float(tracer.tmNextPublish.interval_sec().count() * 1e3);
        {
            char       spinText[] = " ------------- ";
            auto const groundLen = (size(spinText) - 3) - 1;
            uint64_t   starAt;

            auto       alpha = min(1.f, tracer._updateGap / requestIntervalMs * 1e3f);
            auto       delta = uint64_t(alpha * (tracer.fence - tracer._fencePrev));
            tracer._updateGap += ImGui::GetIO().DeltaTime;

            starAt = (tracer._fencePrev) % (groundLen * 2);
            starAt = starAt < groundLen ? starAt : groundLen - (starAt % groundLen);
            spinText[starAt + 1] = '*';

            starAt = (tracer._fencePrev + delta) % (groundLen * 2);
            starAt = starAt < groundLen ? starAt : groundLen - (starAt % groundLen);
            spinText[starAt + 1] = '?';

            starAt = (tracer.fence) % (groundLen * 2);
            starAt = starAt < groundLen ? starAt : groundLen - (starAt % groundLen);
            spinText[starAt + 1] = '#';

            ImGui::SameLine();
            ImGui::TextColored({1, 1, .2, 1}, "%s", spinText);

            ImGui::SameLine();
            ImGui::TextDisabled("%llu", tracer.fence);
        }

        /// Draw interval control
        if (not bVisibleCross)
            ImGui::OpenPopup("CONF");

        if (ImGui::IsPopupOpen("CONF") && ImGui::BeginPopup("CONF"))
        {
            CPPH_CALL_ON_EXIT(ImGui::EndPopup());

            if (ImGui::SliderFloat("Intervals", &requestIntervalMs, 1, 1000, "%6.0f ms"))
            {
                tracer.tmNextPublish.reset(requestIntervalMs * 1.ms);
            }
        }

        /// Publish subscribe request periodically
        if (tracer.tmNextPublish.check_sparse() && tracer._waitExpiry < _cachedTpNow)
        {
            tracer._waitExpiry = steady_clock::now() + 5s;
            proto::service::trace_request_update(_host->RpcSession()).notify(tracer.info.tracer_id);
        }

        for (auto idx : tracer.rootNodeIndices)
        {
            auto root = tracer.nodes[idx].get();

            ImGui::PushID(root);
            _recurseRootTraceNode(&tracer, root);
            ImGui::PopID();
        }
    }
}

void widgets::TraceWindow::Tick()
{
    if (_host->SessionAnchor().expired())
    {
        _tracers.clear();
        return;
    }
}

void widgets::TraceWindow::_fnOnNewTracer(proto::tracer_descriptor_t& trc)
{
    PostEventMainThreadWeak(
            _host->SessionAnchor(), [this, trc = trc]() mutable {
                // If same ID-ed tracer has republished, delete existing local context
                if (auto idx = _findTracerIndex(trc.tracer_id); ~size_t{} != idx)
                    _tracers.erase(_tracers.begin() + idx);

                auto* newCtx = &_tracers.emplace_back();
                newCtx->info = move(trc);
            });
}

void widgets::TraceWindow::_fnOnDestroyTracer(uint64_t tracer_id)
{
    PostEventMainThreadWeak(
            _host->SessionAnchor(), [this, tracer_id]() mutable {
                if (auto idx = _findTracerIndex(tracer_id); ~size_t{} != idx)
                    _tracers.erase(_tracers.begin() + idx);
            });
}

size_t widgets::TraceWindow::_findTracerIndex(uint64_t id) const
{
    auto iter = find_if(_tracers, [id](auto&& e) { return e.info.tracer_id == id; });
    if (iter == _tracers.end()) { return ~size_t{}; }

    return distance(_tracers.begin(), iter);
}

void widgets::TraceWindow::_fnOnNewTraceNode(uint64_t tracer_id, vector<proto::trace_info_t>& nodes)
{
    // Sort nodes by index
    sort(nodes, [](auto&& a, auto&& b) { return a.index < b.index; });

    // Send message
    PostEventMainThreadWeak(
            _host->SessionAnchor(), [this, tracer_id, nodes] {
                if (auto tracer = _findTracer(tracer_id))
                {
                    auto curNodes = &tracer->nodes;

                    // Reserve space
                    if (auto maxIdx = nodes.back().index; curNodes->size() <= maxIdx)
                    {
                        curNodes->resize(maxIdx + 1);
                    }

                    for (auto& newNode : nodes)
                    {
                        // Create new node
                        auto& curNode = curNodes->at(newNode.index);
                        curNode = make_unique<TraceNodeContext>();
                        curNode->info = newNode;

                        if (newNode.parent_index == -1)
                        {
                            // Add to root if it has no parent
                            tracer->rootNodeIndices.push_back(newNode.index);
                        }
                        else
                        {
                            // Otherwise, erase from list ... mostly below operation is pointless.
                            auto rIter = remove(tracer->rootNodeIndices, newNode.index);
                            tracer->rootNodeIndices.erase(rIter, tracer->rootNodeIndices.end());

                            // Register as child to parent
                            auto& parent = curNodes->at(newNode.parent_index);
                            auto  uIter = lower_bound(parent->children, newNode.index);

                            // Unique add ...
                            if (uIter == parent->children.end() || *uIter != newNode.index)
                                parent->children.insert(uIter, newNode.index);
                        }
                    }
                }
            });
}

auto widgets::TraceWindow::_findTracer(uint64_t id) -> widgets::TraceWindow::TracerContext*
{
    auto idx = _findTracerIndex(id);

    if (idx == ~size_t{})
        return nullptr;
    else
        return &_tracers[idx];
}

void widgets::TraceWindow::_recurseRootTraceNode(
        TracerContext* tracer, TraceNodeContext* node)
{
    auto nodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanFullWidth;

    if (node->children.empty())
        nodeFlags |= ImGuiTreeNodeFlags_Leaf;

    if (not ImGui::TreeNodeEx(node->info.name.c_str(), nodeFlags))
        return;

    for (auto idx : node->children)
    {
        _recurseRootTraceNode(tracer, tracer->nodes.at(idx).get());
    }

    ImGui::TreePop();
}

void widgets::TraceWindow::_fnOnTraceUpdate(
        uint64_t tracer_id, vector<proto::trace_update_t>& updates)
{
    PostEventMainThreadWeak(
            _host->SessionAnchor(), [this, tracer_id, updates] {
                if (auto tracer = _findTracer(tracer_id))
                {
                    tracer->_fencePrev = tracer->fence;
                    tracer->_updateGap = 0;

                    tracer->_waitExpiry = {};
                    auto nodes = &tracer->nodes;

                    for (auto& update : updates)
                    {
                        // Update fence
                        tracer->fence = max(tracer->fence, uint64_t(update.fence_value));

                        // Ignore updates that can't be processed
                        if (nodes->size() <= update.index)
                            continue;

                        auto node = (*nodes)[update.index].get();
                        node->data = update;
                        node->_updateAt = steady_clock::now();
                    }

                    // TODO: Sort all node's children by their fence, to make fresh nodes
                    //  precede obsolete ones.
                }
            });
}
