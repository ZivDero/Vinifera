/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains functions for the SDL system.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "sdl_functions.h"

#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_hints.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_oldnames.h"
#include "SDL3/SDL_video.h"
#include "cctooltip.h"
#include "cdctrl.h"
#include "command.h"
#include "convert.h"
#include "debughandler.h"
#include "mouse.h"
#include "optionsext.h"
#include "playmovie.h"
#include "rect.h"
#include "sdlmouse.h"
#include "sdlsurface.h"
#include "tibsun_functions.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"
#include "vinifera_imgui.h"
#include "vinifera_rmlui.h"
#include "vinifera_util.h"
#include "windialog.h"
#include "wsproto.h"
#include "wwmouse.h"

#include <windowsx.h>


namespace
{
    /**
     *  Applies the SDL_GPU driver hint for the selected backend. The values
     *  match SDL_GPU's bootstrap names ("direct3d12", "vulkan"). Auto leaves
     *  the hint unset and lets SDL pick.
     *
     *  @author: ZivDero
     */
    void SDL_Apply_GPU_Driver_Hint()
    {
        const char* requested_driver_name = OptionsClassExtension::Get_Renderer_Driver_SDL_Name(OptionsExtension->RendererDriver);
        const char* requested_driver_config_name = OptionsClassExtension::Get_Renderer_Driver_Config_Name(OptionsExtension->RendererDriver);

        DEBUG_INFO("Requested GPU driver: %s\n", requested_driver_config_name);

        if (requested_driver_name != nullptr) {
            SDL_SetHint(SDL_HINT_GPU_DRIVER, requested_driver_name);
        } else {
            SDL_ResetHint(SDL_HINT_GPU_DRIVER);
        }
    }

    /**
     *  Computes the tactical display rectangle for the given visible area.
     *
     *  @author: ZivDero
     */
    Rect SDL_Get_Display_View_Rect(const Rect& visible_rect)
    {
        Rect temp = visible_rect;
        temp.X = Options.SidebarSide || Debug_Map ? 0 : 168;
        temp.Y = 16;
        temp.Width -= 168;
        temp.Height -= 16;
        return temp;
    }

    /**
     *  Recalculates the SDL mouse cursor image if a cursor exists.
     *
     *  @author: ZivDero
     */
    void SDL_Recalc_Mouse_Cursor_Image()
    {
        if (MouseCursor != nullptr) {
            static_cast<SDLMouseClass*>(MouseCursor)->Recalc_Cursor_Image();
        }
    }

    /**
     *  Rebuilds the software surfaces and UI state for the current display mode.
     *
     *  @author: ZivDero
     */
    void SDL_Rebuild_Display_State(const Rect& visible_rect)
    {
        Rect temp = SDL_Get_Display_View_Rect(visible_rect);

        VisibleRect = visible_rect;
        VideoWidth = visible_rect.Width;
        VideoHeight = visible_rect.Height;

        VisibleSurface = SDLSurface::Create_Primary();

        Allocate_Surfaces(
            VisibleRect,
            Rect(0, 0, temp.Width, VisibleRect.Height),
            Rect(0, 0, temp.Width, VisibleRect.Height),
            Rect(0, 0, 168, VisibleRect.Height));
        LogicalSurface = HiddenSurface;

        Hide_Mouse();
        SDL_Recalc_Mouse_Cursor_Image();
        Show_Mouse();

        Map.Set_View_Dimensions(temp);
        Map.Init_IO();
        Map.Activate(1);
        Map.Shift_Sidebar();
        Map.Flag_To_Redraw(GS_REDRAW_ALL);
        Show_Mouse();
    }
}


/**
 *  Allocates all game surfaces with the given sizes.
 *
 *  @author: ZivDero, tomsons26
 */
