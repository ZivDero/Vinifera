/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Main-window RmlUi integration.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "vinifera_rmlui.h"

#include "SDL3/SDL_render.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_video.h"
#include "ccfile.h"
#include "debughandler.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"

#include "lib/rawfile.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <windowsx.h>

namespace
{
    struct CompiledGeometry
    {
        std::vector<Rml::Vertex> Vertices;
        std::vector<int> Indices;
    };

    class ViniferaRenderInterface : public Rml::RenderInterface
    {
    public:
        explicit ViniferaRenderInterface(SDL_Renderer* renderer) :
            Renderer(renderer)
        {
            BlendMode = SDL_ComposeCustomBlendMode(
                SDL_BLENDFACTOR_ONE,
                SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                SDL_BLENDOPERATION_ADD,
                SDL_BLENDFACTOR_ONE,
                SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                SDL_BLENDOPERATION_ADD);
        }

        void Set_Renderer(SDL_Renderer* renderer)
        {
            Renderer = renderer;
        }

        void Set_Output_Scale(float xscale, float yscale)
        {
            XScale = xscale;
            YScale = yscale;
        }

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
        {
            CompiledGeometry* geometry = new CompiledGeometry;
            geometry->Vertices.assign(vertices.begin(), vertices.end());
            geometry->Indices.assign(indices.begin(), indices.end());
            return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
        }

        void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override
        {
            if (Renderer == nullptr || handle == 0) {
                return;
            }

            const CompiledGeometry* geometry = reinterpret_cast<const CompiledGeometry*>(handle);
            if (geometry->Vertices.empty() || geometry->Indices.empty()) {
                return;
            }

            if (SDLVertices.size() < geometry->Vertices.size()) {
                SDLVertices.resize(geometry->Vertices.size());
            }

            for (size_t index = 0; index < geometry->Vertices.size(); ++index) {
                const Rml::Vertex& vertex = geometry->Vertices[index];
                SDL_Vertex& sdl_vertex = SDLVertices[index];

                sdl_vertex.position = {
                    (vertex.position.x + translation.x) * XScale,
                    (vertex.position.y + translation.y) * YScale
                };
                sdl_vertex.tex_coord = { vertex.tex_coord.x, vertex.tex_coord.y };
                sdl_vertex.color = {
                    vertex.colour.red / 255.0f,
                    vertex.colour.green / 255.0f,
                    vertex.colour.blue / 255.0f,
                    vertex.colour.alpha / 255.0f
                };
            }

            SDL_SetRenderDrawBlendMode(Renderer, BlendMode);
            SDL_RenderGeometry(
                Renderer,
                reinterpret_cast<SDL_Texture*>(texture),
                SDLVertices.data(),
                static_cast<int>(geometry->Vertices.size()),
                geometry->Indices.data(),
                static_cast<int>(geometry->Indices.size()));
        }

        void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
        {
            delete reinterpret_cast<CompiledGeometry*>(handle);
        }

        Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override
        {
            return 0;
        }

        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override
        {
            if (Renderer == nullptr || source.empty()) {
                return 0;
            }

            SDL_Surface* surface = SDL_CreateSurfaceFrom(
                source_dimensions.x,
                source_dimensions.y,
                SDL_PIXELFORMAT_RGBA32,
                const_cast<Rml::byte*>(source.data()),
                source_dimensions.x * 4);

            if (surface == nullptr) {
                return 0;
            }

            SDL_Texture* texture = SDL_CreateTextureFromSurface(Renderer, surface);
            SDL_DestroySurface(surface);

            if (texture != nullptr) {
                SDL_SetTextureBlendMode(texture, BlendMode);
            }

            return reinterpret_cast<Rml::TextureHandle>(texture);
        }

        void ReleaseTexture(Rml::TextureHandle texture) override
        {
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(texture));
        }

        void EnableScissorRegion(bool enable) override
        {
            ScissorEnabled = enable;
            SDL_SetRenderClipRect(Renderer, enable ? &ScissorRect : nullptr);
        }

        void SetScissorRegion(Rml::Rectanglei region) override
        {
            ScissorRect.x = static_cast<int>(region.Left() * XScale);
            ScissorRect.y = static_cast<int>(region.Top() * YScale);
            ScissorRect.w = static_cast<int>(region.Width() * XScale);
            ScissorRect.h = static_cast<int>(region.Height() * YScale);

            if (ScissorEnabled) {
                SDL_SetRenderClipRect(Renderer, &ScissorRect);
            }
        }

