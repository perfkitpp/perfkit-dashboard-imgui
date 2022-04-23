//
// Created by ki608 on 2022-03-18.
//

#include "TraceWindow.hpp"

#include "cpph/macros.hxx"
#include "cpph/refl/object.hxx"
#include "cpph/refl/rpc/rpc.hxx"
#include "cpph/refl/rpc/service.hxx"
#include "cpph/utility/cleanup.hxx"
#include "imgui.h"
#include "imgui_extension.h"
#include "imgui_internal.h"
#include "spdlog/spdlog.h"

void widgets::TraceWindow::BuildService(rpc::service_builder& s)
{
    using proto::notify;
    using Self = TraceWindow;

    s.route(notify::new_tracer, bind_front(&Self::_fnOnNewTracer, this));
    s.route(notify::new_trace_node, bind_front(&Self::_fnOnNewTraceNode, this));
    s.route(notify::validate_tracer_list, bind_front(&Self::_fnOnValidateTracer, this));
    s.route(notify::trace_node_update, bind_front(&Self::_fnOnTraceUpdate, this));
}

void widgets::TraceWindow::Render(bool* bKeepOpen)
{
    CPPH_CALL_ON_EXIT(ImGui::End());
    ImGui::SetNextWindowSize({480, 272}, ImGuiCond_Once);
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
                usprintf("%s##%llu", tracer.info.name.c_str(), tracer.info.tracer_id),
                tracer.bIsTracingCached ? &bVisibleCross : nullptr,
                ImGuiTreeNodeFlags_AllowItemOverlap);
        ImGui::PopStyleColor();

        if (not tracer.bIsTracingCached)
            continue;

        ImGui::PushID(&tracer);
        CPPH_CALL_ON_EXIT(ImGui::PopID());

        /// Draw spinner
        {
            char spinText[] = "[             ]";
            auto const groundLen = (size(spinText) - 3) - 1;
            uint64_t starPos, starAt;

            auto alpha = min(1.f, tracer._updateGap / tracer._actualDeltaUpdateSec);
            auto delta = uint64_t(alpha * (tracer.fence - tracer._fencePrev)) % (groundLen * 3);
            tracer._updateGap += ImGui::GetIO().DeltaTime;

            for (int i = 0; i <= delta; ++i)
            {
                starPos = (tracer.fence + i) % (groundLen * 2);
                starAt = starPos < groundLen ? starPos : groundLen - (starPos % groundLen);
                spinText[starAt + 1] = '-';
            }

            starPos = (tracer.fence + delta) % (groundLen * 2);
            starAt = starPos < groundLen ? starPos : groundLen - (starPos % groundLen);
            spinText[starAt + 1] = starPos < groundLen ? '>' : '<';

            starAt = (tracer.fence) % (groundLen * 2);
            starAt = starAt < groundLen ? starAt : groundLen - (starAt % groundLen);
            spinText[starAt + 1] = '@';

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, {0, 1, 1, .7});
            ImGui::TextUnformatted(spinText, spinText + starAt + 1);

            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Text, ColorRefs::FrontOkay);
            ImGui::TextUnformatted(spinText + starAt + 1, spinText + starAt + 2);

            ImGui::SameLine(0, 0);
            ImGui::PopStyleColor();
            ImGui::TextUnformatted(spinText + starAt + 2, spinText + sizeof spinText - 1);

            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::TextColored({.6, .6, .6, .8}, "[ %llu ]", tracer.fence);
        }

        /// Draw interval control
        if (not bVisibleCross)
            ImGui::OpenPopup("CONF");

        if (ImGui::IsPopupOpen("CONF") && ImGui::BeginPopup("CONF"))
        {
            CPPH_CALL_ON_EXIT(ImGui::EndPopup());

            auto requestIntervalMs = float(tracer.tmNextPublish.interval_sec().count() * 1e3);
            if (ImGui::SliderFloat("Intervals", &requestIntervalMs, 1, 1000, "%6.0f ms"))
            {
                tracer.tmNextPublish.reset(requestIntervalMs * 1.ms);
            }
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

    /// Publish subscribe request periodically.
    // This operation is performed regardless of window visibility.
    for (auto& tracer : _tracers)
    {
        if (tracer.bIsTracingCached && tracer.tmNextPublish.check_sparse() && tracer._waitExpiry < _cachedTpNow)
        {
            tracer._waitExpiry = steady_clock::now() + 5s;
            proto::service::trace_request_update(_host->RpcSession()).notify(tracer.info.tracer_id);
        }
    }
}

void widgets::TraceWindow::_fnOnNewTracer(proto::tracer_descriptor_t& trc)
{
    PostEventMainThreadWeak(
            _host->SessionAnchor(), [this, trc = trc]() mutable {
                // If same ID-ed tracer has republished, delete existing local context
                if (auto idx = _findTracerIndex(trc.tracer_id); ~size_t{} != idx)
                    if (_tracers[idx].info.tracer_id == trc.tracer_id)
                        return;

                auto* newCtx = &_tracers.emplace_back();
                newCtx->info = move(trc);
            });
}