bool SDL_Allocate_Surfaces(const Rect& hidden_rect, const Rect& composite_rect, const Rect& tile_rect, const Rect& sidebar_rect, bool hidden_first)
{
    DEBUG_INFO("Allocating new surfaces\n");

    if (AlternateSurface != nullptr) {
        DEBUG_INFO("Deleting AlternateSurface\n");
        delete AlternateSurface;
        AlternateSurface = nullptr;
    }

    if (HiddenSurface != nullptr) {
        DEBUG_INFO("Deleting HiddenSurface\n");
        delete HiddenSurface;
        HiddenSurface = nullptr;
    }

    if (CompositeSurface != nullptr) {
        DEBUG_INFO("Deleting CompositeSurface\n");
        delete CompositeSurface;
        CompositeSurface = nullptr;
    }

    if (TileSurface != nullptr) {
        DEBUG_INFO("Deleting TileSurface\n");
        delete TileSurface;
        TileSurface = nullptr;
    }

    if (SidebarSurface != nullptr) {
        DEBUG_INFO("Deleting SidebarSurface\n");
        delete SidebarSurface;
        SidebarSurface = nullptr;
    }

    if (hidden_first && hidden_rect.Is_Valid()) {
        HiddenSurface = new SDLSurface(hidden_rect.Width, hidden_rect.Height);
        HiddenSurface->Fill(0);
        DEBUG_INFO("HiddenSurface (%dx%d)\n", hidden_rect.Width, hidden_rect.Height);
    }

    if (composite_rect.Is_Valid()) {
        CompositeSurface = new SDLSurface(composite_rect.Width, composite_rect.Height);
        CompositeSurface->Fill(0);
        DEBUG_INFO("CompositeSurface (%dx%d)\n", composite_rect.Width, composite_rect.Height);
    }

    if (tile_rect.Is_Valid()) {
        TileSurface = new SDLSurface(tile_rect.Width, tile_rect.Height);
        TileSurface->Fill(0);
        DEBUG_INFO("TileSurface (%dx%d)\n", tile_rect.Width, tile_rect.Height);
    }

    if (sidebar_rect.Is_Valid()) {
        SidebarSurface = new SDLSurface(sidebar_rect.Width, sidebar_rect.Height);
        SidebarSurface->Fill(0);
        DEBUG_INFO("SidebarSurface (%dx%d)\n", sidebar_rect.Width, sidebar_rect.Height);
    }

    if (!hidden_first && hidden_rect.Is_Valid()) {
        HiddenSurface = new SDLSurface(hidden_rect.Width, hidden_rect.Height);
        HiddenSurface->Fill(0);
        DEBUG_INFO("HiddenSurface (%dx%d)\n", hidden_rect.Width, hidden_rect.Height);
    }

    if (hidden_rect.Is_Valid()) {
        AlternateSurface = new SDLSurface(hidden_rect.Width, hidden_rect.Height);
        AlternateSurface->Fill(0);
        DEBUG_INFO("AlternateSurface (%dx%d)\n", hidden_rect.Width, hidden_rect.Height);
    }

    return true;
}


/**
 *  Initializes the SDL_GPU presentation layer.
 *
 *  @author: ZivDero
 */
