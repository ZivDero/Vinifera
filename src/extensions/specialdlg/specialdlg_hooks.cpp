/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains hooks for the special escape dialog.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "specialdlg_hooks.h"

#include "ccini.h"
#include "command.h"
#include "debughandler.h"
#include "event.h"
#include "hooker.h"
#include "house.h"
#include "loadoptions.h"
#include "msgloop.h"
#include "mouse.h"
#include "options.h"
#include "queue.h"
#include "restate.h"
#include "scenario.h"
#include "sdl_functions.h"
#include "session.h"
#include "syringe.h"
#include "techno.h"
#include "theme.h"
#include "tibsun_functions.h"
#include "tibsun_globals.h"
#include "vinifera_rmlui.h"
#include "voc.h"
#include "vox.h"
#include "wwkeyboard.h"

#include "lib/rawfile.h"
#include "lib/tooltip.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Traits.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    enum class ModalResult {
        Pending,
        Cancel,
        Resume,
        Restate,
        Load,
        Save,
        Delete,
        Settings,
        Abort,
        Surrender,
        Sound,
        Keyboard,
        Ok,
        Play,
        Stop,
        ResetHotkeys,
        AssignHotkey,
    };

    enum class ModalKind {
        Main,
        Settings,
        Sound,
        Keyboard,
        Abort,
        Surrender,
        ResetHotkeysConfirm,
    };

    struct HotkeySnapshotEntry
    {
        KeyNumType Key;
        CommandClass* Command;
    };

    ModalResult CurrentResult = ModalResult::Pending;
    ModalKind CurrentKind = ModalKind::Main;
    bool SpecialDialogFlag = true;
    KeyNumType PendingHotkey = KN_NONE;
    int SelectedHotkeyCommand = 0;
    int SelectedTheme = 0;

    constexpr int DIALOG_SCALE = 2;
    constexpr int MAX_SPEED_SETTING = 7;
    constexpr int MAX_SCROLL_SETTING = 7;
    constexpr int MAX_DETAIL_SETTING = 3;
    constexpr int MAX_DIFFICULTY_SETTING = 3;

    // Current retail WOL module starts at 0x006867B0; DoFindPage is the first public WOL dialog helper.
    using DoFindPageFunc = void(__fastcall*)();
    DoFindPageFunc DoFindPage = reinterpret_cast<DoFindPageFunc>(0x00687660);

    std::string Escape_Rml(const char* text)
    {
        std::string output;
        if (text == nullptr) {
            return output;
        }

        for (const char* ptr = text; *ptr != '\0'; ++ptr) {
            switch (*ptr) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            default: output += *ptr; break;
            }
        }
        return output;
    }

    std::string Escape_Rml(const std::string& text)
    {
        return Escape_Rml(text.c_str());
    }

    int Dialog_Left(int width)
    {
        return std::max(0, (VideoWidth - width * DIALOG_SCALE) / 2);
    }

    int Dialog_Top(int height)
    {
        return std::max(0, (VideoHeight - height * DIALOG_SCALE) / 2);
    }

    std::string Px(int value)
    {
        return std::to_string(value * DIALOG_SCALE) + "px";
    }

    std::string Rect_Style(int x, int y, int width, int height)
    {
        std::ostringstream out;
        out << "left:" << Px(x) << ";top:" << Px(y) << ";width:" << Px(width) << ";height:" << Px(height) << ";";
        return out.str();
    }

    std::string Document_Begin(int width, int height, const char* title)
    {
        std::ostringstream out;
        out
            << "<rml><head><title>" << Escape_Rml(title) << "</title><style>"
            << "body{margin:0;font-family:\"Segoe UI\",\"Arial\";font-size:13px;color:#ecf2f4;}"
            << "#shade{position:absolute;left:0;top:0;width:100%;height:100%;background-color:rgba(0,0,0,0.42);}"
            << ".dialog{position:absolute;left:" << Dialog_Left(width) << "px;top:" << Dialog_Top(height) << "px;"
            << "width:" << Px(width) << ";height:" << Px(height) << ";background-color:#1f2b32;border:2px #80919a;"
            << "padding:0;}"
            << ".title{position:absolute;text-align:center;font-weight:bold;color:#ffffff;}"
            << "button{position:absolute;background-color:#d6dde0;border:1px #111;color:#101820;font-size:13px;}"
            << "button:hover{background-color:#eef5f7;}"
            << "button:disabled{background-color:#586166;color:#9aa2a7;}"
            << ".label{position:absolute;color:#ecf2f4;}"
            << ".right{text-align:right;}"
            << ".center{text-align:center;}"
            << ".box{position:absolute;border:1px #8ca0aa;color:#ecf2f4;}"
            << "input,select{position:absolute;background-color:#eef5f7;color:#101820;border:1px #101820;font-size:13px;}"
            << "input[type=checkbox]{width:14px;height:14px;}"
            << ".checklabel{position:absolute;color:#ecf2f4;}"
            << ".muted{color:#aeb9bf;}"
            << "</style></head><body><div id=\"shade\"></div><div class=\"dialog\">";
        return out.str();
    }

    std::string Document_End()
    {
        return "</div></body></rml>";
    }

    std::string Button(const char* id, const char* text, int x, int y, int width, int height, bool enabled = true)
    {
        std::ostringstream out;
        out << "<button id=\"" << id << "\" style=\"" << Rect_Style(x, y, width, height) << "\"";
        if (!enabled) {
            out << " disabled=\"disabled\"";
        }
        out << ">" << Escape_Rml(text) << "</button>";
        return out.str();
    }

    std::string Label(const char* text, int x, int y, int width, int height, const char* extra_class = "")
    {
        std::ostringstream out;
        out << "<div class=\"label " << extra_class << "\" style=\"" << Rect_Style(x, y, width, height) << "\">" << Escape_Rml(text) << "</div>";
        return out.str();
    }

    std::string Slider(const char* id, int x, int y, int width, int height, int min, int max, int value)
    {
        std::ostringstream out;
        out << "<input type=\"range\" id=\"" << id << "\" min=\"" << min << "\" max=\"" << max << "\" value=\"" << value
            << "\" style=\"" << Rect_Style(x, y, width, height) << "\" />";
        return out.str();
    }

    std::string Checkbox(const char* id, const char* text, int x, int y, int width, int height, bool checked)
    {
        std::ostringstream out;
        out << "<input type=\"checkbox\" id=\"" << id << "\" style=\"" << Rect_Style(x, y, 8, height) << "\"";
        if (checked) {
            out << " checked=\"checked\"";
        }
        out << " />"
            << "<label for=\"" << id << "\" class=\"checklabel\" style=\"" << Rect_Style(x + 11, y, width - 11, height) << "\">"
            << Escape_Rml(text) << "</label>";
        return out.str();
    }

    Rml::Element* Element_From_Event(Rml::Event& event)
    {
        Rml::Element* element = event.GetTargetElement();
        while (element != nullptr && element->GetId().empty()) {
            element = element->GetParentNode();
        }
        return element;
    }

    Rml::Element* Find_Element(const char* id)
    {
        Rml::Context* context = ViniferaRmlUi::Get_Context();
        if (context == nullptr) {
            return nullptr;
        }

        Rml::ElementDocument* document = context->GetDocument(0);
        return document != nullptr ? document->GetElementById(id) : nullptr;
    }

    int Control_Int(const char* id, int fallback = 0)
    {
        Rml::Element* element = Find_Element(id);
        Rml::ElementFormControl* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);
        return control != nullptr ? std::atoi(control->GetValue().c_str()) : fallback;
    }

    bool Checkbox_Value(const char* id, bool fallback = false)
    {
        Rml::Element* element = Find_Element(id);
        if (element == nullptr) {
            return fallback;
        }

        const Rml::Variant* checked = element->GetAttribute("checked");
        return checked != nullptr ? checked->Get<bool>(fallback) : fallback;
    }

    void Set_Text(const char* id, const std::string& text)
    {
        Rml::Element* element = Find_Element(id);
        if (element != nullptr) {
            element->SetInnerRML(Escape_Rml(text));
        }
    }

    const char* Speed_Label(int pos)
    {
        static const char* labels[] = { "Slowest", "Slower", "Slow", "Medium", "Fast", "Faster", "Fastest" };
        return labels[std::clamp(pos, 0, 6)];
    }

    const char* Detail_Label(int pos)
    {
        static const char* labels[] = { "Low", "Medium", "High" };
        return labels[std::clamp(pos, 0, 2)];
    }

    const char* Difficulty_Label(int pos)
    {
        static const char* labels[] = { "Easy", "Normal", "Hard" };
        return labels[std::clamp(pos, 0, 2)];
    }

    const char* Connection_Label(int pos)
    {
        static const char* labels[] = { "Worst", "Poor", "Good", "Best" };
        return labels[std::clamp(pos, 0, 3)];
    }

    bool Save_Files_Present()
    {
        LoadOptionsClass options;
        return options.Read_Save_Files();
    }

    void Apply_Internet_Main_Sliders()
    {
        if (Session.Type != GAME_INTERNET || PlayerPtr == nullptr) {
            return;
        }

        const int fudge = 3 - Control_Int("latency", 3 - Session.LatencyFudge);
        if (fudge != Session.LatencyFudge) {
            OutList.Add(EventClass(PlayerPtr->HeapID, EVENT_LATENCYFUDGE, fudge));
        }

        const int speed = (MAX_SPEED_SETTING - 1) - Control_Int("game_speed", (MAX_SPEED_SETTING - 1) - Options.GameSpeed);
        if (static_cast<int>(Options.GameSpeed) != speed) {
            OutList.Add(EventClass(PlayerPtr->HeapID, EVENT_GAMESPEED, speed));
        }
    }

    void Apply_Settings()
    {
        const int game_speed = (MAX_SPEED_SETTING - 1) - Control_Int("game_speed", (MAX_SPEED_SETTING - 1) - Options.GameSpeed);
        if (static_cast<int>(Options.GameSpeed) != game_speed) {
            if (GameActive && Session.Type != GAME_NORMAL && Session.Type != GAME_SKIRMISH && PlayerPtr != nullptr) {
                OutList.Add(EventClass(PlayerPtr->HeapID, EVENT_GAMESPEED, game_speed));
            } else {
                Options.GameSpeed = game_speed;
            }
        }

        Options.ScrollRate = (MAX_SCROLL_SETTING - 1) - Control_Int("scroll_speed", (MAX_SCROLL_SETTING - 1) - Options.ScrollRate);

        const int detail_level = Control_Int("detail_level", Options.DetailLevel);
        if (Options.DetailLevel != detail_level) {
            Options.DetailLevel = detail_level;
            Map.Flag_To_Redraw(GS_REDRAW_ALL);
        }

        Options.SidebarCameoText = Checkbox_Value("cameo_text", Options.SidebarCameoText);
        Options.ActionLines = Checkbox_Value("action_lines", Options.ActionLines);
        TechnoClass::Set_Action_Lines(Options.ActionLines);

        Options.ToolTips = Checkbox_Value("tooltips", Options.ToolTips);
        if (ToolTips != nullptr && TacticalActive) {
            ToolTips->Set_Active(Options.ToolTips);
        }

        Options.ScrollMethod = Checkbox_Value("scroll_coasting", Options.ScrollMethod == 0) ? 0 : 1;

        if (!GameActive) {
            Options.Difficulty = static_cast<DiffType>(Control_Int("difficulty", Options.Difficulty));
        }

        Options.Set();
        Options.Save_Settings();
    }

    void Apply_Sound_Slider(const char* id, bool feedback)
    {
        const double value = Control_Int(id, 0) / 10.0;
        if (std::strcmp(id, "music_volume") == 0) {
            Options.Set_Score_Volume(value, feedback);
        } else if (std::strcmp(id, "sound_volume") == 0) {
            Options.Set_Sound_Volume(value, feedback);
        } else if (std::strcmp(id, "voice_volume") == 0) {
            Options.Set_Voice_Volume(value, feedback);
        }
    }

    void Apply_Sound_Checks(const char* id)
    {
        if (std::strcmp(id, "shuffle") == 0) {
            const bool shuffle = Checkbox_Value("shuffle", Options.IsScoreShuffle);
            Options.Set_Shuffle(shuffle);
            if (shuffle) {
                Options.Set_Repeat(false);
                if (Rml::Element* repeat = Find_Element("repeat")) {
                    repeat->RemoveAttribute("checked");
                }
            }
        } else if (std::strcmp(id, "repeat") == 0) {
            const bool repeat = Checkbox_Value("repeat", Options.IsScoreRepeat);
            Options.Set_Repeat(repeat);
            if (repeat) {
                Options.Set_Shuffle(false);
                if (Rml::Element* shuffle = Find_Element("shuffle")) {
                    shuffle->RemoveAttribute("checked");
                }
            }
        }
    }

    std::string Key_Name(KeyNumType key)
    {
        const int base_key = static_cast<int>(key) & 0xFF;
        std::string prefix;

        if (static_cast<int>(key) & KN_SHIFT_BIT) prefix += "Shift+";
        if (static_cast<int>(key) & KN_CTRL_BIT) prefix += "Ctrl+";
        if (static_cast<int>(key) & KN_ALT_BIT) prefix += "Alt+";

        if (base_key == 0) return "";
        if (base_key >= 'A' && base_key <= 'Z') return prefix + static_cast<char>(base_key);
        if (base_key >= '0' && base_key <= '9') return prefix + static_cast<char>(base_key);
        if (base_key >= VK_F1 && base_key <= VK_F12) return prefix + "F" + std::to_string(base_key - VK_F1 + 1);

        switch (base_key) {
        case VK_BACK: return prefix + "Backspace";
        case VK_TAB: return prefix + "Tab";
        case VK_RETURN: return prefix + "Enter";
        case VK_ESCAPE: return prefix + "Esc";
        case VK_SPACE: return prefix + "Space";
        case VK_PRIOR: return prefix + "Page Up";
        case VK_NEXT: return prefix + "Page Down";
        case VK_END: return prefix + "End";
        case VK_HOME: return prefix + "Home";
        case VK_LEFT: return prefix + "Left";
        case VK_UP: return prefix + "Up";
        case VK_RIGHT: return prefix + "Right";
        case VK_DOWN: return prefix + "Down";
        case VK_INSERT: return prefix + "Insert";
        case VK_DELETE: return prefix + "Delete";
        default: return prefix + "Key " + std::to_string(base_key);
        }
    }

    KeyNumType Rml_Key_To_KeyNum(int key_identifier)
    {
        int base_key = 0;

        if (key_identifier >= Rml::Input::KI_A && key_identifier <= Rml::Input::KI_Z) {
            base_key = 'A' + (key_identifier - Rml::Input::KI_A);
        } else if (key_identifier >= Rml::Input::KI_0 && key_identifier <= Rml::Input::KI_9) {
            base_key = '0' + (key_identifier - Rml::Input::KI_0);
        } else if (key_identifier >= Rml::Input::KI_F1 && key_identifier <= Rml::Input::KI_F12) {
            base_key = VK_F1 + (key_identifier - Rml::Input::KI_F1);
        } else {
            switch (key_identifier) {
            case Rml::Input::KI_BACK: base_key = VK_BACK; break;
            case Rml::Input::KI_TAB: base_key = VK_TAB; break;
            case Rml::Input::KI_RETURN: base_key = VK_RETURN; break;
            case Rml::Input::KI_ESCAPE: base_key = VK_ESCAPE; break;
            case Rml::Input::KI_SPACE: base_key = VK_SPACE; break;
            case Rml::Input::KI_PRIOR: base_key = VK_PRIOR; break;
            case Rml::Input::KI_NEXT: base_key = VK_NEXT; break;
            case Rml::Input::KI_END: base_key = VK_END; break;
            case Rml::Input::KI_HOME: base_key = VK_HOME; break;
            case Rml::Input::KI_LEFT: base_key = VK_LEFT; break;
            case Rml::Input::KI_UP: base_key = VK_UP; break;
            case Rml::Input::KI_RIGHT: base_key = VK_RIGHT; break;
            case Rml::Input::KI_DOWN: base_key = VK_DOWN; break;
            case Rml::Input::KI_INSERT: base_key = VK_INSERT; break;
            case Rml::Input::KI_DELETE: base_key = VK_DELETE; break;
            default: break;
            }
        }

        if (base_key == 0) {
            return KN_NONE;
        }

        int key = base_key;
        if (HIWORD(GetKeyState(VK_SHIFT)) & 1) key |= KN_SHIFT_BIT;
        if (HIWORD(GetKeyState(VK_CONTROL)) & 1) key |= KN_CTRL_BIT;
        if (HIWORD(GetKeyState(VK_MENU)) & 1) key |= KN_ALT_BIT;

        return static_cast<KeyNumType>(key);
    }

    std::vector<CommandClass*> Sorted_Commands()
    {
        std::vector<CommandClass*> commands;
        for (int index = 0; index < Commands.Count(); ++index) {
            if (Commands[index] != nullptr) {
                commands.push_back(Commands[index]);
            }
        }

        std::sort(commands.begin(), commands.end(), [](const CommandClass* lhs, const CommandClass* rhs) {
            const int cat = stricmp(lhs->Get_Category(), rhs->Get_Category());
            if (cat != 0) return cat < 0;
            return stricmp(lhs->Get_UI_Name(), rhs->Get_UI_Name()) < 0;
        });

        return commands;
    }

    CommandClass* Command_By_Sorted_Index(int index)
    {
        const std::vector<CommandClass*> commands = Sorted_Commands();
        return index >= 0 && index < static_cast<int>(commands.size()) ? commands[index] : nullptr;
    }

    KeyNumType Key_For_Command(CommandClass* command)
    {
        for (int index = 0; index < HotkeyIndex.Count(); ++index) {
            if (HotkeyIndex.Fetch_By_Position(index) == command) {
                return HotkeyIndex.Fetch_ID_By_Position(index);
            }
        }
        return KN_NONE;
    }

    std::string Command_Name_For_Key(KeyNumType key)
    {
        CommandClass* command = HotkeyIndex[key];
        return command != nullptr ? command->Get_UI_Name() : "";
    }

    void Save_Hotkeys()
    {
        CCINIClass ini;
        ini.Clear();

        for (int index = 0; index < HotkeyIndex.Count(); ++index) {
            CommandClass* command = HotkeyIndex.Fetch_By_Position(index);
            if (command != nullptr) {
                ini.Put_Int("Hotkey", command->Get_Name(), HotkeyIndex.Fetch_ID_By_Position(index));
            }
        }

        RawFileClass file("Keyboard.ini");
        ini.Save(file, false);
    }

    void Assign_Hotkey()
    {
        CommandClass* command = Command_By_Sorted_Index(SelectedHotkeyCommand);
        if (command == nullptr) {
            return;
        }

        for (int index = 0; index < HotkeyIndex.Count(); ++index) {
            if (HotkeyIndex.Fetch_By_Position(index) == command) {
                HotkeyIndex.Remove_Index(HotkeyIndex.Fetch_ID_By_Position(index));
                break;
            }
        }

        if (PendingHotkey != KN_NONE) {
            HotkeyIndex.Remove_Index(PendingHotkey);
            HotkeyIndex.Add_Index(PendingHotkey, command);
        }
    }

    void Update_Hotkey_Details()
    {
        CommandClass* command = Command_By_Sorted_Index(SelectedHotkeyCommand);
        if (command == nullptr) {
            return;
        }

        Set_Text("description", command->Get_Description());
        Set_Text("current_shortcut", Key_Name(Key_For_Command(command)));
        Set_Text("assigned_to", PendingHotkey != KN_NONE ? Command_Name_For_Key(PendingHotkey) : "");
        Set_Text("new_shortcut", PendingHotkey != KN_NONE ? Key_Name(PendingHotkey) : "Press a key");
    }

    void Play_Selected_Theme()
    {
        Rml::Element* element = Find_Element("theme_list");
        Rml::ElementFormControlSelect* select = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(element);
        if (select == nullptr) {
            return;
        }

        const int value = std::atoi(select->GetValue().c_str());
        Theme.Stop();
        Theme.Queue_Song(static_cast<ThemeType>(value));
    }

    class SpecialDialogEventListener : public Rml::EventListener
    {
    public:
        void ProcessEvent(Rml::Event& event) override
        {
            Rml::Element* element = Element_From_Event(event);
            const std::string id = element != nullptr ? element->GetId() : "";

            if (event == "keydown" && CurrentKind == ModalKind::Keyboard) {
                PendingHotkey = Rml_Key_To_KeyNum(event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN));
                Update_Hotkey_Details();
                event.StopPropagation();
                return;
            }

            if (event == "change") {
                if (id == "game_speed") Set_Text("game_speed_label", Speed_Label(Control_Int("game_speed")));
                if (id == "scroll_speed") Set_Text("scroll_speed_label", Speed_Label(Control_Int("scroll_speed")));
                if (id == "detail_level") Set_Text("detail_label", Detail_Label(Control_Int("detail_level")));
                if (id == "difficulty") Set_Text("difficulty_label", Difficulty_Label(Control_Int("difficulty")));
                if (id == "latency") Set_Text("latency_label", Connection_Label(Control_Int("latency")));
                if (id == "music_volume" || id == "sound_volume" || id == "voice_volume") Apply_Sound_Slider(id.c_str(), true);
                if (id == "shuffle" || id == "repeat") Apply_Sound_Checks(id.c_str());
                if (id == "command_list") {
                    SelectedHotkeyCommand = Control_Int("command_list", SelectedHotkeyCommand);
                    PendingHotkey = KN_NONE;
                    Update_Hotkey_Details();
                }
                return;
            }

            if (!(event == "click")) {
                return;
            }

            if (id == "resume") CurrentResult = ModalResult::Resume;
            else if (id == "restate") CurrentResult = ModalResult::Restate;
            else if (id == "load") CurrentResult = ModalResult::Load;
            else if (id == "save") CurrentResult = ModalResult::Save;
            else if (id == "delete") CurrentResult = ModalResult::Delete;
            else if (id == "settings") CurrentResult = ModalResult::Settings;
            else if (id == "abort") CurrentResult = ModalResult::Abort;
            else if (id == "surrender") CurrentResult = ModalResult::Surrender;
            else if (id == "sound") CurrentResult = ModalResult::Sound;
            else if (id == "keyboard") CurrentResult = ModalResult::Keyboard;
            else if (id == "ok") CurrentResult = ModalResult::Ok;
            else if (id == "cancel") CurrentResult = ModalResult::Cancel;
            else if (id == "play") Play_Selected_Theme();
            else if (id == "stop") Theme.Queue_Song(THEME_QUIET);
            else if (id == "assign") CurrentResult = ModalResult::AssignHotkey;
            else if (id == "reset") CurrentResult = ModalResult::ResetHotkeys;
        }
    };

    SpecialDialogEventListener Listener;

    std::string Build_Main_Menu()
    {
        if (Session.Type == GAME_NORMAL || Session.Type == GAME_SKIRMISH) {
            const bool saves_present = Save_Files_Present();
            std::ostringstream out;
            out << Document_Begin(209, 140, "Options")
                << Button("settings", "Game Controls", 47, 12, 115, 14)
                << Button("restate", "Restate Briefing", 47, 29, 115, 14, Session.Type != GAME_SKIRMISH)
                << Button("load", "Load Game", 47, 46, 115, 14, saves_present)
                << Button("save", "Save Game", 47, 63, 115, 14)
                << Button("delete", "Delete Game", 47, 80, 115, 14, saves_present)
                << Button("abort", "Abort Mission", 47, 97, 115, 14)
                << Button("resume", "Resume Mission", 47, 114, 115, 14)
                << Document_End();
            return out.str();
        }

        if (Session.Type == GAME_INTERNET) {
            std::ostringstream out;
            out << Document_Begin(340, 168, "Options")
                << Button("settings", "Game Controls", 120, 12, 99, 14)
                << Button("abort", "Abort Mission", 120, 30, 99, 14)
                << Button("resume", "Resume Mission", 120, 48, 99, 14)
                << "<div class=\"box\" style=\"" << Rect_Style(28, 75, 283, 68) << "\"><span style=\"position:absolute;left:10px;top:-9px;background-color:#1f2b32;\">Internet Game Controls</span></div>"
                << Label("Connection", 39, 93, 58, 13)
                << Slider("latency", 95, 93, 148, 13, 0, 3, 3 - Session.LatencyFudge)
                << "<div id=\"latency_label\" class=\"label right\" style=\"" << Rect_Style(247, 93, 45, 13) << "\">" << Connection_Label(3 - Session.LatencyFudge) << "</div>"
                << Label("Game Speed", 39, 115, 58, 13)
                << Slider("game_speed", 95, 115, 148, 13, 0, 6, 6 - Options.GameSpeed)
                << "<div id=\"game_speed_label\" class=\"label right\" style=\"" << Rect_Style(247, 115, 45, 13) << "\">" << Speed_Label(6 - Options.GameSpeed) << "</div>"
                << Document_End();
            return out.str();
        }

        std::ostringstream out;
        out << Document_Begin(209, 75, "Options")
            << Button("settings", "Game Controls", 55, 12, 99, 14)
            << Button("abort", "Abort Mission", 55, 30, 99, 14)
            << Button("resume", "Resume Mission", 55, 48, 99, 14)
            << Document_End();
        return out.str();
    }

    std::string Build_Settings()
    {
        const bool wol = GameActive && Session.Type == GAME_INTERNET;
        const bool sp_layout = !GameActive;
        const int width = wol ? 294 : 292;
        const int height = wol ? 126 : sp_layout ? 163 : 157;

        std::ostringstream out;
        out << Document_Begin(width, height, "Game Controls");

        int y = 12;
        if (!wol) {
            out << Label("Game Speed", sp_layout ? 22 : 22, y, sp_layout ? 58 : 63, 13, sp_layout ? "" : "right")
                << Slider("game_speed", sp_layout ? 80 : 90, y, sp_layout ? 148 : 128, 13, 0, 6, 6 - Options.GameSpeed)
                << "<div id=\"game_speed_label\" class=\"label right\" style=\"" << Rect_Style(sp_layout ? 229 : 224, y, sp_layout ? 45 : 50, 13) << "\">" << Speed_Label(6 - Options.GameSpeed) << "</div>";
            y += sp_layout ? 22 : 31;
        }

        out << Label("Scroll Rate", 22, y, sp_layout ? 58 : 63, 13, sp_layout ? "" : "right")
            << Slider("scroll_speed", sp_layout ? 80 : 90, y, sp_layout ? 148 : 128, 13, 0, 6, 6 - Options.ScrollRate)
            << "<div id=\"scroll_speed_label\" class=\"label right\" style=\"" << Rect_Style(wol ? 226 : sp_layout ? 229 : 224, y, sp_layout ? 45 : 50, 13) << "\">" << Speed_Label(6 - Options.ScrollRate) << "</div>";
        y += sp_layout ? 22 : 31;

        out << Label("Visual Details", 22, y, sp_layout ? 58 : 63, 13, sp_layout ? "" : "right")
            << Slider("detail_level", sp_layout ? 80 : 90, y, sp_layout ? 148 : 128, 13, 0, 2, Options.DetailLevel)
            << "<div id=\"detail_label\" class=\"label right\" style=\"" << Rect_Style(wol ? 226 : sp_layout ? 229 : 224, y, sp_layout ? 45 : 50, 13) << "\">" << Detail_Label(Options.DetailLevel) << "</div>";

        if (sp_layout) {
            out << Label("Difficulty", 22, 78, 58, 13)
                << Slider("difficulty", 80, 78, 148, 13, 0, 2, Options.Difficulty)
                << "<div id=\"difficulty_label\" class=\"label right\" style=\"" << Rect_Style(229, 78, 45, 13) << "\">" << Difficulty_Label(Options.Difficulty) << "</div>"
                << Checkbox("cameo_text", "Cameo Text", 22, 103, 124, 10, Options.SidebarCameoText)
                << Checkbox("action_lines", "Target Lines", 22, 119, 124, 10, Options.ActionLines)
                << Checkbox("tooltips", "Tooltips", 146, 103, 128, 10, Options.ToolTips)
                << Checkbox("scroll_coasting", "Scroll Coasting", 146, 119, 128, 10, Options.ScrollMethod == 0)
                << Button("ok", "Main Menu", 81, 137, 130, 14);
        } else {
            const int check_y = wol ? 63 : 94;
            out << Checkbox("cameo_text", "Sidebar Text", 22, check_y, 119, 10, Options.SidebarCameoText)
                << Checkbox("action_lines", "Target Lines", 22, check_y + 18, 119, 10, Options.ActionLines)
                << Checkbox("tooltips", "Tooltips", 147, check_y, 127, 10, Options.ToolTips)
                << Checkbox("scroll_coasting", "Scroll Coasting", 147, check_y + 18, 127, 10, Options.ScrollMethod == 0)
                << Button("sound", "Sound", 22, wol ? 100 : 131, 77, 14)
                << Button("keyboard", "Keyboard", 109, wol ? 100 : 131, 77, 14)
                << Button("ok", "Options Menu", 196, wol ? 100 : 131, 77, 14);
        }

        out << Document_End();
        return out.str();
    }

    std::string Build_Sound()
    {
        const bool lite = !GameActive;
        std::ostringstream out;
        out << Document_Begin(294, lite ? 112 : 215, "Sound Controls")
            << Label("Music Volume:", 22, lite ? 17 : 12, 70, 15, "right")
            << Slider("music_volume", lite ? 99 : 97, lite ? 17 : 12, lite ? 173 : 175, 15, 0, 10, static_cast<int>(Options.ScoreVolume * 10.0f + 0.5f))
            << Label("Sound Volume:", 22, lite ? 40 : 34, 70, 15, "right")
            << Slider("sound_volume", lite ? 99 : 97, lite ? 40 : 34, lite ? 173 : 175, 15, 0, 10, static_cast<int>(Options.SoundVolume * 10.0f + 0.5f))
            << Label("Voice Volume:", 22, lite ? 62 : 56, 70, 15, "right")
            << Slider("voice_volume", lite ? 99 : 97, lite ? 62 : 56, lite ? 173 : 175, 15, 0, 10, static_cast<int>(Options.VoiceVolume * 10.0f + 0.5f));

        if (!lite) {
            out << Button("play", "Play", 22, 86, 70, 14)
                << Button("stop", "Stop", 22, 113, 70, 14)
                << Checkbox("shuffle", "Shuffle", 22, 140, 70, 14, Options.IsScoreShuffle)
                << Checkbox("repeat", "Repeat", 22, 167, 70, 14, Options.IsScoreRepeat)
                << "<select id=\"theme_list\" size=\"8\" style=\"" << Rect_Style(97, 82, 175, 99) << "\">";

            int visible_num = 1;
            for (ThemeType theme = THEME_FIRST; theme < Theme.Max_Themes(); theme = static_cast<ThemeType>(theme + 1)) {
                if (!Theme.Is_Allowed(theme)) {
                    continue;
                }

                char buffer[160];
                const int length = Theme.Track_Length(theme);
                std::snprintf(buffer, sizeof(buffer), "%02d - %s [%d:%02d]", visible_num++, Theme.Full_Name(theme), length / 60, length % 60);
                out << "<option value=\"" << static_cast<int>(theme) << "\"";
                if (Theme.What_Is_Playing() == theme) {
                    out << " selected=\"selected\"";
                }
                out << ">" << Escape_Rml(buffer) << "</option>";
            }
            out << "</select>" << Button("ok", "OK", 210, 189, 62, 14);
        } else {
            out << Button("ok", "OK", 115, 86, 62, 14);
        }

        out << Document_End();
        return out.str();
    }

    std::string Build_Keyboard()
    {
        const std::vector<CommandClass*> commands = Sorted_Commands();
        std::ostringstream out;
        out << Document_Begin(336, 208, "Customize Keyboard")
            << "<div class=\"title\" style=\"" << Rect_Style(22, 12, 292, 11) << "\">Customize Keyboard</div>"
            << Label("Commands:", 168, 27, 146, 8)
            << "<select id=\"command_list\" size=\"11\" style=\"" << Rect_Style(168, 41, 146, 104) << "\">";

        for (int index = 0; index < static_cast<int>(commands.size()); ++index) {
            out << "<option value=\"" << index << "\"";
            if (index == SelectedHotkeyCommand) {
                out << " selected=\"selected\"";
            }
            out << ">" << Escape_Rml(commands[index]->Get_Category()) << ": " << Escape_Rml(commands[index]->Get_UI_Name()) << "</option>";
        }

        CommandClass* command = Command_By_Sorted_Index(SelectedHotkeyCommand);
        const KeyNumType current_key = Key_For_Command(command);

        out << "</select>"
            << Label("Category:", 22, 27, 146, 9)
            << "<div class=\"box\" style=\"" << Rect_Style(22, 42, 138, 72) << "\">"
            << "<div class=\"label\" style=\"left:" << Px(7) << ";top:" << Px(7) << ";width:" << Px(127) << ";height:" << Px(58) << ";\">"
            << Escape_Rml(command != nullptr ? command->Get_Category() : "") << "</div></div>"
            << "<div class=\"box\" style=\"" << Rect_Style(22, 57, 138, 57) << "\"><span style=\"position:absolute;left:8px;top:-9px;background-color:#1f2b32;\">Description:</span></div>"
            << "<div id=\"description\" class=\"label\" style=\"" << Rect_Style(29, 68, 127, 42) << "\">" << Escape_Rml(command != nullptr ? command->Get_Description() : "") << "</div>"
            << Label("Press new shortcut key:", 22, 119, 128, 9)
            << "<div id=\"new_shortcut\" class=\"label center\" style=\"" << Rect_Style(22, 131, 85, 14) << ";background-color:#eef5f7;color:#101820;\">Press a key</div>"
            << Button("assign", "Assign", 110, 132, 50, 14)
            << Label("Currently assigned to:", 22, 151, 146, 11)
            << Label("Current shortcut:", 168, 151, 146, 11)
            << "<div id=\"assigned_to\" class=\"label center\" style=\"" << Rect_Style(22, 166, 146, 10) << "\"></div>"
            << "<div id=\"current_shortcut\" class=\"label center\" style=\"" << Rect_Style(168, 166, 146, 10) << "\">" << Escape_Rml(Key_Name(current_key)) << "</div>"
            << Button("reset", "Reset All", 22, 181, 54, 14)
            << Button("ok", "OK", 187, 181, 50, 14)
            << Button("cancel", "Cancel", 264, 181, 50, 14)
            << Document_End();
        return out.str();
    }

    std::string Build_Abort()
    {
        std::ostringstream out;
        out << Document_Begin(256, 63, "Abort")
            << "<div class=\"label center\" style=\"" << Rect_Style(22, 12, 212, 19) << "\">Do you want to abort the mission?</div>"
            << Button("abort", "Abort", 22, 37, 60, 14);

        const bool can_restart_or_surrender = Session.Type == GAME_NORMAL || (PlayerPtr != nullptr && !PlayerPtr->IsDefeated && !PlayerPtr->IsToWin && !PlayerPtr->IsToLose && !PlayerPtr->IsToDie);
        out << Button("surrender", Session.Type == GAME_NORMAL ? "Restart" : "Surrender", 98, 37, 60, 14, can_restart_or_surrender)
            << Button("cancel", "Cancel", 174, 37, 60, 14)
            << Document_End();
        return out.str();
    }

    std::string Build_Surrender()
    {
        std::ostringstream out;
        out << Document_Begin(256, 63, "Surrender")
            << "<div class=\"label center\" style=\"" << Rect_Style(22, 12, 212, 19) << "\">Do you want to surrender?</div>"
            << Button("surrender", "Surrender", 58, 37, 60, 14)
            << Button("cancel", "Cancel", 138, 37, 60, 14)
            << Document_End();
        return out.str();
    }

    std::string Build_Reset_Hotkeys_Confirm()
    {
        std::ostringstream out;
        out << Document_Begin(256, 63, "Reset Hotkeys")
            << "<div class=\"label center\" style=\"" << Rect_Style(22, 12, 212, 19) << "\">Reset all keyboard shortcuts?</div>"
            << Button("ok", "Yes", 58, 37, 60, 14)
            << Button("cancel", "No", 138, 37, 60, 14)
            << Document_End();
        return out.str();
    }

    void Pump_Modal()
    {
        Windows_Message_Handler();
        SDL_Update_Screen(VisibleSurface);
        Call_Back();
        Sleep(1);
    }

    ModalResult Run_Modal(ModalKind kind)
    {
        CurrentKind = kind;
        CurrentResult = ModalResult::Pending;

        std::string document;
        switch (kind) {
        case ModalKind::Main: document = Build_Main_Menu(); break;
        case ModalKind::Settings: document = Build_Settings(); break;
        case ModalKind::Sound: document = Build_Sound(); break;
        case ModalKind::Keyboard: document = Build_Keyboard(); break;
        case ModalKind::Abort: document = Build_Abort(); break;
        case ModalKind::Surrender: document = Build_Surrender(); break;
        case ModalKind::ResetHotkeysConfirm: document = Build_Reset_Hotkeys_Confirm(); break;
        }

        Rml::ElementDocument* rml_document = ViniferaRmlUi::Load_Document(document.c_str());
        if (rml_document == nullptr) {
            return ModalResult::Cancel;
        }

        rml_document->AddEventListener("click", &Listener);
        rml_document->AddEventListener("change", &Listener);
        rml_document->AddEventListener("keydown", &Listener);

        while (CurrentResult == ModalResult::Pending) {
            Pump_Modal();
        }

        const ModalResult result = CurrentResult;
        ViniferaRmlUi::Close_Document();
        return result;
    }

    void Do_Load_Dialog()
    {
        LoadOptionsClass().Load_Dialog();
    }

    void Do_Save_Dialog()
    {
        char description[512] = {};
        if (Scen != nullptr) {
            std::strncpy(description, Scen->Description, sizeof(description) - 1);
        }
        LoadOptionsClass().Save_Dialog(description);
    }

    void Do_Delete_Dialog()
    {
        LoadOptionsClass().Delete_Dialog();
    }

    void Vinifera_Special_Dialog()
    {
        if (SpecialDialog == SDLG_NONE) {
            return;
        }

        if (Session.Type == GAME_NORMAL || (PlayerPtr != nullptr && !PlayerPtr->IsToLose && !PlayerPtr->IsToWin && !PlayerPtr->IsToDie && (SpecialDialogFlag || PlayerPtr->IsDefeated))) {
            SpecialDialogFlag = true;
        }

        Pause_Scenario();

        while (SpecialDialog != SDLG_NONE) {
            switch (SpecialDialog) {
            case SDLG_OPTIONS: {
                const ModalResult result = Run_Modal(ModalKind::Main);
                switch (result) {
                case ModalResult::Resume:
                    Apply_Internet_Main_Sliders();
                    SpecialDialog = SDLG_NONE;
                    break;
                case ModalResult::Restate:
                    Restate_Mission(Scen);
                    SpecialDialog = SDLG_NONE;
                    break;
                case ModalResult::Load:
                    Do_Load_Dialog();
                    SpecialDialog = SDLG_NONE;
                    break;
                case ModalResult::Save:
                    Do_Save_Dialog();
                    SpecialDialog = SDLG_OPTIONS;
                    break;
                case ModalResult::Delete:
                    Do_Delete_Dialog();
                    SpecialDialog = SDLG_OPTIONS;
                    break;
                case ModalResult::Settings:
                    SpecialDialog = SDLG_SETTINGS;
                    break;
                case ModalResult::Abort:
                    if (Session.Type == GAME_INTERNET) {
                        SpecialDialog = SDLG_ABORT;
                    } else {
                        SpecialDialog = WestwoodOnline_Tournament ? SDLG_SURRENDER : SDLG_ABORT;
                    }
                    break;
                default:
                    SpecialDialog = SDLG_NONE;
                    break;
                }
                break;
            }

            case SDLG_SETTINGS: {
                const ModalResult result = Run_Modal(ModalKind::Settings);
                switch (result) {
                case ModalResult::Sound:
                    Apply_Settings();
                    SpecialDialog = SDLG_SOUND;
                    break;
                case ModalResult::Keyboard:
                    Apply_Settings();
                    SpecialDialog = SDLG_KEYBOARD;
                    break;
                case ModalResult::Ok:
                    Apply_Settings();
                    SpecialDialog = SDLG_OPTIONS;
                    break;
                default:
                    SpecialDialog = SDLG_OPTIONS;
                    break;
                }
                break;
            }

            case SDLG_SOUND: {
                bool stay = true;
                while (stay) {
                    const ModalResult result = Run_Modal(ModalKind::Sound);
                    switch (result) {
                    case ModalResult::Play:
                        Play_Selected_Theme();
                        break;
                    case ModalResult::Stop:
                        Theme.Queue_Song(THEME_QUIET);
                        break;
                    default:
                        Apply_Sound_Slider("music_volume", false);
                        Apply_Sound_Slider("sound_volume", false);
                        Apply_Sound_Slider("voice_volume", false);
                        Options.Save_Settings();
                        stay = false;
                        break;
                    }
                }
                SpecialDialog = SDLG_SETTINGS;
                break;
            }

            case SDLG_KEYBOARD: {
                bool stay = true;
                while (stay) {
                    const ModalResult result = Run_Modal(ModalKind::Keyboard);
                    switch (result) {
                    case ModalResult::AssignHotkey:
                        Assign_Hotkey();
                        PendingHotkey = KN_NONE;
                        break;
                    case ModalResult::ResetHotkeys:
                        if (Run_Modal(ModalKind::ResetHotkeysConfirm) == ModalResult::Ok) {
                            RawFileClass file("Keyboard.ini");
                            file.Delete();
                            Load_Keyboard_Hotkeys();
                        }
                        break;
                    case ModalResult::Ok:
                        Save_Hotkeys();
                        stay = false;
                        break;
                    case ModalResult::Cancel:
                    default:
                        Load_Keyboard_Hotkeys();
                        stay = false;
                        break;
                    }
                }
                SpecialDialog = SDLG_SETTINGS;
                break;
            }

            case SDLG_ABORT: {
                const ModalResult result = Run_Modal(ModalKind::Abort);
                if (result == ModalResult::Abort) {
                    Queue_Exit();
                } else if (result == ModalResult::Surrender) {
                    if (Session.Type == GAME_NORMAL) {
                        PlayerRestarts = true;
                    } else if (PlayerPtr != nullptr) {
                        OutList.Add(EventClass(PlayerPtr->HeapID, EVENT_DESTRUCT));
                        SpecialDialogFlag = false;
                    }
                }
                SpecialDialog = SDLG_NONE;
                break;
            }

            case SDLG_SURRENDER:
                if (PlayerPtr != nullptr && !PlayerPtr->IsDefeated && !PlayerPtr->IsToWin && !PlayerPtr->IsToLose && !PlayerPtr->IsToDie && Run_Modal(ModalKind::Surrender) == ModalResult::Surrender) {
                    if (Session.Type == GAME_NORMAL || Session.Type == GAME_SKIRMISH) {
                        PlayerPtr->Flag_To_Lose();
                    } else {
                        OutList.Add(EventClass(PlayerPtr->HeapID, EVENT_DESTRUCT));
                        SpecialDialogFlag = false;
                    }
                }
                SpecialDialog = SDLG_NONE;
                break;

            case SDLG_WOL_OPTIONS:
                DoFindPage();
                SpecialDialog = SDLG_NONE;
                break;

            default:
                SpecialDialog = SDLG_NONE;
                break;
            }
        }

        Resume_Scenario();
        Map.Flag_To_Redraw(GS_REDRAW_ALL);
    }
}

void SpecialDialog_Hooks()
{
    Patch_Jump(0x00462640, &Vinifera_Special_Dialog);
}
