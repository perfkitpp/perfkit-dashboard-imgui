//
// Created by ki608 on 2022-04-29.
//

#include "SessionDiscoverAgent.hpp"

#include "cpph/refl/archive/json.hpp"
#include "cpph/streambuf/view.hxx"

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

                    ::PostEventMainThread(bind(&SessionDiscoverAgent::onRecvFindMe, this, _asyncRecv.endpoint, move(find_me)));
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
    return ISession::ShouldRenderSessionListEntityContent();
}

void SessionDiscoverAgent::RenderSessionListEntityContent()
{
    ISession::RenderSessionListEntityContent();
}

void SessionDiscoverAgent::onRecvFindMe(asio::ip::udp::endpoint& ep, message::find_me_t& tm)
{
    // TODO: Add node
}

auto CreateSessionDiscoverAgent() -> shared_ptr<ISession>
{
    return make_shared<SessionDiscoverAgent>();
}