bool SDL_Set_Video_Mode(HWND, int width, int height, int bits_per_pixel)
{
    if (SDLWindow == nullptr) {
        DEBUG_ERROR("SDLWindow is null!\n");
        return false;
    }

    /**
     *  We need to delete the existing presentation layer first.
     */
    SDL_Reset_Video_Mode();

    /**
     *  Query the window's pixel format. Used only for diagnostics under SDL_GPU,
     *  the actual presentation format is the swapchain texture format.
     */
    SDL_PixelFormat pixel_format = SDL_GetWindowPixelFormat(SDLWindow);
    if (pixel_format == SDL_PIXELFORMAT_UNKNOWN || SDL_BITSPERPIXEL(pixel_format) < 16) {
        DEBUG_ERROR("SDL3 window pixel format unsupported: %s (%d bpp)\n", SDL_GetPixelFormatName(pixel_format), SDL_BITSPERPIXEL(pixel_format));
        return false;
    }

    DEBUG_INFO("Pixel format: %s (%d bpp)\n", SDL_GetPixelFormatName(pixel_format), SDL_BITSPERPIXEL(pixel_format));

    /**
     *  Apply the GPU driver selection before creating the device.
     */
    SDL_Apply_GPU_Driver_Hint();

    /**
     *  Create an SDL_GPU device. Request all formats the upstream RmlUi backend
     *  can compile shaders into so the same code paths run on every supported
     *  backend (D3D12 wants DXIL, Vulkan wants SPIR-V).
     */
    const SDL_GPUShaderFormat shader_formats =
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_DXIL |
        SDL_GPU_SHADERFORMAT_MSL;

    const bool debug_device =
#ifdef _DEBUG
        true;
#else
        false;
#endif

    SDLGPUDevice = SDL_CreateGPUDevice(shader_formats, debug_device, nullptr);
    if (SDLGPUDevice == nullptr) {
        DEBUG_ERROR("SDL_CreateGPUDevice failed! SDL Error: %s\n", SDL_GetError());
        return false;
    }
    DEBUG_INFO("SDL_GPU device created. Driver: %s\n", SDL_GetGPUDeviceDriver(SDLGPUDevice));

    if (!SDL_ClaimWindowForGPUDevice(SDLGPUDevice, SDLWindow)) {
        DEBUG_ERROR("SDL_ClaimWindowForGPUDevice failed! SDL Error: %s\n", SDL_GetError());
        SDL_DestroyGPUDevice(SDLGPUDevice);
        SDLGPUDevice = nullptr;
        return false;
    }

    /**
     *  Configure swapchain composition (always SDR for now) and present mode
     *  driven by the user's vsync preference. Mailbox is preferred for low
     *  latency without tearing when supported, otherwise fall back to vsync
     *  when on, immediate when off.
     */
    SDL_GPUPresentMode present_mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!OptionsExtension->IsVSync &&
        SDL_WindowSupportsGPUPresentMode(SDLGPUDevice, SDLWindow, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
        present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
    SDL_SetGPUSwapchainParameters(SDLGPUDevice, SDLWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, present_mode);

    SDL_GPUTextureFormat swapchain_format = SDL_GetGPUSwapchainTextureFormat(SDLGPUDevice, SDLWindow);
    SDLSwapchainTextureFormat = static_cast<unsigned>(swapchain_format);
    DEBUG_INFO("SDL_GPU swapchain format: %u\n", SDLSwapchainTextureFormat);

    /**
     *  Create the persistent GPU texture that mirrors the game's CPU-rendered
     *  framebuffer. RGBA8 is used unconditionally because it is universally
     *  supported by SDL_GPU backends as a sampler+blit source; the per-frame
     *  cost of expanding RGB565 -> RGBA8 on the CPU is negligible at the
     *  resolutions Vinifera targets.
     */
    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tex_info.width = static_cast<Uint32>(width);
    tex_info.height = static_cast<Uint32>(height);
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = 1;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

    SDLGameFrameTexture = SDL_CreateGPUTexture(SDLGPUDevice, &tex_info);
    if (SDLGameFrameTexture == nullptr) {
        DEBUG_ERROR("SDL_CreateGPUTexture (game frame) failed! SDL Error: %s\n", SDL_GetError());
        SDL_ReleaseWindowFromGPUDevice(SDLGPUDevice, SDLWindow);
        SDL_DestroyGPUDevice(SDLGPUDevice);
        SDLGPUDevice = nullptr;
        return false;
    }
    DEBUG_INFO("Game-frame GPU texture created (format=%d).\n", static_cast<int>(tex_info.format));

    /**
     *  Save video mode information.
     */
    VideoWidth = width;
    VideoHeight = height;
    VideoBitsPerPixel = bits_per_pixel;

    if (!ViniferaImGui::Initialize(MainWindow, SDLGPUDevice, swapchain_format)) {
        DEBUG_ERROR("Vinifera ImGui could not be initialized.\n");
    }

    if (!ViniferaRmlUi::Initialize(MainWindow, SDLWindow, SDLGPUDevice)) {
        DEBUG_ERROR("Vinifera RmlUi could not be initialized.\n");
    }

    return true;
}


/**
 *  Resets video mode and deletes the SDL_GPU presentation layer.
 *
 *  @author: ZivDero
 */
void SDL_Reset_Video_Mode()
{
    ViniferaRmlUi::Shutdown();
    ViniferaImGui::Shutdown();

    if (SDLGPUDevice != nullptr) {
        // Make sure the GPU is idle before tearing down resources owned by the
        // device, otherwise releases can race with in-flight command buffers.
        SDL_WaitForGPUIdle(SDLGPUDevice);

        if (SDLGameFrameTexture != nullptr) {
            SDL_ReleaseGPUTexture(SDLGPUDevice, SDLGameFrameTexture);
            SDLGameFrameTexture = nullptr;
        }

        SDL_ReleaseWindowFromGPUDevice(SDLGPUDevice, SDLWindow);
        SDL_DestroyGPUDevice(SDLGPUDevice);
        SDLGPUDevice = nullptr;
    }

    SDLSwapchainTextureFormat = 0;

    /**
     *  Clear video mode information.
     */
    VideoWidth = 0;
    VideoHeight = 0;
    VideoBitsPerPixel = 0;
}


/**
 *  Pointer to the window procedure set by SDL.
 */
static WNDPROC SDL_Proc = nullptr;

/**
 *  Replacement window procedure for the main window.
 *
 *  @author: tomsons26, ZivDero
 */
LRESULT CALLBACK SDL_Windows_Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    const LPARAM original_lParam = lParam;

    if (ViniferaImGui::Process_Window_Message(hwnd, message, wParam, original_lParam)) {
        return 0;
    }

    if (ViniferaRmlUi::Process_Window_Message(hwnd, message, wParam, original_lParam)) {
        return 0;
    }

    /*
    **  Scale mouse inputs before they are processed by SDL or the game.
    */
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        if (SDL_Should_Scale()) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            x = static_cast<int>(x * SDL_XScale());
            y = static_cast<int>(y * SDL_YScale());

            lParam = MAKELPARAM(x, y);
        }
        break;
    default:
        break;
    }

    /*
    **  Pass on any messages intended for the winsock message handler.
    */
    if (PacketTransport) {
        if (message == (UINT)PacketTransport->Protocol_Event_Message()) {
            if (PacketTransport->Message_Handler(hwnd, message, wParam, lParam)) {
                return DefWindowProc(hwnd, message, wParam, lParam);
            } else {
                return 0;
            }
        }
    }

    Map.Message_Handler(hwnd, message, wParam, lParam);

    switch (message) {

        /*
        **  Refresh the window.
        */
    case WM_PAINT:
        if (MouseCursor != nullptr && VisibleSurface != nullptr && HiddenSurface != nullptr && CompositeSurface != nullptr) {
            if (TacticalActive == true) {
                Update_Visible_Surface(MouseCursor->Is_Captured(), CompositeSurface);
                Map.Blit_Sidebar(true);
            } else if (Movie_Is_Playing() == true) {
                Movie_Update_Visible_Surface();
            } else {
                Update_Visible_Surface(MouseCursor->Is_Captured(), HiddenSurface);
            }
        }

        /*
        **  Tell SDL that the window needs refreshing to simulate what it does itself.
        */
        SDL_Event event;
        event.type = SDL_EVENT_WINDOW_EXPOSED;
        event.window.windowID = SDL_GetWindowID(SDLWindow);
        event.window.data1 = 0;
        event.window.data2 = 0;
        SDL_PushEvent(&event);

        /*
        **  But don't let SDL handle this event, or it will break Win32 controls' drawing.
        */
        return DefWindowProc(hwnd, message, wParam, lParam);

    case WM_CLOSE:
        CDControl.Unlock_All_CD_Trays();
        break;

        /*
        **  Windoze message says we have to shut down. Try and do it cleanly.
        */
    case WM_DESTROY:
        if (ToolTips != nullptr) {
            delete ToolTips;
            ToolTips = nullptr;
        }
        CDControl.Unlock_All_CD_Trays();
        MainWindow = nullptr;

        /*
        **  If we are shutting down gracefully than flag that the message loop has finished.
        **  If this is a forced shutdown (ReadyToQuit == 0) then try and close down everything
        **  before we exit.
        */
        switch (ReadyToQuit) {
        default:
        case 1:
            ReadyToQuit = 2;
            break;

        case 0:
            break;
        }
        return 0;

    case WM_ACTIVATEAPP:
        if (hwnd == MainWindow && GameInFocus != (wParam != 0)) {
            GameInFocus = wParam != 0;
            if (GameInFocus) {
                Focus_Restore();

                /*
                **  Force all child controls to redraw when regaining focus.
                */
                EnumChildWindows(
                    hwnd,
                    [](HWND child, LPARAM) -> BOOL {
                        RedrawWindow(child, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
                        return TRUE;
                    },
                    0);

                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
            } else {
                Focus_Loss();
            }
        }
        return 0;

    case WM_RBUTTONUP:

        /*
        **  Set some kind of scolling flag, perhaps "CanScroll".
        */
        Map.field_1D0C = false;
        break;

    case WM_MOVING:
        On_WM_MOVING(hwnd, wParam, lParam);
        return CallWindowProc(SDL_Proc, hwnd, message, wParam, lParam);

    case WM_MOUSEWHEEL:
        if (!_MouseWheel) {
            _MouseWheel = true;

            /**
             *  If we are not currently playing a scenario, no need to execute this command.
             */
            if (TacticalActive && ScenarioActive) {
                if (GET_WHEEL_DELTA_WPARAM(wParam) < 0) {
                    Do_Command("SidebarDown");
                } else {
                    Do_Command("SidebarUp");
                }
            }
            _MouseWheel = false;
        }
        break;

    case WM_SYSCOMMAND:
        switch (wParam) {

        case SC_CLOSE:
            CDControl.Unlock_All_CD_Trays();

#ifdef TS_CLIENT
            /*
            **  TS Client users are used to Alt+F4 aborting the game, which in turn closes the game
            **  because there is no main menu in the TS Client.
            */
            if (GameActive) {
                Queue_Exit();
            }
#endif
            /*
            **  Windows sent us a close message. Probably in response to Alt-F4. Ignore it by
            **  pretending to handle the message and returning true;
            */
            return 0;

        case SC_SCREENSAVE:

            /*
            **  Windoze is about to start the screen saver. If we just return without passing
            **  this message to DefWindowProc then the screen saver will not be allowed to start.
            */
            return 0;

        default:
            break;
        }
        break;

    default:
        break;
    }

    /*
    **  Pass this message through to the keyboard handler.
    */
    Keyboard->Message_Handler(hwnd, message, wParam, lParam);

    return CallWindowProc(SDL_Proc, hwnd, message, wParam, lParam);
}


