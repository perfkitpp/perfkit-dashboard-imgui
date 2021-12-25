#include "session_slot.hpp"

#include <perfkit/common/futils.hxx>
#include <perfkit/common/utility/cleanup.hxx>

#include "application.hpp"
#include "classes/connection/plain_tcp.hpp"
#include "imgui-extension.h"
#include "imgui_internal.h"
#include "implot.h"
#include "spdlog/spdlog.h"
#include "utility.hpp"

using namespace std::literals;

session_slot::session_slot(std::string url, bool from_apiserver)
        : _url(std::move(url)),
          _from_apiserver(from_apiserver)
{
    _history.emplace_back();
    _shello.SetShowWhitespaces(false);
    _shello.SetReadOnly(true);

    //_shello.SetColorizerEnable(false);
    //
    // auto palette = _shello.GetPalette();
    // palette[0]   = 0xffffffff;
    //_shello.SetPalette(palette);
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

                    _plots = {};
                    _context.reset();
                    _context = std::make_unique<session_context>(std::move(conn));
                    _context->on_session_state_update
                            = CPPH_BIND(_session_state_update);

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
                _trace_context.emplace(_url, &*_context);

                _shello_fence = 0;
                _shello.SetText({});
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
                // since _trace_context's pending future results depends on
                //  context's promises, context must be disposed first to incur
                //  'future_error' when disposing trace_context.
                _context = {};
                _trace_context.reset();

                _state = state::disconnected;
                break;
            }

            static void* active_plotting = nullptr;
            bool is_plotting             = active_plotting == this;

            bool keep_open      = true;
            bool plotting_label = is_plotting;

            ImGui::AlignTextToFramePadding();
            auto spin       = ":._-^'\"~\\|/*"sv;
            char spinbuf[2] = {};
            spinbuf[0]      = spin[int(ImGui::GetTime() / 0.25f) % spin.size()];

            ImGui::PushStyleColor(ImGuiCol_Text, 0xff00ff00);
            ImGui::TextEx(spinbuf);
            ImGui::PopStyleColor();

            ImGui::SameLine();
            auto on_select = ImGui::Selectable(
                    _fmt.format("{0}@{1}##HEAD:{2}",
                                _context->info()->name,
                                _url,
                                _url)
                            .c_str(),
                    &plotting_label,
                    ImGuiSelectableFlags_SpanAllColumns
                            | ImGuiSelectableFlags_AllowItemOverlap);

            if (ImGui::IsItemClicked()
                && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (is_plotting)
                    active_plotting = nullptr;
                else
                    active_plotting = this;
            }

            if (on_select)
                _has_focus = true;

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, 0xff002288);
            if (ImGui::SmallButton(_key("x##{}", _url)))
            {
                keep_open = false;
            }
            ImGui::PopStyleColor();

            if (is_plotting)
            {
                if (ImGui::Begin(
                            _key("Status Plot", _context->info()->name, _url),
                            &is_plotting))
                {
                    ImGui::BeginChild(
                            "Stauts Plot Child",
                            {}, true,
                            ImGuiWindowFlags_AlwaysAutoResize
                                    | ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    _plot_on_submenu();
                    ImGui::EndChild();
                }

                ImGui::End();

                if (not is_plotting)
                {
                    active_plotting = nullptr;
                }
            }

            if (not keep_open)
            {
                _context = {};
                _state   = state::disconnected;
                break;
            }
        }
        break;

        default:
            break;
    }
}