    private:
        SDL_Renderer* Renderer = nullptr;
        SDL_BlendMode BlendMode = SDL_BLENDMODE_BLEND;
        SDL_Rect ScissorRect = {};
        bool ScissorEnabled = false;
        float XScale = 1.0f;
        float YScale = 1.0f;
        std::vector<SDL_Vertex> SDLVertices;
    };

    class ViniferaSystemInterface : public Rml::SystemInterface
    {
    public:
        ViniferaSystemInterface()
        {
            QueryPerformanceFrequency(&Frequency);
            QueryPerformanceCounter(&Startup);
        }

        void Set_Window(HWND hwnd)
        {
            Window = hwnd;
        }

        double GetElapsedTime() override
        {
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            return static_cast<double>(counter.QuadPart - Startup.QuadPart) / static_cast<double>(Frequency.QuadPart);
        }

        void SetMouseCursor(const Rml::String& cursor_name) override
        {
            const LPCTSTR cursor = cursor_name == "pointer" ? IDC_HAND : cursor_name == "text" ? IDC_IBEAM : IDC_ARROW;
            SetCursor(LoadCursor(nullptr, cursor));
        }

        void SetClipboardText(const Rml::String& text_utf8) override
        {
            if (Window == nullptr || !OpenClipboard(Window)) {
                return;
            }

            EmptyClipboard();

            const int wide_count = MultiByteToWideChar(CP_UTF8, 0, text_utf8.c_str(), -1, nullptr, 0);
            HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, wide_count * sizeof(wchar_t));
            if (data != nullptr) {
                wchar_t* output = static_cast<wchar_t*>(GlobalLock(data));
                MultiByteToWideChar(CP_UTF8, 0, text_utf8.c_str(), -1, output, wide_count);
                GlobalUnlock(data);
                SetClipboardData(CF_UNICODETEXT, data);
            }

            CloseClipboard();
        }

        void GetClipboardText(Rml::String& text) override
        {
            text.clear();

            if (Window == nullptr || !OpenClipboard(Window)) {
                return;
            }

            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            if (data != nullptr) {
                const wchar_t* input = static_cast<const wchar_t*>(GlobalLock(data));
                if (input != nullptr) {
                    const int utf8_count = WideCharToMultiByte(CP_UTF8, 0, input, -1, nullptr, 0, nullptr, nullptr);
                    std::string output(utf8_count > 0 ? utf8_count - 1 : 0, '\0');
                    if (!output.empty()) {
                        WideCharToMultiByte(CP_UTF8, 0, input, -1, &output[0], utf8_count, nullptr, nullptr);
                    }
                    text = output;
                    GlobalUnlock(data);
                }
            }

            CloseClipboard();
        }

    private:
        HWND Window = nullptr;
        LARGE_INTEGER Frequency = {};
        LARGE_INTEGER Startup = {};
    };

    class ViniferaFileInterface : public Rml::FileInterface
    {
    public:
        Rml::FileHandle Open(const Rml::String& path) override
        {
            std::string normalized = path;
            std::replace(normalized.begin(), normalized.end(), '/', '\\');
            while (!normalized.empty() && (normalized.front() == '\\' || normalized.front() == '.')) {
                if (normalized.front() == '.') {
                    if (normalized.size() > 1 && normalized[1] == '\\') {
                        normalized.erase(0, 2);
                    } else {
                        break;
                    }
                } else {
                    normalized.erase(normalized.begin());
                }
            }

            const bool raw_filesystem_path = normalized.size() > 1 && normalized[1] == ':';
            FileClass* file = raw_filesystem_path ? static_cast<FileClass*>(new RawFileClass(normalized.c_str())) : static_cast<FileClass*>(new CCFileClass(normalized.c_str()));
            if (!file->Open(FILE_ACCESS_READ)) {
                delete file;
                return 0;
            }

            return reinterpret_cast<Rml::FileHandle>(file);
        }

        void Close(Rml::FileHandle handle) override
        {
            FileClass* file = reinterpret_cast<FileClass*>(handle);
            if (file != nullptr) {
                file->Close();
                delete file;
            }
        }

        size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override
        {
            FileClass* file = reinterpret_cast<FileClass*>(handle);
            if (file == nullptr || buffer == nullptr || size == 0) {
                return 0;
            }

            const size_t read_size = std::min<size_t>(size, static_cast<size_t>(INT_MAX));
            return static_cast<size_t>(std::max<long>(0, file->Read(buffer, static_cast<int>(read_size))));
        }

        bool Seek(Rml::FileHandle handle, long offset, int origin) override
        {
            FileClass* file = reinterpret_cast<FileClass*>(handle);
            if (file == nullptr) {
                return false;
            }

            FileSeekType seek_type = FILE_SEEK_CURRENT;
            if (origin == SEEK_SET) {
                seek_type = FILE_SEEK_START;
            } else if (origin == SEEK_END) {
                seek_type = FILE_SEEK_END;
            }

            return file->Seek(offset, seek_type) >= 0;
        }

        size_t Tell(Rml::FileHandle handle) override
        {
            FileClass* file = reinterpret_cast<FileClass*>(handle);
            if (file == nullptr) {
                return 0;
            }

            return static_cast<size_t>(std::max<off_t>(0, file->Tell()));
        }
    };

    struct DocumentRecord
    {
        Rml::ElementDocument* Document = nullptr;
        ViniferaRmlUi::DocumentLayer Layer = ViniferaRmlUi::DocumentLayer::Overlay;
    };

    std::unique_ptr<ViniferaSystemInterface> SystemInterface;
    std::unique_ptr<ViniferaRenderInterface> RenderInterface;
    std::unique_ptr<ViniferaFileInterface> FileInterface;
    Rml::Context* Context = nullptr;
    Rml::ElementDocument* Document = nullptr;
    std::vector<DocumentRecord> Documents;
    bool Initialized = false;

    int Get_Key_Modifiers()
    {
        int state = 0;

        if (GetKeyState(VK_CAPITAL) & 1) state |= Rml::Input::KM_CAPSLOCK;
        if (GetKeyState(VK_NUMLOCK) & 1) state |= Rml::Input::KM_NUMLOCK;
        if (HIWORD(GetKeyState(VK_SHIFT)) & 1) state |= Rml::Input::KM_SHIFT;
        if (HIWORD(GetKeyState(VK_CONTROL)) & 1) state |= Rml::Input::KM_CTRL;
        if (HIWORD(GetKeyState(VK_MENU)) & 1) state |= Rml::Input::KM_ALT;

        return state;
    }

    Rml::Input::KeyIdentifier Convert_Key(WPARAM key)
    {
        if (key >= 'A' && key <= 'Z') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (key - 'A'));
        if (key >= '0' && key <= '9') return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (key - '0'));
        if (key >= VK_F1 && key <= VK_F12) return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_F1 + (key - VK_F1));

        switch (key) {
        case VK_BACK: return Rml::Input::KI_BACK;
        case VK_TAB: return Rml::Input::KI_TAB;
        case VK_RETURN: return Rml::Input::KI_RETURN;
        case VK_ESCAPE: return Rml::Input::KI_ESCAPE;
        case VK_SPACE: return Rml::Input::KI_SPACE;
        case VK_PRIOR: return Rml::Input::KI_PRIOR;
        case VK_NEXT: return Rml::Input::KI_NEXT;
        case VK_END: return Rml::Input::KI_END;
        case VK_HOME: return Rml::Input::KI_HOME;
        case VK_LEFT: return Rml::Input::KI_LEFT;
        case VK_UP: return Rml::Input::KI_UP;
        case VK_RIGHT: return Rml::Input::KI_RIGHT;
        case VK_DOWN: return Rml::Input::KI_DOWN;
        case VK_INSERT: return Rml::Input::KI_INSERT;
        case VK_DELETE: return Rml::Input::KI_DELETE;
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_SHIFT:
            return Rml::Input::KI_LSHIFT;
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_CONTROL:
            return Rml::Input::KI_LCONTROL;
        case VK_LMENU:
        case VK_RMENU:
        case VK_MENU:
            return Rml::Input::KI_LMENU;
        default:
            return Rml::Input::KI_UNKNOWN;
        }
    }

    float Window_X_To_Logical(int x)
    {
        if (SDLWindowWidth <= 0 || VideoWidth <= 0) {
            return static_cast<float>(x);
        }
        return static_cast<float>(x) * static_cast<float>(VideoWidth) / static_cast<float>(SDLWindowWidth);
    }

    float Window_Y_To_Logical(int y)
    {
        if (SDLWindowHeight <= 0 || VideoHeight <= 0) {
            return static_cast<float>(y);
        }
        return static_cast<float>(y) * static_cast<float>(VideoHeight) / static_cast<float>(SDLWindowHeight);
    }

    std::string Get_Windows_Font_Path(const char* filename)
    {
        char windows_directory[MAX_PATH] = {};
        GetWindowsDirectoryA(windows_directory, static_cast<UINT>(std::size(windows_directory)));

        std::string path = windows_directory;
        path += "\\Fonts\\";
        path += filename;
        return path;
    }

    bool Register_Font_Face(const std::string& path, const char* family, Rml::Style::FontWeight weight)
    {
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }

        const bool explicit_family = Rml::LoadFontFace(path, family, Rml::Style::FontStyle::Normal, weight, false);
        const bool fallback_family = Rml::LoadFontFace(path, family, Rml::Style::FontStyle::Normal, weight, true);
        return explicit_family || fallback_family;
    }

    bool Has_Documents()
    {
        return !Documents.empty();
    }

    bool Has_Documents(ViniferaRmlUi::DocumentLayer layer)
    {
        for (const DocumentRecord& record : Documents) {
            if (record.Layer == layer && record.Document != nullptr) {
                return true;
            }
        }
        return false;
    }

    void Remove_Document_Record(Rml::ElementDocument* document)
    {
        Documents.erase(std::remove_if(Documents.begin(), Documents.end(), [document](const DocumentRecord& record) {
            return record.Document == document;
        }), Documents.end());

        Document = nullptr;
        for (auto it = Documents.rbegin(); it != Documents.rend(); ++it) {
            if (it->Layer == ViniferaRmlUi::DocumentLayer::Modal && it->Document != nullptr) {
                Document = it->Document;
                break;
            }
        }
    }
}