/**
 *  Creates the main game window.
 *
 *  @author: ZivDero, CCHyper
 */
bool SDL_Create_Main_Window(HINSTANCE instance, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        DEBUG_ERROR("SDL_Init failed! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    SDL_PropertiesID props = SDL_CreateProperties();

    if (WindowedMode) {
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    } else {
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
    }

    DWORD dwPid = GetProcessId(GetCurrentProcess());
    if (!dwPid) {
        DEBUG_ERROR("Create_Main_Window() - Failed to get the process id!\n");
        return false;
    }

    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, Vinifera_Get_Window_Title(dwPid));

    /**
     *  Create the window. SDL_ClaimWindowForGPUDevice (called later from
     *  SDL_Set_Video_Mode) installs whichever window properties the chosen
     *  GPU backend requires, so we don't pre-set OpenGL/Vulkan flags here.
     */
    SDLWindow = SDL_CreateWindowWithProperties(props);
    if (SDLWindow == nullptr) {
        DEBUG_ERROR("SDLWindow could not be created! SDL_Error: %s\n", SDL_GetError());
        return false;
    }
    DEBUG_INFO("SDLWindow created.\n");
    
    /**
     *  Record the size that the window has been created at.
     */
    SDL_GetWindowSize(SDLWindow, &SDLWindowWidth, &SDLWindowHeight);
    DEBUG_INFO("SDLWindow size: %d X %d.\n", SDLWindowWidth, SDLWindowHeight);

    /**
     *  Save the window handle for the game to use.
     */
    props = SDL_GetWindowProperties(SDLWindow);
    MainWindow = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));

    /**
     *  We draw Win32 child windows as part of the main window, so we need to disable clipping.
     *  Otherwise, we will see black boxes where child windows are.
     */
    LONG_PTR style = GetWindowLongPtr(MainWindow, GWL_STYLE);
    style &= ~(WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
    SetWindowLongPtr(MainWindow, GWL_STYLE, style);

    /**
     *  Set the window to use our window procedure, save the one SDL set.
     */
    SDL_Proc = (WNDPROC)SetWindowLongPtr(MainWindow, GWLP_WNDPROC, (LONG_PTR)SDL_Windows_Procedure);

    /**
     *  Explicitly set input focus to the window.
     */
    SDL_RaiseWindow(SDLWindow);
    GameInFocus = true; // The SDL window needs this initially otherwise we need to alt-tab to gain focus.

    /**
     *  This used to happen on WM_CREATE but our proc is no longer the proc that's used when
     *  the window is created, so it never happens.
     */
    if (!ToolTips) {
        ToolTips = new CCToolTip(MainWindow);
        if (ToolTips) {
            ToolTips->Set_Timer_Delay(500);
        }
    }

    return true;
}