void session_slot::render_windows()
{
    if (_state != state::valid)
        return;

    static session_slot* selected_session = nullptr;

    if (_has_focus)
    {
        selected_session = this;
        _has_focus       = false;
    }

    if (selected_session == this)
    {
        if (ImGui::Begin("Terminal"))
            _draw_shell();

        ImGui::End();
    }

    if (selected_session == this)
    {  // Visualize configuration category
        if (ImGui::Begin("Configurations"))
        {
            ImGui::TextEx(_key("{}@{}", _context->info()->name, _url));

            ImGui::BeginChild("ConfigurationsChild");
            auto& conf = _context->configs();
            for (auto& [name, category] : conf)
            {
                ImGui::PushStyleColor(ImGuiCol_Header, 0xff361d1d);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0xff804545);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0xff5e3d3d);
                auto enter = ImGui::CollapsingHeader(_key("{}##{}", name, _url));
                ImGui::PopStyleColor(3);

                if (enter)
                    _draw_category_recursive(category);
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    _trace_context->update_always();

    if (selected_session == this)
    {
        if (ImGui::Begin("Traces"))
            _trace_context->update_selected();

        ImGui::End();
    }
}

void session_slot::_draw_shell()
{
    if (auto s = _context->shell_output(&_shello_fence); not s.empty())
        xterm_leap_escape(&_shello, s);

    if (_shello.GetTotalLines() != _shello_color_fence && _shello_colorize_timer.check())
    {
        _shello_color_fence = std::max(_shello_color_fence, _shello.GetTotalLines() - 250);
        _shello.ForceColorize(_shello_color_fence - 1);
        _shello_color_fence = _shello.GetTotalLines();
    }

    _shello.Render(_key("Terminal:{}", _url), {-1, -40}, true);

    if (_do_autoscroll)
    {
        _shello.MoveBottom();
        _shello.MoveEnd();
        ImGui::SetScrollY(ImGui::GetScrollMaxY()), _do_autoscroll = false;
    }

    if (_context->consume_recv_char() && not _scroll_lock)
        _do_autoscroll = true;

    ImGui::Checkbox("Scroll Lock", &_scroll_lock);

    // Commandline
    ImGui::PushItemWidth(-1);

    auto* current_command = &_history.back();
    if (current_command->size() < 1024)
        current_command->resize(1024);

    auto* command  = current_command->data();
    bool has_enter = ImGui::InputTextWithHint(
            _key("SHELLIN:{}", _url), "$ enter command here",
            command, current_command->size() - 1,
            ImGuiInputTextFlags_CallbackCompletion
                    | ImGuiInputTextFlags_CallbackHistory
                    | ImGuiInputTextFlags_EnterReturnsTrue
                    | ImGuiInputTextFlags_CallbackCharFilter
                    | ImGuiInputTextFlags_CallbackAlways,
            [](ImGuiInputTextCallbackData* data) -> int {
                // TODO: history,
                // TODO: autocomplete on tab key
                auto self = (session_slot*)data->UserData;

                if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
                {
                    try
                    {
                        self->_active_suggest.reset();
                        self->_waiting_suggest = self->_context->suggest_command(
                                data->Buf, data->BufTextLen);
                    }
                    catch (std::future_error& e)
                    {
                        SPDLOG_INFO(e.what());
                    }
                }
                else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
                {
                    auto* pcursor = &self->_history_cursor;
                    self->_active_suggest.reset();

                    if (data->EventKey == ImGuiKey_UpArrow)
                    {
                        ++*pcursor;
                    }
                    else if (data->EventKey == ImGuiKey_DownArrow)
                    {
                        --*pcursor;
                    }

                    *pcursor = std::clamp<int64_t>(
                            *pcursor, 0, self->_history.size() - 1);

                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, self->_history.end()[-1 - *pcursor].c_str());
                }
                else if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
                {
                    if (data->EventChar == L' ')
                    {
                        self->_active_suggest.reset();
                    }

                    return 0;
                }
                else if (
                        self->_waiting_suggest.valid()
                        && self->_waiting_suggest.wait_for(0ms) == std::future_status::ready)
                {
                    try
                    {
                        self->_active_suggest.reset();
                        self->_active_suggest = self->_waiting_suggest.get();

                        data->DeleteChars(0, data->BufTextLen);
                        data->InsertChars(0, self->_active_suggest->new_command.c_str());
                    }
                    catch (std::future_error& e)
                    {
                        SPDLOG_INFO(e.what());
                    }
                }

                self->_cmd_prev_cursor = data->CursorPos;
                return 1;
            },
            this);

    if (_active_suggest && not _active_suggest->candidates.empty())
    {
        std::string_view last_word = command;
        auto i_space               = last_word.find_last_of(' ');

        if (i_space != ~size_t{})
            last_word = last_word.substr(i_space + 1);

        auto where = ImGui::GetItemRectMin();
        where.y += 20;
        where.x += ImGui::CalcTextSize(command, command + i_space).x;
        ImGui::SetNextWindowPos(where);
        ImGui::SetNextWindowBgAlpha(0.6);
        ImGui::Begin("AUTOCOMPLETE_POPUP", nullptr,
                     ImGuiWindowFlags_NoDecoration
                             | ImGuiWindowFlags_AlwaysAutoResize
                             | ImGuiWindowFlags_NoNavFocus
                             | ImGuiWindowFlags_NoInputs
                             | ImGuiWindowFlags_NoMouseInputs
                             | ImGuiWindowFlags_NoFocusOnAppearing);

        bool has_any_match = false;
        for (std::string_view suggest : _active_suggest->candidates)
        {
            if (suggest.find(last_word) == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, 0xff45fc42);
                ImGui::TextEx(last_word.data(), last_word.data() + last_word.size());
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 0);
                ImGui::TextEx(suggest.data() + last_word.size(), suggest.data() + suggest.size());

                has_any_match |= true;
            }
        }

        if (not has_any_match)
            _active_suggest.reset();

        ImGui::End();
    }
    else
    {
        _active_suggest.reset();
    }

    if (has_enter)
    {
        ImGui::SetKeyboardFocusHere(-1);
        current_command->resize(strlen(current_command->c_str()));

        if (not current_command->empty())
        {
            _context->push_command(*current_command);

            if (_history.size() > 2 && _history.end()[-1] == _history.end()[-2])
                (*current_command)[0] = '\0';
            else
                _history.emplace_back();

            _history_cursor = 0;
            _active_suggest = {};
        }
    }
    ImGui::PopItemWidth();
}