bool ViniferaRmlUi::Initialize(HWND hwnd, SDL_Renderer* renderer)
{
    if (Initialized) {
        if (RenderInterface != nullptr) {
            RenderInterface->Set_Renderer(renderer);
        }
        if (SystemInterface != nullptr) {
            SystemInterface->Set_Window(hwnd);
        }
        return true;
    }

    if (hwnd == nullptr || renderer == nullptr) {
        return false;
    }

    SystemInterface = std::make_unique<ViniferaSystemInterface>();
    SystemInterface->Set_Window(hwnd);
    RenderInterface = std::make_unique<ViniferaRenderInterface>(renderer);
    FileInterface = std::make_unique<ViniferaFileInterface>();

    Rml::SetSystemInterface(SystemInterface.get());
    Rml::SetRenderInterface(RenderInterface.get());
    Rml::SetFileInterface(FileInterface.get());

    if (!Rml::Initialise()) {
        DEBUG_ERROR("RmlUi initialization failed.\n");
        FileInterface.reset();
        RenderInterface.reset();
        SystemInterface.reset();
        return false;
    }

    bool loaded_font = Register_Font_Face(Get_Windows_Font_Path("segoeui.ttf"), "ViniferaUi", Rml::Style::FontWeight::Normal);
    loaded_font = Register_Font_Face(Get_Windows_Font_Path("segoeuib.ttf"), "ViniferaUi", Rml::Style::FontWeight::Bold) || loaded_font;
    loaded_font = Register_Font_Face(Get_Windows_Font_Path("arial.ttf"), "ViniferaUi", Rml::Style::FontWeight::Normal) || loaded_font;
    loaded_font = Register_Font_Face(Get_Windows_Font_Path("arialbd.ttf"), "ViniferaUi", Rml::Style::FontWeight::Bold) || loaded_font;

    if (!loaded_font) {
        DEBUG_WARNING("RmlUi could not load a Windows UI font.\n");
    }

    Context = Rml::CreateContext("vinifera", Rml::Vector2i(std::max(VideoWidth, 1), std::max(VideoHeight, 1)));
    if (Context == nullptr) {
        Rml::Shutdown();
        FileInterface.reset();
        RenderInterface.reset();
        SystemInterface.reset();
        return false;
    }

    Initialized = true;
    return true;
}

