//
// Created by ki608 on 2022-04-29.
//

#pragma once
#include <memory>

#include <asio/io_context.hpp>
#include <asio/ip/udp.hpp>

#include "cpph/utility/chrono.hxx"
#include "interfaces/Session.hpp"
#include "perfkit/extension/net/protocol.hpp"

namespace message = net::message;

class SessionDiscoverAgent : public std::enable_shared_from_this<SessionDiscoverAgent>,
                             public ISession
{
    struct FindMeNode {
        steady_clock::time_point latestRefresh;
        message::find_me_t payload;
    };

   private:
    asio::system_context _iocSystem;
    asio::ip::udp::socket _sockDiscover{_iocSystem};
    map<asio::ip::udp::endpoint, FindMeNode> _findMe;

    struct AsyncRecvType {
        char buffer[1024];
        asio::ip::udp::endpoint endpoint;
    } _asyncRecv;

   public:
    void InitializeSession(const string& keyUri) override;
    void FetchSessionDisplayName(std::string* string_1) override;
    bool ShouldRenderSessionListEntityContent() const override;
    void RenderSessionListEntityContent() override;
    bool CanOpenSession() override { return false; }
    bool CanDeleteSession() override { return false; }

   private:
    void onRecvFindMe(asio::ip::udp::endpoint&, message::find_me_t&);
};