void session_slot::_title_string()
{
    ImGui::TextEx(_url.c_str());

    bool should_close = false;

    if (_state != state ::connecting)
    {  // If deleted during connecting, deadlock occurs.
        ImGui::SameLine();
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
            if (ImGui::Button("no")) { _prompt_close = false; }
        }
    }

    if (should_close)
        throw session_slot_close{this};
}

static bool prop_editor_recursive_impl(
        char const* label_base,
        nlohmann::json* e,
        nlohmann::json const* min,
        nlohmann::json const* max)
{
    bool has_change = false;

    if (e->is_object() || e->is_array())
    {
        size_t idx    = 0;
        auto find_ptr = [&](auto from, auto key) {
            nlohmann::json const* r = {};

            if (from && from->is_object())
                if (auto it = from->find(key); it != from->end())
                {
                    r = &*it;
                }

            if (from && from->is_array() && from->size() > idx)
                r = &(*from)[idx];

            return r;
        };

        ImGui::Text(e->is_object() ? "<object>" : "<array>");
        ImGui::TreePush();

        for (auto& [key, value] : e->items())
        {
            char label[256];
            snprintf(label, sizeof label, "%s.%s", label_base, key.c_str());

            ImGui::PushStyleColor(ImGuiCol_Text, 0xffab8446);
            ImGui::TextEx(key.c_str());
            ImGui::PopStyleColor(1);

            ImGui::SameLine();
            has_change |= prop_editor_recursive_impl(
                    label,
                    &value,
                    find_ptr(min, key),
                    find_ptr(max, key));

            ++idx;
        }

        ImGui::TreePop();
    }
    else if (e->is_boolean())
    {
        has_change |= ImGui::Checkbox(label_base, e->get_ptr<bool*>());
    }
    else if (e->is_string())
    {
        static TextEditor editor;
        static std::string* editing = nullptr;

        auto str           = e->get_ptr<std::string*>();
        auto readonly_flag = 0;

        if (str == editing)
        {
            bool keep_open = true;
            if (ImGui::Begin("String Property Editor", &keep_open, ImGuiWindowFlags_NoDocking))
            {
                // TODO: reinforce this
                //       - syntax mode
                editor.Render("String Property Editor Body", {});

                if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
                {
                    keep_open = false;
                }
                else if (ImGui::IsKeyPressed('S')
                         && (ImGui::GetMergedKeyModFlags() & ImGuiKeyModFlags_Ctrl))
                {
                    *editing = editor.GetText();
                    has_change |= true;
                }
                else
                {
                    readonly_flag |= ImGuiInputTextFlags_ReadOnly;
                }
            }

            if (not keep_open)
            {
                has_change |= true;
                *editing = editor.GetText();
                editing  = nullptr;
            }
            ImGui::End();
        }

        ImGui::SetNextItemWidth(-1);
        has_change |= ImGui::InputTextEx(
                label_base,
                "",
                str->data(),
                str->capacity(),
                {},
                ImGuiInputTextFlags_CallbackResize
                        | readonly_flag,
                [](ImGuiInputTextCallbackData* data) -> int {
                    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
                    {
                        auto str = static_cast<std::string*>(data->UserData);
                        str->reserve(data->BufSize);
                        data->Buf = str->data();
                    }

                    return 0;
                },
                str);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            editing = str;
            editor.SetText(*str);
        }

        if (has_change)
        {
            // 1. str을 dynamic buffer 대용으로 사용 중.
            // 2. 버퍼 크기보다 크게 resize 시
            static std::string copy_buf;
            auto len = strlen(str->c_str());

            if (len > str->size())
            {
                copy_buf = str->data() + str->size();
                str->append(copy_buf.begin(), copy_buf.end());
            }
            else
            {
                str->resize(len);
            }
        }
    }
    else if (e->is_number())
    {
        void* ptr;
        float step       = 1.;
        void const *pmin = {}, *pmax = {};
        int data_type = 0;

        if (e->is_number_integer())
        {
            data_type = ImGuiDataType_S64;
            auto data = e->get_ptr<int64_t*>();
            ptr       = data;
            min && (pmin = min->get_ptr<int64_t const*>());
            max && (pmax = max->get_ptr<int64_t const*>());

            step = std::max(0.001, std::abs(*data) / 1000.);
        }
        else if (e->is_number_float())
        {
            data_type = ImGuiDataType_Double;
            auto data = e->get_ptr<double*>();
            ptr       = data;
            min && (pmin = min->get_ptr<double const*>());
            max && (pmax = max->get_ptr<double const*>());

            step = std::max(0.001, std::abs(*data) / 1000);
        }

        ImGui::SetNextItemWidth(-1);

        if (pmin && pmax)
        {
            has_change |= ImGui::SliderScalar(label_base, data_type, ptr, pmin, pmax);
        }
        else
        {
            has_change |= ImGui::DragScalar(label_base, data_type, ptr, step, pmin, pmax);
        }
    }
    else if (e->is_binary())
    {
        ImGui::Text("<binary>");
    }
    else if (e->is_null())
    {
        ImGui::Text("<null>");
    }
    else
    {
        ImGui::Text("-- invalid --");
    }

    return has_change;
}