void ViniferaRmlUi::Shutdown()
{
    if (!Initialized) {
        return;
    }

    Close_Documents(DocumentLayer::Hud);
    Close_Documents(DocumentLayer::Overlay);
    Close_Documents(DocumentLayer::Modal);
    if (Context != nullptr) {
        Rml::RemoveContext("vinifera");
        Context = nullptr;
    }

    Rml::Shutdown();
    FileInterface.reset();
    RenderInterface.reset();
    SystemInterface.reset();
    Initialized = false;
}

bool ViniferaRmlUi::Process_Window_Message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!Initialized || Context == nullptr || !Is_Input_Captured()) {
        return false;
    }

    bool handled = false;
    bool input_message = true;

    switch (msg) {
    case WM_MOUSEMOVE:
        handled = Context->ProcessMouseMove(
            static_cast<int>(Window_X_To_Logical(GET_X_LPARAM(lparam))),
            static_cast<int>(Window_Y_To_Logical(GET_Y_LPARAM(lparam))),
            Get_Key_Modifiers());
        break;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        handled = Context->ProcessMouseButtonDown(0, Get_Key_Modifiers());
        break;

    case WM_LBUTTONUP:
        ReleaseCapture();
        handled = Context->ProcessMouseButtonUp(0, Get_Key_Modifiers());
        break;

    case WM_RBUTTONDOWN:
        handled = Context->ProcessMouseButtonDown(1, Get_Key_Modifiers());
        break;

    case WM_RBUTTONUP:
        handled = Context->ProcessMouseButtonUp(1, Get_Key_Modifiers());
        break;

    case WM_MOUSEWHEEL:
        handled = Context->ProcessMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(-WHEEL_DELTA), Get_Key_Modifiers());
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        handled = Context->ProcessKeyDown(Convert_Key(wparam), Get_Key_Modifiers());
        break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        handled = Context->ProcessKeyUp(Convert_Key(wparam), Get_Key_Modifiers());
        break;

    case WM_CHAR:
    case WM_SYSCHAR:
        if (wparam == '\r') {
            handled = Context->ProcessTextInput('\n');
        } else if (wparam >= 32 && wparam != 127) {
            handled = Context->ProcessTextInput(static_cast<Rml::Character>(wparam));
        } else {
            handled = true;
        }
        break;

    default:
        input_message = false;
        break;
    }

    return input_message ? true : handled;
}