/**
 *  Destroys the main game window.
 *
 *  @author: CCHyper
 */
void SDL_Destroy_Main_Window()
{
    /**
     *  Destroy window.
     */
    SDL_DestroyWindow(SDLWindow);
    SDLWindow = nullptr;
}


namespace
{
    /**
     *  Uploads the game's RGB565 software framebuffer into the persistent
     *  SDLGameFrameTexture (RGBA8) by mapping a transfer buffer and recording
     *  a copy pass. Returns false if any GPU resource could not be acquired.
     */
    bool SDL_Upload_Game_Frame(SDL_GPUCommandBuffer* command_buffer, Surface* surface)
    {
        const Uint32 width = static_cast<Uint32>(surface->Get_Width());
        const Uint32 height = static_cast<Uint32>(surface->Get_Height());
        const Uint32 pixel_count = width * height;
        const Uint32 dst_size = pixel_count * 4u;

        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = dst_size;

        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(SDLGPUDevice, &transfer_info);
        if (transfer == nullptr) {
            DEBUG_WARNING("SDL_CreateGPUTransferBuffer failed in Upload_Game_Frame: %s\n", SDL_GetError());
            return false;
        }

        Uint32* dst = static_cast<Uint32*>(SDL_MapGPUTransferBuffer(SDLGPUDevice, transfer, true));
        if (dst == nullptr) {
            SDL_ReleaseGPUTransferBuffer(SDLGPUDevice, transfer);
            return false;
        }

        if (void* locked = surface->Lock()) {
            const int stride_bytes = surface->Stride();
            const Uint16* src_rows = static_cast<const Uint16*>(locked);

            for (Uint32 y = 0; y < height; ++y) {
                const Uint16* src = reinterpret_cast<const Uint16*>(reinterpret_cast<const Uint8*>(src_rows) + static_cast<size_t>(y) * stride_bytes);
                Uint32* dst_row = dst + static_cast<size_t>(y) * width;

                for (Uint32 x = 0; x < width; ++x) {
                    const Uint16 pixel = src[x];
                    // RGB565 -> RGBA8 with replicated low bits for proper full-range coverage.
                    const Uint32 r = ((pixel >> 11) & 0x1Fu);
                    const Uint32 g = ((pixel >> 5) & 0x3Fu);
                    const Uint32 b = (pixel & 0x1Fu);
                    const Uint32 r8 = (r << 3) | (r >> 2);
                    const Uint32 g8 = (g << 2) | (g >> 4);
                    const Uint32 b8 = (b << 3) | (b >> 2);
                    dst_row[x] = r8 | (g8 << 8) | (b8 << 16) | (0xFFu << 24);
                }
            }

            surface->Unlock();
        }

        SDL_UnmapGPUTransferBuffer(SDLGPUDevice, transfer);

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
        if (copy_pass == nullptr) {
            SDL_ReleaseGPUTransferBuffer(SDLGPUDevice, transfer);
            return false;
        }

        SDL_GPUTextureTransferInfo src_info = {};
        src_info.transfer_buffer = transfer;
        src_info.offset = 0;

        SDL_GPUTextureRegion dst_region = {};
        dst_region.texture = SDLGameFrameTexture;
        dst_region.w = width;
        dst_region.h = height;
        dst_region.d = 1;

        SDL_UploadToGPUTexture(copy_pass, &src_info, &dst_region, true);
        SDL_EndGPUCopyPass(copy_pass);

        // The transfer buffer can be released immediately; the upload command
        // is now recorded into the in-flight command buffer and SDL will keep
        // the underlying allocation alive until the buffer is submitted.
        SDL_ReleaseGPUTransferBuffer(SDLGPUDevice, transfer);
        return true;
    }
}