static std::optional<nlohmann::json> prop_editor(
        bool* dirty,
        uint64_t item_key,
        session_context::config_entity_type const& e)
{
    static uint64_t selected_item = 0;
    bool const is_changed         = selected_item != item_key;
    bool has_change               = false;
    bool apply_changes            = false;

    selected_item = item_key;

    static struct _context_data_t
    {
        nlohmann::json editing;
        std::string combo_value;
        TextEditor edit_raw;
        bool mode_apply_on_change = false;
        bool mode_edit_raw        = false;
    } context;

    if (item_key == 0)
        return {};

    auto force_refresh = ImGui::Button("Refresh##PropEdit");
    ImGui::SameLine();

    if (is_changed)
    {
        *dirty = false;

        if (context.mode_edit_raw)
            context.edit_raw.SetText(e.value.dump(2));
        else
            context.editing = e.value;
    }
    else if (force_refresh)
    {
        *dirty = false;
        if (context.mode_edit_raw)
        {
            context.edit_raw.SetText(e.value.dump(2));
        }
        else
        {
            context.editing = e.value;
        }
    }

    ImGui::BeginDisabled(context.mode_edit_raw);
    if (ImGui::Checkbox("Apply On Change", &context.mode_apply_on_change))
    {
        if (*dirty)
        {
            apply_changes = true;
            *dirty        = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Checkbox("Edit Raw", &context.mode_edit_raw))
    {
        if (context.mode_edit_raw)
        {
            context.edit_raw.SetText(e.value.dump(2));
        }
        else
        {
            context.editing = nlohmann::json::parse(context.edit_raw.GetText(), nullptr, false);

            if (context.editing.is_discarded())
            {
                context.editing = e.value;
            }
        }
    }

    if (context.mode_edit_raw || not context.mode_apply_on_change)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, 0xff356b28);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xff55a142);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xff67ba52);
        apply_changes = ImGui::Button("Apply##Edit Changes", {-1, 0});
        ImGui::PopStyleColor(3);
    }

    ImGui::BeginChild("Editor Child", {}, true);

    if (context.mode_edit_raw)
    {
        context.edit_raw.Render("Property Raw Editor");
    }
    else
    {
        nlohmann::json const *min = {}, *max = {}, *one_of = {};
        auto find_ptr
                = [&](auto key) {
                      auto it = e.metadata.find(key);
                      nlohmann::json const* rval;

                      if (it != e.metadata.end())
                          rval = &*it;
                      else
                          rval = nullptr;

                      return rval;
                  };

        min    = find_ptr("min");
        max    = find_ptr("max");
        one_of = find_ptr("one_of");

        if (one_of && one_of->is_array())
        {
            if (context.combo_value.empty())
            {
                context.combo_value = context.editing.dump();
            }

            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##Edit Combo", context.combo_value.c_str()))
            {
                std::string sample;
                sample.reserve(context.combo_value.capacity());

                ImGui::SetItemDefaultFocus();

                for (auto& elem : *one_of)
                {
                    sample = elem.dump();

                    if (ImGui::Selectable(sample.c_str()))
                    {
                        context.combo_value = sample;
                        context.editing     = nlohmann::json::parse(context.combo_value);
                        context.edit_raw.SetText(context.combo_value);

                        has_change = true;
                    }
                }

                ImGui::EndCombo();
            }
        }
        else
        {
            has_change |= prop_editor_recursive_impl("##EDIT_PROPERTY", &context.editing, min, max);
        }
    }

    ImGui::EndChild();
    *dirty |= has_change;

    if (not context.mode_edit_raw && context.mode_apply_on_change)
    {
        apply_changes |= has_change;
    }

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(257, false))
    {
        apply_changes = true;
    }

    if (apply_changes)
    {
        if (context.mode_edit_raw)
        {
            context.editing = nlohmann::json::parse(context.edit_raw.GetText(), nullptr, false);

            if (context.editing.is_discarded())
            {
                return {};
            }
        }

        *dirty = false;
        return context.editing;
    }

    return {};
}

