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
#include <cstdio>
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
    std::string SelectedHotkeyCategory;
    std::string SelectedHotkeyCommandName;
    Rml::ElementDocument* ActiveDocument = nullptr;

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

        Rml::ElementDocument* document = ActiveDocument != nullptr ? ActiveDocument : context->GetDocument(0);
        return document != nullptr ? document->GetElementById(id) : nullptr;
    }

    Rml::ElementFormControl* Form_Control(const char* id)
    {
        return rmlui_dynamic_cast<Rml::ElementFormControl*>(Find_Element(id));
    }

    Rml::ElementFormControlSelect* Select_Control(const char* id)
    {
        return rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(Find_Element(id));
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

    void Set_Control_Value(const char* id, int value)
    {
        if (Rml::ElementFormControl* control = Form_Control(id)) {
            control->SetValue(std::to_string(value));
        }
    }

    void Set_Control_Value(const char* id, const std::string& value)
    {
        if (Rml::ElementFormControl* control = Form_Control(id)) {
            control->SetValue(value);
        }
    }

    void Set_Checkbox(const char* id, bool checked)
    {
        if (Rml::Element* element = Find_Element(id)) {
            if (checked) {
                element->SetAttribute("checked", "");
            } else {
                element->RemoveAttribute("checked");
            }
        }
    }

    void Set_Enabled(const char* id, bool enabled)
    {
        if (Rml::Element* element = Find_Element(id)) {
            if (enabled) {
                element->RemoveAttribute("disabled");
            } else {
                element->SetAttribute("disabled", "");
            }
        }
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

    std::vector<std::string> Sorted_Categories()
    {
        std::vector<std::string> categories;
        for (int index = 0; index < Commands.Count(); ++index) {
            CommandClass* command = Commands[index];
            if (command == nullptr || command->Get_Category() == nullptr) {
                continue;
            }

            const std::string category = command->Get_Category();
            if (std::find_if(categories.begin(), categories.end(), [&category](const std::string& value) {
                return stricmp(value.c_str(), category.c_str()) == 0;
            }) == categories.end()) {
                categories.push_back(category);
            }
        }

        std::sort(categories.begin(), categories.end(), [](const std::string& lhs, const std::string& rhs) {
            return stricmp(lhs.c_str(), rhs.c_str()) < 0;
        });
        return categories;
    }

    std::vector<CommandClass*> Commands_For_Category(const std::string& category)
    {
        std::vector<CommandClass*> commands;
        for (int index = 0; index < Commands.Count(); ++index) {
            CommandClass* command = Commands[index];
            if (command != nullptr && command->Get_Category() != nullptr && stricmp(command->Get_Category(), category.c_str()) == 0) {
                commands.push_back(command);
            }
        }

        std::sort(commands.begin(), commands.end(), [](const CommandClass* lhs, const CommandClass* rhs) {
            return stricmp(lhs->Get_UI_Name(), rhs->Get_UI_Name()) < 0;
        });
        return commands;
    }

    CommandClass* Command_By_Name(const std::string& name)
    {
        for (int index = 0; index < Commands.Count(); ++index) {
            CommandClass* command = Commands[index];
            if (command != nullptr && stricmp(command->Get_Name(), name.c_str()) == 0) {
                return command;
            }
        }
        return nullptr;
    }

    CommandClass* Selected_Command()
    {
        if (!SelectedHotkeyCommandName.empty()) {
            return Command_By_Name(SelectedHotkeyCommandName);
        }
        return nullptr;
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
        CommandClass* command = Selected_Command();
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
        CommandClass* command = Selected_Command();
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

    void Populate_Settings_Document()
    {
        Set_Control_Value("game_speed", (MAX_SPEED_SETTING - 1) - Options.GameSpeed);
        Set_Control_Value("scroll_speed", (MAX_SCROLL_SETTING - 1) - Options.ScrollRate);
        Set_Control_Value("detail_level", Options.DetailLevel);
        Set_Control_Value("difficulty", Options.Difficulty);
        Set_Text("game_speed_label", Speed_Label((MAX_SPEED_SETTING - 1) - Options.GameSpeed));
        Set_Text("scroll_speed_label", Speed_Label((MAX_SCROLL_SETTING - 1) - Options.ScrollRate));
        Set_Text("detail_label", Detail_Label(Options.DetailLevel));
        Set_Text("difficulty_label", Difficulty_Label(Options.Difficulty));
        Set_Checkbox("cameo_text", Options.SidebarCameoText);
        Set_Checkbox("action_lines", Options.ActionLines);
        Set_Checkbox("tooltips", Options.ToolTips);
        Set_Checkbox("scroll_coasting", Options.ScrollMethod == 0);
    }

    void Populate_Main_Document()
    {
        Set_Enabled("restate", Session.Type != GAME_SKIRMISH);
        const bool saves_present = Save_Files_Present();
        Set_Enabled("load", saves_present);
        Set_Enabled("delete", saves_present);
        Set_Control_Value("latency", 3 - Session.LatencyFudge);
        Set_Control_Value("game_speed", (MAX_SPEED_SETTING - 1) - Options.GameSpeed);
        Set_Text("latency_label", Connection_Label(3 - Session.LatencyFudge));
        Set_Text("game_speed_label", Speed_Label((MAX_SPEED_SETTING - 1) - Options.GameSpeed));
    }

    void Populate_Sound_Document()
    {
        Set_Control_Value("music_volume", static_cast<int>(Options.ScoreVolume * 10.0f + 0.5f));
        Set_Control_Value("sound_volume", static_cast<int>(Options.SoundVolume * 10.0f + 0.5f));
        Set_Control_Value("voice_volume", static_cast<int>(Options.VoiceVolume * 10.0f + 0.5f));
        Set_Checkbox("shuffle", Options.IsScoreShuffle);
        Set_Checkbox("repeat", Options.IsScoreRepeat);

        Rml::ElementFormControlSelect* select = Select_Control("theme_list");
        if (select == nullptr) {
            return;
        }

        select->RemoveAll();

        int active_row = 0;
        int row = 0;
        int visible_num = 1;
        for (ThemeType theme = THEME_FIRST; theme < Theme.Max_Themes(); theme = static_cast<ThemeType>(theme + 1)) {
            if (!Theme.Is_Allowed(theme)) {
                continue;
            }

            char buffer[160];
            const int length = Theme.Track_Length(theme);
            std::snprintf(buffer, sizeof(buffer), "%02d - %s [%d:%02d]", visible_num++, Theme.Full_Name(theme), length / 60, length % 60);
            select->Add(Escape_Rml(buffer), std::to_string(static_cast<int>(theme)));
            if (Theme.What_Is_Playing() == theme) {
                active_row = row;
            }
            ++row;
        }
        select->SetSelection(active_row);
    }

    void Populate_Command_List()
    {
        Rml::ElementFormControlSelect* select = Select_Control("command_list");
        if (select == nullptr) {
            return;
        }

        select->RemoveAll();

        const std::vector<CommandClass*> commands = Commands_For_Category(SelectedHotkeyCategory);
        for (CommandClass* command : commands) {
            select->Add(Escape_Rml(command->Get_UI_Name()), command->Get_Name());
        }

        if (SelectedHotkeyCommandName.empty() && !commands.empty()) {
            SelectedHotkeyCommandName = commands.front()->Get_Name();
        }

        if (!SelectedHotkeyCommandName.empty()) {
            select->SetValue(SelectedHotkeyCommandName);
        }
    }

    void Populate_Keyboard_Document()
    {
        Rml::ElementFormControlSelect* category_select = Select_Control("category_list");
        if (category_select == nullptr) {
            return;
        }

        const std::vector<std::string> categories = Sorted_Categories();
        category_select->RemoveAll();
        for (const std::string& category : categories) {
            category_select->Add(Escape_Rml(category), category);
        }

        if (SelectedHotkeyCategory.empty() || std::find_if(categories.begin(), categories.end(), [](const std::string& category) {
            return stricmp(category.c_str(), SelectedHotkeyCategory.c_str()) == 0;
        }) == categories.end()) {
            SelectedHotkeyCategory = !categories.empty() ? categories.front() : "";
            SelectedHotkeyCommandName.clear();
        }

        if (!SelectedHotkeyCategory.empty()) {
            category_select->SetValue(SelectedHotkeyCategory);
        }

        Populate_Command_List();
        PendingHotkey = KN_NONE;
        Update_Hotkey_Details();
    }

    void Populate_Document(ModalKind kind)
    {
        switch (kind) {
        case ModalKind::Main:
            Populate_Main_Document();
            break;
        case ModalKind::Settings:
            Populate_Settings_Document();
            break;
        case ModalKind::Sound:
            Populate_Sound_Document();
            break;
        case ModalKind::Keyboard:
            Populate_Keyboard_Document();
            break;
        case ModalKind::Abort:
            Set_Text("surrender", Session.Type == GAME_NORMAL ? "Restart" : "Surrender");
            Set_Enabled("surrender", Session.Type == GAME_NORMAL || (PlayerPtr != nullptr && !PlayerPtr->IsDefeated && !PlayerPtr->IsToWin && !PlayerPtr->IsToLose && !PlayerPtr->IsToDie));
            break;
        default:
            break;
        }
    }

    const char* Document_Path(ModalKind kind)
    {
        switch (kind) {
        case ModalKind::Main:
            if (Session.Type == GAME_NORMAL || Session.Type == GAME_SKIRMISH) return "RMLUI\\SPECIAL\\OPTIONS_SP.RML";
            if (Session.Type == GAME_INTERNET) return "RMLUI\\SPECIAL\\OPTIONS_WOL.RML";
            return "RMLUI\\SPECIAL\\OPTIONS_MP.RML";
        case ModalKind::Settings:
            if (GameActive && Session.Type == GAME_INTERNET) return "RMLUI\\SPECIAL\\SETTINGS_WOL.RML";
            if (!GameActive) return "RMLUI\\SPECIAL\\SETTINGS_SP.RML";
            return "RMLUI\\SPECIAL\\SETTINGS_MP.RML";
        case ModalKind::Sound:
            return !GameActive ? "RMLUI\\SPECIAL\\SOUND_LITE.RML" : "RMLUI\\SPECIAL\\SOUND_FULL.RML";
        case ModalKind::Keyboard:
            return "RMLUI\\SPECIAL\\KEYBOARD.RML";
        case ModalKind::Abort:
            return "RMLUI\\SPECIAL\\ABORT.RML";
        case ModalKind::Surrender:
            return "RMLUI\\SPECIAL\\SURRENDER.RML";
        case ModalKind::ResetHotkeysConfirm:
            return "RMLUI\\SPECIAL\\RESET_HOTKEYS.RML";
        }

        return "";
    }

    void Dialog_Size(ModalKind kind, int& width, int& height)
    {
        switch (kind) {
        case ModalKind::Main:
            if (Session.Type == GAME_INTERNET) {
                width = 680;
                height = 336;
            } else if (Session.Type == GAME_NORMAL || Session.Type == GAME_SKIRMISH) {
                width = 418;
                height = 280;
            } else {
                width = 418;
                height = 150;
            }
            break;
        case ModalKind::Settings:
            if (GameActive && Session.Type == GAME_INTERNET) {
                width = 588;
                height = 252;
            } else if (!GameActive) {
                width = 584;
                height = 326;
            } else {
                width = 584;
                height = 314;
            }
            break;
        case ModalKind::Sound:
            width = 588;
            height = !GameActive ? 224 : 430;
            break;
        case ModalKind::Keyboard:
            width = 672;
            height = 416;
            break;
        case ModalKind::Abort:
        case ModalKind::Surrender:
        case ModalKind::ResetHotkeysConfirm:
            width = 512;
            height = 126;
            break;
        }
    }

    void Center_Dialog(ModalKind kind)
    {
        if (ActiveDocument == nullptr) {
            return;
        }

        Rml::Element* dialog = ActiveDocument->QuerySelector(".dialog");
        if (dialog == nullptr) {
            return;
        }

        int width = 0;
        int height = 0;
        Dialog_Size(kind, width, height);

        const int left = std::max(0, (VideoWidth - width) / 2);
        const int top = std::max(0, (VideoHeight - height) / 2);

        dialog->SetProperty("left", std::to_string(left) + "px");
        dialog->SetProperty("top", std::to_string(top) + "px");
        dialog->SetProperty("width", std::to_string(width) + "px");
        dialog->SetProperty("height", std::to_string(height) + "px");
        dialog->SetProperty("margin-left", "0px");
        dialog->SetProperty("margin-top", "0px");
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

            if (event == "change" || event == "input") {
                if (id == "game_speed") Set_Text("game_speed_label", Speed_Label(Control_Int("game_speed")));
                if (id == "scroll_speed") Set_Text("scroll_speed_label", Speed_Label(Control_Int("scroll_speed")));
                if (id == "detail_level") Set_Text("detail_label", Detail_Label(Control_Int("detail_level")));
                if (id == "difficulty") Set_Text("difficulty_label", Difficulty_Label(Control_Int("difficulty")));
                if (id == "latency") Set_Text("latency_label", Connection_Label(Control_Int("latency")));
                if (id == "music_volume" || id == "sound_volume" || id == "voice_volume") Apply_Sound_Slider(id.c_str(), true);
                if (id == "shuffle" || id == "repeat") Apply_Sound_Checks(id.c_str());
                if (id == "category_list") {
                    if (Rml::ElementFormControlSelect* select = Select_Control("category_list")) {
                        SelectedHotkeyCategory = select->GetValue();
                        SelectedHotkeyCommandName.clear();
                        PendingHotkey = KN_NONE;
                        Populate_Command_List();
                        Update_Hotkey_Details();
                    }
                }
                if (id == "command_list") {
                    if (Rml::ElementFormControlSelect* select = Select_Control("command_list")) {
                        SelectedHotkeyCommandName = select->GetValue();
                    }
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

        Rml::ElementDocument* rml_document = ViniferaRmlUi::Open_Document(Document_Path(kind), ViniferaRmlUi::DocumentLayer::Modal);
        if (rml_document == nullptr) {
            return ModalResult::Cancel;
        }

        ActiveDocument = rml_document;
        Center_Dialog(kind);
        Populate_Document(kind);
        rml_document->AddEventListener("click", &Listener);
        rml_document->AddEventListener("change", &Listener);
        rml_document->AddEventListener("input", &Listener);
        rml_document->AddEventListener("keydown", &Listener);

        while (CurrentResult == ModalResult::Pending) {
            Pump_Modal();
        }

        const ModalResult result = CurrentResult;
        ViniferaRmlUi::Close_Document(rml_document);
        ActiveDocument = nullptr;
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
