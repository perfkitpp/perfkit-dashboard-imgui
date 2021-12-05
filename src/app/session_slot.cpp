#include "session_slot.hpp"

#include "application.hpp"
#include "classes/connection/plain_tcp.hpp"
#include "imgui-extension.h"
#include "imgui_internal.h"
#include "perfkit/common/utility/cleanup.hxx"
#include "spdlog/spdlog.h"

session_slot::session_slot(std::string url, bool from_apiserver)
        : _url(std::move(url)),
          _from_apiserver(from_apiserver)
{
    _command.resize(8192);
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
                _context->login(_id, _pw);  // simply try with current cache
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

            bool visible      = true;
            bool show_submenu = ImGui::CollapsingHeader(
                    _fmt.format("{}@{} ... {}###HEAD:{}",
                                _context->info()->name,
                                _url,
                                "|/-\\"[(int)(ImGui::GetTime() / 0.25f) & 3],
                                _url)
                            .c_str(),
                    &visible,
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick);

            if (ImGui::IsItemClicked())
            {
                ImGui::FocusWindow(ImGui::FindWindowByName(_terminal_window_name()));
            }

            if (show_submenu)
            {
                /*   if (ImGui::IsItemFocused())
                    ImGui::SetWindowFocus(_terminal_window_name());*/

                ImGui::TreePush();
                ImGui::Text(__FILE__ " (%d): TODO", __LINE__);
                // TODO: session state visualizer
                ImGui::TreePop();
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

    bool keep_open = true;
    if (ImGui::Begin(_terminal_window_name(), &keep_open))
    {
        _has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        ImGui::BeginChild(_key("SHELLOUT:{}", _url), {-1, -48}, true,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);

        _context->shell_output().use(
                [&](std::string const& s) {
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
                _command.data(), _command.size(),
                ImGuiInputTextFlags_CallbackCompletion
                        | ImGuiInputTextFlags_CallbackHistory
                        | ImGuiInputTextFlags_EnterReturnsTrue,
                [](ImGuiInputTextCallbackData* data) -> int {
                    // TODO: history,
                    // TODO: autocomplete on tab key
                    return 1;
                },
                this);

        if (has_enter)
        {  // TODO: submit command
            ImGui::SetKeyboardFocusHere(-1);
            _context->push_command(_command.c_str());
            _command[0] = '\0';
        }
        ImGui::PopItemWidth();
    }
    ImGui::End();

    static session_slot* selected_session = nullptr;

    if (ImGui::Begin("Configurations") && selected_session == this)
    {  // Visualize configuration category
        ImGui::Text(_key("{}@{}", _context->info()->name, _url));

        ImGui::BeginChild("");
        auto& conf = _context->configs();
        for (auto& [name, category] : conf)
        {
            if (ImGui::CollapsingHeader(_key("{}##{}", name, _url)))
            {
                _draw_category_recursive(category);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    if (_has_focus)
        selected_session = this;

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

static std::optional<nlohmann::json> prop_editor(
        uint64_t context,
        session_context::config_entity_type const& e)
{
    return {};
}

void session_slot::_draw_category_recursive(
        session_context::config_type const& target)
{
    for (auto& sub : target.subcategories)
    {
        if (not ImGui::TreeNodeEx(
                    _key("{}##{}", sub.name.c_str(), (void*)&sub),
                    ImGuiTreeNodeFlags_SpanFullWidth))
            continue;

        _draw_category_recursive(sub);

        ImGui::TreePop();
    }

    static uint64_t selected_item = 0;

    for (auto& elem : target.entities)
    {
        bool render_modify_view = elem.config_key == selected_item;

        ImGui::PushStyleColor(
                ImGuiCol_Text,
                render_modify_view ? 0xff11ff11
                                   : 0xffaaaaaa);

        ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.3f);
        bool open = ImGui::Selectable(_key("   {}##{}", elem.name, elem.config_key));
        ImGui::PopStyleColor(1);

        if (ImGui::IsItemHovered())
        {
            ImGui::PushTextWrapPos(ImGui::GetWindowWidth());
            ImGui::BeginTooltip();
            ImGui::Text(elem.metadata.dump(2).c_str());
            ImGui::EndTooltip();
            ImGui::PopTextWrapPos();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(
                ImGuiCol_Text,
                elem.value.is_number() || elem.value.is_boolean() ? 0xff56bf6f
                : elem.value.is_string()                          ? 0xff4c87c7
                                                                  : 0xffc7794c);

        if (not elem.value.is_structured())
            ImGui::Text(elem.value.dump().c_str());
        else if (elem.value.is_array())
            ImGui::Text("[array]");
        else
            ImGui::Text("[object]");

        ImGui::PopStyleColor();

        open && (selected_item = elem.config_key);

        if (not render_modify_view)
            continue;

        if (open && render_modify_view)
        {
            selected_item = 0;
            continue;
        }

        ImGui::TreePush();
        auto result = prop_editor(selected_item, elem);

        if (result)
        {
            // TODO: send modify request
        }
        ImGui::TreePop();
    }
}