void session_slot::_draw_category_recursive(
        session_context::config_type const& target)
{
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

            ImGui::PushStyleColor(ImGuiCol_Text, 0xffffffff);
            if (auto it = elem.metadata.find("description");
                it != elem.metadata.end() && it->is_string())
            {
                auto& str = it->get_ref<std::string const&>();
                if (not str.empty())
                {
                    ImGui::TextEx(str.c_str());
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, 0xffaaaaaa);
                    ImGui::TextEx("-- no description --");
                    ImGui::PopStyleColor();
                }
            }

            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, 0xff55ffff);
            ImGui::TextEx(elem.value.dump(2).c_str());

            ImGui::PopStyleColor(2);

            ImGui::EndTooltip();
            ImGui::PopTextWrapPos();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(
                ImGuiCol_Text,
                elem.value.is_number() || elem.value.is_boolean() ? 0xff56bf6f
                : elem.value.is_string()                          ? 0xff4c87c7
                                                                  : 0xffc7794c);

        ImGui::TextEx(elem.value.dump().c_str());
        ImGui::PopStyleColor();

        if (open)
        {
            if (render_modify_view)
                selected_item = 0;
            else
                selected_item = elem.config_key;
        }

        render_modify_view = elem.config_key == selected_item;
        if (not render_modify_view)
            continue;

        static bool is_dirty = false;
        char buf[256];
        snprintf(buf, sizeof buf,
                 "editing [%s]%s###Property Editor",
                 elem.name.c_str(),
                 is_dirty ? "*" : "");
        ImGui::Begin(buf);

        if (auto result = prop_editor(&is_dirty, selected_item, elem))
        {
            _context->configure(target.name, elem.config_key, *result);
        }

        ImGui::End();
    }

    for (auto& sub : target.subcategories)
    {
        if (not ImGui::TreeNodeEx(
                    _key("{}##{}", sub.name.c_str(), (void*)&sub),
                    ImGuiTreeNodeFlags_SpanFullWidth
                            | ImGuiTreeNodeFlags_DefaultOpen))
            continue;

        _draw_category_recursive(sub);

        ImGui::TreePop();
    }
}

session_slot::~session_slot()
{
    _context = {};           // always be first
    _trace_context.reset();  // then next
}

template <typename Ty_, typename RTy_>
static void put(session_slot::data_footprint<Ty_>* a, RTy_&& b)
{
    if constexpr (std::is_integral_v<Ty_>)
    {
        if (a->size() >= 2 && a->end()[-1].value == b && a->end()[-2].value == b)
        {
            a->back().timestamp.reset();
            return;  // skip update when it's identical with previous}
        }
    }

    session_slot::graph_node<Ty_> arg;
    arg.value = b;
    a->push_back(arg);
}