void ViniferaRmlUi::Render()
{
    if (!Initialized || Context == nullptr || !Has_Documents() || RenderInterface == nullptr) {
        return;
    }

    Context->SetDimensions(Rml::Vector2i(std::max(VideoWidth, 1), std::max(VideoHeight, 1)));

    const float xscale = VideoWidth > 0 ? static_cast<float>(SDLWindowWidth) / static_cast<float>(VideoWidth) : 1.0f;
    const float yscale = VideoHeight > 0 ? static_cast<float>(SDLWindowHeight) / static_cast<float>(VideoHeight) : 1.0f;
    RenderInterface->Set_Output_Scale(xscale, yscale);

    Context->Update();
    Context->Render();
}

bool ViniferaRmlUi::Is_Initialized()
{
    return Initialized;
}

bool ViniferaRmlUi::Is_Dialog_Open()
{
    return Initialized && Has_Documents();
}

bool ViniferaRmlUi::Has_Modal()
{
    return Initialized && Has_Documents(DocumentLayer::Modal);
}

bool ViniferaRmlUi::Is_Input_Captured()
{
    return Has_Modal();
}

Rml::Context* ViniferaRmlUi::Get_Context()
{
    return Context;
}

Rml::ElementDocument* ViniferaRmlUi::Load_Document(const char* rml)
{
    if (!Initialized || Context == nullptr || rml == nullptr) {
        return nullptr;
    }

    Close_Documents(DocumentLayer::Modal);
    Document = Context->LoadDocumentFromMemory(rml);
    if (Document != nullptr) {
        Documents.push_back({ Document, DocumentLayer::Modal });
        Document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Auto);
    }

    return Document;
}

Rml::ElementDocument* ViniferaRmlUi::Open_Document(const char* path, DocumentLayer layer)
{
    if (!Initialized || Context == nullptr || path == nullptr) {
        return nullptr;
    }

    if (layer == DocumentLayer::Modal) {
        Close_Documents(DocumentLayer::Modal);
    }

    Rml::ElementDocument* document = Context->LoadDocument(path);
    if (document == nullptr) {
        DEBUG_WARNING("RmlUi could not load document '%s'.\n", path);
        return nullptr;
    }

    Documents.push_back({ document, layer });
    if (layer == DocumentLayer::Modal) {
        Document = document;
        document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Auto);
    } else {
        document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }

    return document;
}

void ViniferaRmlUi::Close_Document(Rml::ElementDocument* document)
{
    if (document == nullptr) {
        return;
    }

    Remove_Document_Record(document);
    document->Close();

    if (Context != nullptr) {
        Context->Update();
    }
}

void ViniferaRmlUi::Close_Documents(DocumentLayer layer)
{
    std::vector<Rml::ElementDocument*> closing;
    for (const DocumentRecord& record : Documents) {
        if (record.Layer == layer && record.Document != nullptr) {
            closing.push_back(record.Document);
        }
    }

    for (Rml::ElementDocument* document : closing) {
        Close_Document(document);
    }
}

void ViniferaRmlUi::Close_Document()
{
    Close_Documents(DocumentLayer::Modal);
}