/**
 *  Update the screen with any rendering performed since the previous call.
 *
 *  @author: ZivDero, CCHyper, tomsons26
 */
bool SDL_Update_Screen(Surface* surface)
{
    if (SDLGPUDevice == nullptr) {
        return false;
    }

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(SDLGPUDevice);
    if (command_buffer == nullptr) {
        DEBUG_WARNING("SDL_AcquireGPUCommandBuffer failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GPUTexture* swapchain_texture = nullptr;
    Uint32 swapchain_width = 0;
    Uint32 swapchain_height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, SDLWindow, &swapchain_texture, &swapchain_width, &swapchain_height)) {
        // Window minimized or otherwise unavailable; cancel the frame.
        SDL_CancelGPUCommandBuffer(command_buffer);
        return true;
    }

    if (swapchain_texture == nullptr) {
        // SDL_WaitAndAcquireGPUSwapchainTexture can succeed but return null when
        // the window is currently unrenderable (e.g. minimized). Submit anyway
        // so any queued resource releases drain.
        SDL_SubmitGPUCommandBuffer(command_buffer);
        return true;
    }

    /**
     *  Phase 1: blit the game framebuffer into the swapchain texture. The
     *  upload pass handles RGB565 -> RGBA8 conversion; SDL_BlitGPUTexture
     *  then handles RGBA8 -> swapchain format and any required scaling.
     */
    if (surface != nullptr) {
        if (SDL_Upload_Game_Frame(command_buffer, surface)) {
            SDL_GPUBlitInfo blit_info = {};
            blit_info.source.texture = SDLGameFrameTexture;
            blit_info.source.w = static_cast<Uint32>(surface->Get_Width());
            blit_info.source.h = static_cast<Uint32>(surface->Get_Height());

            blit_info.destination.texture = swapchain_texture;
            blit_info.destination.w = swapchain_width;
            blit_info.destination.h = swapchain_height;

            blit_info.load_op = SDL_GPU_LOADOP_DONT_CARE; // first write of the frame, no need to load.
            blit_info.filter = (OptionsExtension->ScaleMode == SDL_SCALEMODE_LINEAR)
                ? SDL_GPU_FILTER_LINEAR
                : SDL_GPU_FILTER_NEAREST;
            blit_info.cycle = false;

            SDL_BlitGPUTexture(command_buffer, &blit_info);
        }

        // Mouse cursor uses the same scaled-vs-not state machine as before.
        static bool scaled = SDL_Should_Scale();
        if (scaled != SDL_Should_Scale()) {
            scaled = SDL_Should_Scale();
            static_cast<SDLMouseClass*>(MouseCursor)->Recalc_Cursor_Image();
        }
    } else {
        // No game surface to blit. Clear the swapchain so we don't present
        // garbage from a previous frame.
        SDL_GPUColorTargetInfo color_info = {};
        color_info.texture = swapchain_texture;
        color_info.load_op = SDL_GPU_LOADOP_CLEAR;
        color_info.store_op = SDL_GPU_STOREOP_STORE;
        color_info.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
        SDL_GPURenderPass* clear_pass = SDL_BeginGPURenderPass(command_buffer, &color_info, 1, nullptr);
        if (clear_pass != nullptr) {
            SDL_EndGPURenderPass(clear_pass);
        }
    }

    /**
     *  Phase 2: ImGui. Prepare uploads vertex/index buffers via its own copy
     *  pass before we start a render pass for it.
     */
    ViniferaImGui::Prepare(command_buffer);
    {
        SDL_GPUColorTargetInfo color_info = {};
        color_info.texture = swapchain_texture;
        color_info.load_op = SDL_GPU_LOADOP_LOAD;
        color_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* imgui_pass = SDL_BeginGPURenderPass(command_buffer, &color_info, 1, nullptr);
        if (imgui_pass != nullptr) {
            ViniferaImGui::Render(command_buffer, imgui_pass);
            SDL_EndGPURenderPass(imgui_pass);
        }
    }

    /**
     *  Phase 3: RmlUi. The backend manages its own copy/render passes
     *  internally, drawing on top of the swapchain with LOAD_OP_LOAD.
     */
    ViniferaRmlUi::Render(command_buffer, swapchain_texture, swapchain_width, swapchain_height);

    SDL_SubmitGPUCommandBuffer(command_buffer);
    return true;
}