void widgets::TraceWindow::_fnOnValidateTracer(vector<uint64_t>& aliveTracers)
{
    sort(aliveTracers);

    PostEventMainThreadWeak(
            _host->SessionAnchor(), [this, aliveTracers = std::move(aliveTracers)]() mutable {
                erase_if_each(_tracers, [&](TracerContext& t) {
                    return not binary_search(aliveTracers, t.info.tracer_id);
                });
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
                        if (curNode != nullptr) { continue; }

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
                            auto uIter = lower_bound(parent->children, newNode.index);

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

static ImU32 VisitPayloadEntity(string* out, nullptr_t const& value)
{
    *out = "";
    return ColorRefs::GlyphKeyword;
}

static ImU32 VisitPayloadEntity(string* out, steady_clock::duration const& value)
{
    auto sec = std::chrono::duration_cast<decltype(1.s)>(value).count();

    fmt::format_to(back_inserter(*out), "{:.3f} ms", sec * 1e3);
    return ColorRefs::GlyphUserType;
}

static ImU32 VisitPayloadEntity(string* out, int64_t const& value)
{
    fmt::format_to(back_inserter(*out), "{}", value);
    return ColorRefs::GlyphNumber;
}

static ImU32 VisitPayloadEntity(string* out, double const& value)
{
    fmt::format_to(back_inserter(*out), "{:g}", value);
    return ColorRefs::GlyphNumber;
}

static ImU32 VisitPayloadEntity(string* out, string const& value)
{
    *out = value;
    return ColorRefs::GlyphString;
}

static ImU32 VisitPayloadEntity(string* out, bool const& value)
{
    *out = value ? "true" : "false";
    return ColorRefs::GlyphKeyword;
}

void widgets::TraceWindow::_recurseRootTraceNode(
        TracerContext* tracer, TraceNodeContext* node)
{
    auto nodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanFullWidth;

    if (node->children.empty())
        nodeFlags |= ImGuiTreeNodeFlags_Leaf;

    // Highlight node text if node is latest ...
    using std::chrono::duration_cast;
    bool const bIsActiveNode = node->data.fence_value == tracer->fence;

    if (bIsActiveNode)
        ImGui::PushStyleColor(ImGuiCol_Text, 0xffbbbbbb);
    else
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));

    // Draw node label
    auto const bSkipChildren = not ImGui::TreeNodeEx(usprintf("%s", node->info.name.c_str(), node->info.index));
    ImGui::PopStyleColor();

    bool const bShowContentTooltip = ImGui::IsItemHovered();
    bool bToggleSubsription = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    bool bTogglePlotWindow = ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    // Draw primitives
    auto dl = ImGui::GetWindowDrawList();

    if (node->data.ref_subscr())
    {
        ImGui::SameLine();
        auto at = ImGui::GetCursorScreenPos();
        at.y += ImGui::GetTextLineHeight() / 3;
        dl->AddCircleFilled(at, ImGui::GetFontSize() / 6, bIsActiveNode ? 0xff00ff00 : 0x4400ff00);
        ImGui::Spacing();
    }

    // TODO: Draw red dot on plot recording

    if (bToggleSubsription)
    {
        // Toggle subscription state
        proto::service::trace_control_t arg;
        arg.subscribe = not node->data.ref_subscr();

        proto::service::trace_request_control(_host->RpcSession())
                .notify(tracer->info.tracer_id, node->info.index, arg);
    }

    // Draw node value
    ImGui::SameLine();
    {
        auto& builder = _reusedStringBuilder;
        builder.clear();

        ImU32 color = std::visit([&](auto&& e) { return VisitPayloadEntity(&builder, e); }, node->data.payload);
        ImGui::PushStyleColor(ImGuiCol_Text, color);

        auto size = ImGui::CalcTextSize(builder.data(), builder.data() + builder.size());
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - size.x));

        ImGui::Selectable("##SEL_ACT");
        ImGui::SameLine(0, 0);

        bTogglePlotWindow = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        bool bDrawValueToolTip = ImGui::IsItemHovered();

        ImGui::TextUnformatted(builder.data(), builder.data() + builder.size());

        if (bDrawValueToolTip)
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(DpiScale() * 480);
            ImGui::TextUnformatted(builder.data(), builder.data() + builder.size());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        ImGui::PopStyleColor();
    }

    if (bTogglePlotWindow)
    {
        ImGui::Text("DDDDD");
    }

    if (bSkipChildren) { return; }

    CPPH_CALL_ON_EXIT(ImGui::TreePop());

    for (auto idx : node->children)
    {
        _recurseRootTraceNode(tracer, tracer->nodes.at(idx).get());
    }
}

void widgets::TraceWindow::_fnOnTraceUpdate(
        uint64_t tracer_id, vector<proto::trace_update_t>& updates)
{
    PostEventMainThreadWeak(
            _host->SessionAnchor(), [this, tracer_id, updates] {
                if (auto tracer = _findTracer(tracer_id))
                {
                    tracer->_actualDeltaUpdateSec = float(tracer->_tmActualDeltaUpdate.elapsed().count());
                    tracer->_tmActualDeltaUpdate.reset();

                    tracer->tmNextPublish.reset();

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
                        assert(node->info.index == update.index);
                        node->data = update;
                        node->_updateAt = steady_clock::now();

                        // TODO: Plot link data push
                    }

                    // TODO: Sort all node's children by their fence, to make fresh nodes
                    //  precede obsolete ones.
                }
            });
}