void session_slot::_session_state_update(const session_context::session_state_type& state)
{
    put(&_plots.cpu_total, (state.cpu_usage_total_system + state.cpu_usage_total_user) * 100.);
    put(&_plots.cpu_total_user, state.cpu_usage_total_user * 100.);
    put(&_plots.cpu_total_sys, state.cpu_usage_total_system * 100.);

    put(&_plots.cpu_this, (state.cpu_usage_self_user + state.cpu_usage_self_system) * 100.);
    put(&_plots.cpu_this_user, state.cpu_usage_self_user * 100.);
    put(&_plots.cpu_this_sys, state.cpu_usage_self_system * 100.);

    put(&_plots.mem_rss, state.memory_usage_resident);
    put(&_plots.mem_virt, state.memory_usage_virtual);

    put(&_plots.bw_in, state.bw_in * 8);
    put(&_plots.bw_out, state.bw_out * 8);

    // 사각형 형태를 유지하기 위해 두 개씩 put
    if (not _plots.num_thrd.empty())
        put(&_plots.num_thrd, _plots.num_thrd.back().value);
    else
        put(&_plots.num_thrd, 0);

    put(&_plots.num_thrd, state.num_threads);
}

template <typename Ty_>
static ImPlotPoint getter(void* param, int idx)
{
    auto p   = (typename session_slot::data_footprint<Ty_>*)param;
    auto arg = (p->begin())[idx];

    if (idx == p->size() - 1)
        return ImPlotPoint(0., arg.value);
    else
        return ImPlotPoint(-arg.timestamp.elapsed().count(), arg.value);
}

template <typename Ty_>
static ImPlotPoint getter_0(void* param, int idx)
{
    auto p   = (typename session_slot::data_footprint<Ty_>*)param;
    auto arg = (p->begin())[idx];

    if (idx == p->size() - 1)
        return ImPlotPoint(0., 0.);
    else
        return ImPlotPoint(-arg.timestamp.elapsed().count(), 0.);
}

template <int Type_, typename Range_>
static void DoPlot(Range_&& rng, char const* label)
{
    if (rng.empty())
        return;

    auto size  = rng.size();
    using type = decltype(rng.front().value);

    if constexpr (Type_ == 0 || Type_ == 2)
    {
        ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
        ImPlot::PlotShadedG(label, getter<type>, &rng, getter_0<type>, &rng, size);
        ImPlot::PopStyleVar();
        ImPlot::PlotLineG(label, getter<type>, &rng, size);
    }
    else if constexpr (Type_ == 1)
    {
        ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
        ImPlot::PlotShadedG(label, getter<type>, &rng, getter_0<type>, &rng, size);
        ImPlot::PopStyleVar();

        ImPlot::PlotScatterG(label, getter<type>, &rng, size);
        ImPlot::PlotStairsG(label, getter<type>, &rng, size);
        ImPlot::PlotBarsG(label, getter<type>, &rng, size, 0.);
    }
}