/**
 *  Returns if scaling should currently be applied.
 *  We turn off scaling when any windows dialogs are open
 *  because we cannot properly scale their input.
 *
 *  @author: ZivDero
 */
bool SDL_Should_Scale()
{
    return WSDialogCount == 0 && (SpecialDialog == SDLG_NONE || ViniferaRmlUi::Is_Dialog_Open());
}


/**
 *  Changes the display mode to the given resolution.
 *
 *  @author: ZivDero, tomsons26
 */
bool SDL_Change_Display_Mode(int width, int height)
{
    DEBUG_INFO("About to set video mode\n");

    Rect old_visible_rect = VisibleRect;
    if (!old_visible_rect.Is_Valid() && VideoWidth > 0 && VideoHeight > 0) {
        old_visible_rect = Rect(0, 0, VideoWidth, VideoHeight);
    }

    const int old_video_width = VideoWidth;
    const int old_video_height = VideoHeight;
    const int old_video_bits_per_pixel = VideoBitsPerPixel > 0 ? VideoBitsPerPixel : 16;

    int old_window_x = 0;
    int old_window_y = 0;
    int old_window_width = SDLWindowWidth;
    int old_window_height = SDLWindowHeight;

    Hide_Mouse();

    /**
     *  Delete the old primary surface.
     */
    if (VisibleSurface != nullptr) {
        DEBUG_INFO("Deleting VisibleSurface\n");
        delete VisibleSurface;
        VisibleSurface = nullptr;
    }

    /**
     *  If the window size isn't set manually, resize the window to refect the new resolution.
     */
    if (WindowedMode) {
        int window_width = width;
        int window_height = height;

        /**
         *  If the window size isn't set manually, resize the window to refect the new resolution.
         */
        if (OptionsExtension->WindowWidth > 0 && OptionsExtension->WindowHeight > 0) {
            window_width = OptionsExtension->WindowWidth;
            window_height = OptionsExtension->WindowHeight;
        }

        /**
         *  Get the current window size and position.
         */
        SDL_GetWindowPosition(SDLWindow, &old_window_x, &old_window_y);
        SDL_GetWindowSize(SDLWindow, &old_window_width, &old_window_height);

        /**
         *  Compute the current center point.
         */
        int center_x = old_window_x + old_window_width / 2;
        int center_y = old_window_y + old_window_height / 2;

        /**
         *  Compute new top-left corner so that the center stays the same.
         */
        int new_x = center_x - window_width / 2;
        int new_y = center_y - window_height / 2;

        /**
         *  Apply and save the new position and size.
         */
        SDL_SetWindowPosition(SDLWindow, new_x, new_y);
        SDL_SetWindowSize(SDLWindow, window_width, window_height);

        SDLWindowWidth = window_width;
        SDLWindowHeight = window_height;
        DEBUG_INFO("SDLWindow size: %d X %d.\n", SDLWindowWidth, SDLWindowHeight);
    }

    /**
     *  Recreate all the SDL intermediates (texture, renderer).
     */
    if (!Set_Video_Mode(MainWindow, width, height, 16)) {
        DEBUG_ERROR("Set_Video_Mode failed.\n");

        if (WindowedMode) {
            SDL_SetWindowPosition(SDLWindow, old_window_x, old_window_y);
            SDL_SetWindowSize(SDLWindow, old_window_width, old_window_height);
            SDLWindowWidth = old_window_width;
            SDLWindowHeight = old_window_height;
            DEBUG_INFO("SDLWindow size restored: %d X %d.\n", SDLWindowWidth, SDLWindowHeight);
        }

        if (old_visible_rect.Is_Valid() && old_video_width > 0 && old_video_height > 0) {
            DEBUG_WARNING("Restoring previous display mode.\n");

            if (!Set_Video_Mode(MainWindow, old_video_width, old_video_height, old_video_bits_per_pixel)) {
                DEBUG_ERROR("Failed to restore previous video mode.\n");
                Show_Mouse();
                return false;
            }

            SDL_Rebuild_Display_State(old_visible_rect);
        } else {
            DEBUG_ERROR("Previous display mode is invalid and cannot be restored.\n");
        }

        Show_Mouse();
        return false;
    }

    /**
     *  Set the new surface resolution and reallocate the game surfaces.
     */
    SDL_Rebuild_Display_State(Rect(0, 0, width, height));
    DEBUG_INFO("VisibleRect: %dx%d\n", width, height);

    DEBUG_INFO("Mode change complete.\n");

    return true;
}
