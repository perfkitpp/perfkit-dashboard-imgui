//
// Created by ki608 on 2022-04-29.
//

#include "SessionDiscoverAgent.hpp"

#include "Application.hpp"
#include "cpph/refl/archive/json.hpp"
#include "cpph/streambuf/view.hxx"
#include "imgui.h"

void SessionDiscoverAgent::FetchSessionDisplayName(std::string* string_1)
{
    ISession::FetchSessionDisplayName(string_1);
}

void SessionDiscoverAgent::InitializeSession(const string& keyUri)
{
    using asio::ip::udp;
    auto epBroad = udp::endpoint{asio::ip::address_v4::any(), message::find_me_port};
    _sockDiscover.open(epBroad.protocol());
    _sockDiscover.set_option(udp::socket::reuse_address{true});
    _sockDiscover.set_option(udp::socket::broadcast{true});
    _sockDiscover.bind(epBroad);

    enum
    {
        BUFSIZE = 1024
    };

    auto fnAccept = y_combinator{
            [w_self = weak_from_this(), this]  //
            (auto&& fnSelf, auto&& ec, size_t nRecv) {
                if (ec) { return; }

                auto _lock_ = w_self.lock();
                if (not _lock_) { return; }

                // Push it to discovered list.
                try
                {
                    message::find_me_t find_me;
                    streambuf::view buf{{_asyncRecv.buffer, nRecv}};
                    archive::json::reader reader{&buf};
                    reader >> find_me;

                    ::PostEventMainThread(
                            bind(&SessionDiscoverAgent::onRecvFindMe, this, _asyncRecv.endpoint, move(find_me)));
                }
                catch (std::exception&)
                {
                }

                _sockDiscover.async_receive_from(
                        asio::buffer(_asyncRecv.buffer),
                        _asyncRecv.endpoint,
                        std::forward<decltype(fnSelf)>(fnSelf));
            }};

    _sockDiscover.async_receive_from(
            asio::buffer(_asyncRecv.buffer),
            _asyncRecv.endpoint,
            std::move(fnAccept));
}

bool SessionDiscoverAgent::ShouldRenderSessionListEntityContent() const
{
    return true;
}

void SessionDiscoverAgent::RenderSessionListEntityContent()
{
    if (not ImGui::BeginListBox("##DiscoverList", {-1, 100 * DpiScale()})) { return; }

    // GC old sessions (5 seconds)
    erase_if_each(_findMe, [](auto&& pair) { return steady_clock::now() - pair.second.latestRefresh > 15s; });

    // Render selectables
    for (auto& [ep, content] : _findMe)
    {
        ImGui::Bullet(), ImGui::SameLine();

        bool bTryOpen = ImGui::Selectable(usfmt("{}##{}", content.payload.alias.c_str(), (void*)&ep));
        ImGui::SameLine();
        ImGui::TextDisabled("%s:%d", ep.address().to_string().c_str(), content.payload.port);

        if (bTryOpen)
        {
            auto fnRegister = [ep = ep, content = content] {
                ESessionType sessionType = ESessionType::TcpUnsafe;  // NOTE: Other type of sessions?
                string keyStr = fmt::format("{}:{}", ep.address().to_string(), content.payload.port);

                auto pNode = Application::Get()->RegisterSessionMainThread(
                        std::move(keyStr), sessionType, content.payload.alias, true);

                if (pNode)
                {
                    NotifyToast{"Registered discovered session"};
                }
                else
                {
                    NotifyToast{"Session is already registered"}.Wanrning();
                }
            };

            // NOTE: Intentionally defer register invocation, to prevent session list modification
            //        during range-based for loop, which makes a call to this
            //        RenderSessionListEntityContent() function.
            PostEventMainThread(std::move(fnRegister));
        }
    }

    ImGui::EndListBox();
}

void SessionDiscoverAgent::onRecvFindMe(asio::ip::udp::endpoint& ep, message::find_me_t& tm)
{
    // TODO: Add node
    auto iter = _findMe.find(ep);
    if (iter == _findMe.end())
    {
        NotifyToast{"New session discovered"}.String("{} ({}:{})", tm.alias, ep.address().to_string(), tm.port);
        iter = _findMe.try_emplace(move(ep)).first;
    }

    auto& [endpoint, content] = *iter;
    content.latestRefresh = steady_clock::now();
    content.payload = std::move(tm);
}

auto CreateSessionDiscoverAgent() -> shared_ptr<ISession>
{
    return make_shared<SessionDiscoverAgent>();
}