void session_slot::_plot_on_submenu()
{
    auto makeOpenPlotContext =
            [=](char const* name) {
                return [mM           = std::array<double, 2>{},
                        name         = std::string{name},
                        fitTimeAuto  = true,
                        fitValueAuto = true]  //
                       () mutable -> bool {
                           ImGui::AlignTextToFramePadding();
                           auto open = ImGui::TreeNodeEx(
                                   perfkit::futils::usprintf("%s##Header", name.c_str()),
                                   ImGuiTreeNodeFlags_DefaultOpen
                                           | ImGuiTreeNodeFlags_SpanFullWidth);

                           if (open)
                           {
                               ImGui::Checkbox("Fit Time", &fitTimeAuto);
                               ImGui::SameLine();
                               ImGui::Checkbox("Fit Value", &fitValueAuto);

                               if (fitTimeAuto)
                                   ImPlot::SetNextAxisToFit(ImAxis_X1);
                               if (fitValueAuto)
                                   ImPlot::SetNextAxisToFit(ImAxis_Y1);

                               ImPlot::PushStyleColor(ImPlotCol_FrameBg, 0x00);
                               if (ImPlot::BeginPlot(name.c_str(), {-1, 0}, ImPlotFlags_NoTitle))
                               {
                                   mM[0] = std::max(0., mM[0]);
                                   mM[1] = std::min(0., mM[1]);

                                   ImPlot::SetupAxisLinks(ImAxis_Y1, &mM[0], nullptr);
                                   ImPlot::SetupAxisLinks(ImAxis_X1, nullptr, &mM[1]);
                                   ImPlot::SetupAxisFormat(ImAxis_X1, "%.1fs");
                               }
                               else
                               {
                                   ImPlot::PopStyleColor();
                                   ImGui::TreePop();
                                   open = false;
                               }
                           }

                           return open;
                       };
            };

    auto ClosePlot =
            [] {
                ImPlot::EndPlot();
                ImPlot::PopStyleColor();
                ImGui::TreePop();
            };

    auto BeginAxisX =
            [](auto&& queue) {
                if (queue.empty())
                    return 0.;

                double time = queue.front().timestamp.elapsed().count();
                return -ceil(time / 15.) * 15.;
            };

    ImPlotFormatter formatCpuUsage =
            [](double value, char* buff, int size, void*) {
                if (value > 10.)
                {
                    snprintf(buff, size, "%.0lf%%", value);
                }
                else if (value > 1.)
                {
                    snprintf(buff, size, "%.1lf%%", value);
                }
                else
                {
                    snprintf(buff, size, "%g%%", value);
                }
            };

    ImPlotFormatter formatMemoryUsage =
            [](double value, char* buff, int size, void* user) {
                if (value < 0)
                {
                    snprintf(buff, size, "--");
                    return;
                }

                int64_t lvalue      = value;
                char const suffix[] = " KMGTPE";
                int64_t base        = 1;
                int order           = 0;

                for (; base * 1024 < lvalue
                       && order < std::size(suffix) - 1;
                     base *= 1024, ++order) {}

                auto is_bandwidth = (intptr_t)user == 2;

                if (order > 0)
                {
                    int dec  = lvalue * 10 / base;
                    int frac = dec % 10;
                    dec /= 10;

                    if (is_bandwidth)
                        snprintf(buff, size, "%d.%d %cbps", dec, frac, suffix[order]);
                    else
                        snprintf(buff, size, "%d.%d %ciB", dec, frac, suffix[order]);
                }
                else
                {
                    if (is_bandwidth)
                        snprintf(buff, size, "%lld bps", lvalue);
                    else
                        snprintf(buff, size, "%lld B", lvalue);
                }
            };

    ImPlot::SetNextAxisLimits(ImAxis_X1, -30., 0.);

    if (static auto fn = makeOpenPlotContext("Cpu Usage: Total"); fn())
    {  // Cpu Meter
        ImPlot::SetupAxisFormat(ImAxis_Y1, formatCpuUsage);

        DoPlot<0>(_plots.cpu_total, "User+System");
        DoPlot<0>(_plots.cpu_total_user, "User");
        DoPlot<0>(_plots.cpu_total_sys, "System");

        ClosePlot();
    }

    if (static auto fn = makeOpenPlotContext("CPU Usage: This Process"); fn())
    {  // Cpu Meter
        ImPlot::SetupAxisFormat(ImAxis_Y1, formatCpuUsage);

        DoPlot<0>(_plots.cpu_this, "User+System");
        DoPlot<0>(_plots.cpu_this_user, "User");
        DoPlot<0>(_plots.cpu_this_sys, "System");

        ClosePlot();
    }

    if (static auto fn = makeOpenPlotContext("Number of Threads"); fn())
    {
        DoPlot<1>(_plots.num_thrd, "Thread Count");

        ClosePlot();
    }

    if (static auto fn = makeOpenPlotContext("Memory Usage"); fn())
    {
        ImPlot::SetupAxisFormat(ImAxis_Y1, formatMemoryUsage, (void*)1);

        DoPlot<2>(_plots.mem_virt, "Virtual");
        DoPlot<2>(_plots.mem_rss, "Resident");

        ClosePlot();
    }

    if (static auto fn = makeOpenPlotContext("Perfkit IO BW Usgae"); fn())
    {
        ImPlot::SetupAxisFormat(ImAxis_Y1, formatMemoryUsage, (void*)2);

        DoPlot<0>(_plots.bw_out, "Outgoing");
        DoPlot<0>(_plots.bw_in, "Incoming");

        ClosePlot();
    }
}
