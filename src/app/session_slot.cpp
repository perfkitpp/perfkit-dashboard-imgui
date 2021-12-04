#include "session_slot.hpp"

#include "application.hpp"
#include "classes/connection/plain_tcp.hpp"
#include "imgui-extension.h"
#include "spdlog/spdlog.h"

session_slot::session_slot(std::string url, bool from_apiserver)
        : _url(std::move(url)),
          _from_apiserver(from_apiserver)
{
    _shell.resize(8192);
}

void session_slot::render_on_list()
{
    switch (_state)
    {
        case state::disconnected:
        {
            if (ImGui::Button(_fmt.format("CONNECT##{}", _url).c_str()))
            {
                if (_from_apiserver)
                {
                    // TODO: create API server connection
                }
                else
                {
                    auto colon = _url.find_last_of(':');
                    if (colon == std::string::npos)
                        throw session_slot_invalid_url{this};

                    auto address = _url.substr(0, colon);
                    int port;

                    try
                    {
                        auto ports = _url.substr(colon + 1);
                        port       = std::stoi(ports);
                    }
                    catch (std::invalid_argument&)
                    {
                        throw session_slot_invalid_url{this};
                    }
                    catch (std::out_of_range&)
                    {
                        throw session_slot_invalid_url{this};
                    }

                    connection_ptr conn;
                    try
                    {
                        conn = std::make_shared<plain_tcp>(
                                &application::ioc_net(), address.c_str(), port);
                    }
                    catch (asio::system_error& ec)
                    {
                        spdlog::error(
                                "error during tcp connection ... ({}) {}",
                                ec.code().value(), ec.what());
                        return;
                    }

                    _context.reset();
                    _context = std::make_unique<session_context>(std::move(conn));

                    _state = state::connecting;
                }
            }
            ImGui::SameLine();
            _title_string();
        }
        break;

        case state::connecting:
        {
            ImGui::Spinner("##SessionListSpinner", 0xffffffff);
            ImGui::SameLine();
            _title_string();

            if (_context->status() == session_connection_state::invalid)
            {
                _context = {};
                _state   = state::disconnected;
            }
            else if (_context->status() == session_connection_state::connected)
            {
                _state = state::pre_login;
                _context->login(_id, _pw); // simply try with current cache
            }
        }
        break;

        case state::pre_login:
        {
            ImGui::Bullet();
            _title_string();

            if (_context->info())
            {
                _state = state::valid;
            }
            if (_context->status() == session_connection_state::invalid)
            {
                _context = {};
                _state   = state::disconnected;
            }
            else
            {
                ImGui::TreePush();

                ImGui::InputText("ID", _id, sizeof _id);
                ImGui::InputText("PW", _pw, sizeof _pw, ImGuiInputTextFlags_Password);

                bool do_login = ImGui::Button(_fmt.format("  Login  ##{}", _url).c_str());

                ImGui::TreePop();

                if (do_login)
                {
                    _context->login(_id, _pw);
                }
            }
        }
        break;

        case state::valid:
        {
            if (_context->status() == session_connection_state::invalid)
            {
                _context = {};
                _state   = state::disconnected;
                break;
            }

            bool visible = true;

            if (ImGui::CollapsingHeader(
                        _fmt.format("{}@{} ... {}###HEAD:{}",
                                    _context->info()->name,
                                    _url,
                                    "|/-\\"[(int)(ImGui::GetTime() / 0.25f) & 3],
                                    _url)
                                .c_str(),
                        &visible))
            {
                /*   if (ImGui::IsItemFocused())
                    ImGui::SetWindowFocus(_terminal_window_name());*/

                ImGui::BeginChild(_key("STAT:{}", _url));
                ImGui::Text(__FILE__ " (%d): TODO", __LINE__);
                // TODO: session state visualizer
                ImGui::EndChild();
            }

            if (not visible)
            {
                _context = {};
                _state   = state::disconnected;
                break;
            }
        }
        break;

        default:;
    }
}

void session_slot::render_windows()
{
    if (_state != state::valid)
        return;

    bool should_active = false;
    bool keep_open     = true;
    if (ImGui::Begin(_terminal_window_name(), &keep_open))
    {
        ImGui::BeginChild(_key("SHELLOUT:{}", _url), {-1, -48}, true,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);

        _context->shell_output().use(
                [&](std::string const& s)
                {
                    ImGui::TextUnformatted(s.c_str(), s.c_str() + s.size());
                });

        if (_do_autoscroll)
            ImGui::SetScrollY(ImGui::GetScrollMaxY()), _do_autoscroll = false;

        if (_context->consume_recv_char() && not _scroll_lock)
            _do_autoscroll = true;

        ImGui::EndChild();

        ImGui::Checkbox("Scroll Lock", &_scroll_lock);

        ImGui::PushItemWidth(-1);
        bool has_enter = ImGui::InputTextWithHint(
                _key("SHELLIN:{}", _url), "$ enter command here",
                _shell.data(), _shell.size(),
                ImGuiInputTextFlags_CallbackCompletion
                        | ImGuiInputTextFlags_CallbackHistory
                        | ImGuiInputTextFlags_EnterReturnsTrue,
                [](ImGuiInputTextCallbackData* data) -> int
                {
                    // TODO: history,
                    // TODO: autocomplete on tab key
                    return 1;
                },
                this);

        if (has_enter)
        {  // TODO: submit command
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::PopItemWidth();
    }
    ImGui::End();

    if (not keep_open)
    {
        _context = {};
        _state   = state::disconnected;
    }
}

void session_slot::_title_string()
{
    ImGui::Text(_url.c_str());
    ImGui::SameLine();

    bool should_close = false;

    ImGui::PushStyleColor(ImGuiCol_Button, 0xff000077);
    if (not _prompt_close)
    {
        _prompt_close = ImGui::Button(_fmt.format("delete##{}", _url).c_str());
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, 0xff00ffff);
        ImGui::Text("really?");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        should_close = ImGui::Button("yes");

        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::Button("no") && (_prompt_close = false);
    }

    if (should_close)
        throw session_slot_close{this};
}
