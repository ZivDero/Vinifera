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

#include "SDL3/SDL_init.h"
#include "SDL3/SDL_oldnames.h"
#include "SDL3/SDL_video.h"
#include "cctooltip.h"
#include "cdctrl.h"
#include "command.h"
#include "convert.h"
#include "distortion_queue.h"
#include "font_cache.h"
#include "font_queue.h"
#include "gpu_surface.h"
#include "tactical_line_queue.h"
#include "graphics_device.h"
#include "perf_monitor.h"
#include "shp_cache.h"
#include "shroud_fog_queue.h"
#include "sprite_queue.h"
#include "surface_target_registry.h"
#include "tile_queue.h"
#include "voxel_asset.h"
#include "voxel_queue.h"
#include "iso_tile_atlas.h"
#include "palette_array.h"
#include "shp_atlas.h"
#include "debughandler.h"
#include "mouse.h"
#include "movie.h"
#include "optionsext.h"
#include "ownerdrawext_hooks.h"
#include "radar.h"
#include "playmovie.h"
#include "primitive_queue.h"
#include "rect.h"
#include "render_pass.h"
#include "scene_copy.h"
#include "sdl_movie.h"
#include "sdlmouse.h"
#include "unit_composite.h"
#include "spotlight_queue.h"
#include "unit_scratch.h"
#include "wave_queue.h"
#include "sdlsurface.h"
#include "tibsun_functions.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"
#include "vinifera_imgui.h"
#include "vinifera_util.h"
#include "windialog.h"
#include "wsproto.h"
#include "wwmouse.h"

#include <windowsx.h>


namespace
{
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

    void SDL_Register_Surface_Targets()
    {
        using namespace Vinifera::Gfx;

        SurfaceTargetRegistry& registry = SurfaceTargetRegistry::Get();
        registry.Clear();

        /**
         *  CompositeSurface: `GpuSurface` (Stage 7 Step 2). The tactical
         *  scene + HUD overlays are owned entirely by `SceneRT`; there is
         *  no CPU upload path for this surface. Vanilla draw vtable calls
         *  hit `GpuSurface` virtuals which enqueue to the GPU queues;
         *  vanilla pixel writes via Lock land in a dummy buffer and
         *  disappear (sacrificed effects listed in `SDL_Update_Screen`).
         */
        if (CompositeSurface != nullptr) {
            GpuSurfaceTargetDesc desc = {};
            desc.SurfacePtr = CompositeSurface;
            desc.LogicalRect = CompositeSurface->Get_Rect();
            desc.ScreenRect = CompositeSurface->Get_Rect();
            desc.OutputTarget = GpuRenderTarget::Scene;
            registry.Bind(desc);
        }

        if (TileSurface != nullptr) {
            GpuSurfaceTargetDesc desc = {};
            desc.SurfacePtr = TileSurface;
            desc.LogicalRect = TileSurface->Get_Rect();
            desc.ScreenRect = TileSurface->Get_Rect();
            desc.OutputTarget = GpuRenderTarget::Scene;
            registry.Bind(desc);
        }

        /**
         *  SidebarSurface: `GpuSurface` (Stage 7 Step 3). Cameos +
         *  primitive frames render onto `SidebarRT` via the queues. WWFont
         *  text and the radar minimap are sacrificed visuals until Steps
         *  5–6 land their GPU ports; the audit-log warnings from
         *  `GpuSurface` warn-stubs name remaining CPU-only callers.
         */
        if (SidebarSurface != nullptr) {
            GpuSurfaceTargetDesc desc = {};
            desc.SurfacePtr = SidebarSurface;
            desc.LogicalRect = SidebarSurface->Get_Rect();
            desc.ScreenRect = Rect(SidebarRect.X, 0, SidebarSurface->Get_Width(), SidebarSurface->Get_Height());
            desc.OutputTarget = GpuRenderTarget::Sidebar;
            registry.Bind(desc);
        }

        /**
         *  HiddenSurface / VisibleSurface (CPU `SDLSurface`s for menus /
         *  legacy compat) carry no GPU output target. They aren't queried
         *  by any GPU dispatch path; the registry entries exist purely for
         *  Diagnostics-style enumeration.
         */
        if (HiddenSurface != nullptr) {
            GpuSurfaceTargetDesc desc = {};
            desc.SurfacePtr = HiddenSurface;
            desc.LogicalRect = HiddenSurface->Get_Rect();
            desc.ScreenRect = HiddenSurface->Get_Rect();
            desc.OutputTarget = GpuRenderTarget::None;
            registry.Bind(desc);
        }

        if (VisibleSurface != nullptr) {
            GpuSurfaceTargetDesc desc = {};
            desc.SurfacePtr = VisibleSurface;
            desc.LogicalRect = VisibleSurface->Get_Rect();
            desc.ScreenRect = VisibleSurface->Get_Rect();
            desc.OutputTarget = GpuRenderTarget::None;
            registry.Bind(desc);
        }
    }

    /**
     *  Shared entry guard for both sidebar phases — keeps preconditions in
     *  one place. Returns nullptr if any precondition fails.
     */
    static Vinifera::Gfx::GpuSurfaceTarget* SDL_Sidebar_RT_Target()
    {
        if (Vinifera::Gfx::Device == nullptr || SidebarSurface == nullptr || VideoWidth <= 0 || VideoHeight <= 0) {
            return nullptr;
        }
        if (!GameActive || !TacticalActive || !ScenarioActive || !Map.IsSidebarActive || Debug_Map) {
            return nullptr;
        }

        Vinifera::Gfx::GpuSurfaceTarget* target =
            Vinifera::Gfx::SurfaceTargetRegistry::Get().Find(SidebarSurface);
        if (target == nullptr
            || target->Get_Output_Target() != Vinifera::Gfx::GpuRenderTarget::Sidebar) {
            return nullptr;
        }
        return target;
    }


    /**
     *  Phase 1: Size `SidebarRT` to the sidebar's logical dimensions, bind
     *  it, and clear it. The queue flushes that follow paint cameos +
     *  primitives directly onto `SidebarRT` (no CPU upload — `SidebarSurface`
     *  is `GpuSurface` after Step 3, and its `Lock()` returns a dummy).
     */
    void SDL_Prepare_Sidebar_RT(SDL_ScaleMode /*scale_mode*/)
    {
        if (SDL_Sidebar_RT_Target() == nullptr) {
            return;
        }

        const int sidebar_width = SidebarSurface->Get_Width();
        const int sidebar_height = SidebarSurface->Get_Height();
        if (sidebar_width <= 0 || sidebar_height <= 0) {
            return;
        }

        if (!Vinifera::Gfx::Device->Ensure_Sidebar_Target_Size(sidebar_width, sidebar_height)) {
            return;
        }

        Vinifera::Gfx::Device->Bind_Sidebar_Target();
        Vinifera::Gfx::Device->Clear_Sidebar_Target();
    }


    /**
     *  Phase 2: After the queue flushes have drawn GPU SHPs / primitives
     *  onto SidebarRT, compose the now-complete sidebar texture into SceneRT
     *  at the sidebar's screen position. Re-binds SceneRT before drawing.
     */
    void SDL_Draw_Sidebar_RT_Compose(SDL_ScaleMode scale_mode)
    {
        if (SDL_Sidebar_RT_Target() == nullptr) {
            return;
        }

        const int sidebar_width = SidebarSurface->Get_Width();
        const int sidebar_height = SidebarSurface->Get_Height();
        if (sidebar_width <= 0 || sidebar_height <= 0) {
            return;
        }

        Vinifera::Gfx::Device->Bind_Scene_Target();

        /**
         *  SceneRT is now at vanilla's logical resolution, so the sidebar
         *  rect lands 1:1 at its logical coords. The present quad upscales
         *  SceneRT → Backbuffer at frame end.
         */
        Rect dst(SidebarRect.X, 0, sidebar_width, sidebar_height);

        Vinifera::Gfx::Device->Draw_Texture(
            Vinifera::Gfx::Device->Get_Sidebar_Target_SRV(),
            dst,
            scale_mode);
        Vinifera::Gfx::PerfMonitor::Get().Note_Sidebar_Composite();
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

        /**
         *  `VideoWidth/Height` advertise the logical resolution — vanilla
         *  CPU drawing code (movies, menus, score, loading) renders into
         *  logical-res CPU surfaces (HiddenSurface / AlternateSurface) and
         *  the GPU upscales them to the backbuffer at present time.
         *  Window-pixel paths (OwnerDraw, WinAPI dialogs) read backbuffer
         *  dims directly off the device.
         */
        VideoWidth  = visible_rect.Width;
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
        SDL_Register_Surface_Targets();
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
    Vinifera::Gfx::SurfaceTargetRegistry::Get().Clear();

    /**
     *  HiddenSurface / AlternateSurface live at *logical* res. Vanilla CPU
     *  drawing (movies, menus, score, loading screens) renders into them
     *  at that size; the GPU upscales the result to the backbuffer at
     *  present time.
     *
     *  OwnerDrawAlternate is the parallel surface at window (backbuffer)
     *  res, used as the OwnerDraw scratch for dialog widgets (vanilla's
     *  AlternateSurface refs inside ownrdraw.cpp / dialog code are
     *  Patch_Dword'd to point here — see ownerdrawext_hooks.cpp).
     */
    const Rect cpu_surface_rect = hidden_rect;
    Rect window_surface_rect = cpu_surface_rect;
    if (Vinifera::Gfx::Device != nullptr) {
        const int bb_w = Vinifera::Gfx::Device->Get_Backbuffer_Width();
        const int bb_h = Vinifera::Gfx::Device->Get_Backbuffer_Height();
        if (bb_w > 0 && bb_h > 0) {
            window_surface_rect = Rect(0, 0, bb_w, bb_h);
        }
    }

    if (AlternateSurface != nullptr) {
        DEBUG_INFO("Deleting AlternateSurface\n");
        delete AlternateSurface;
        AlternateSurface = nullptr;
    }

    if (OwnerDrawAlternate != nullptr) {
        DEBUG_INFO("Deleting OwnerDrawAlternate\n");
        delete OwnerDrawAlternate;
        OwnerDrawAlternate = nullptr;
    }

    if (OwnerDrawVisible != nullptr) {
        DEBUG_INFO("Deleting OwnerDrawVisible\n");
        delete OwnerDrawVisible;
        OwnerDrawVisible = nullptr;
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

    if (hidden_first && cpu_surface_rect.Is_Valid()) {
        HiddenSurface = new SDLSurface(cpu_surface_rect.Width, cpu_surface_rect.Height);
        HiddenSurface->Fill(0);
        DEBUG_INFO("HiddenSurface (%dx%d)\n", cpu_surface_rect.Width, cpu_surface_rect.Height);
    }

    if (composite_rect.Is_Valid()) {
        /**
         *  Stage 7 Step 2: `CompositeSurface` commits to `GpuSurface`. The
         *  tactical scene + HUD overlays live on `SceneRT`; vanilla HUD draw
         *  calls (Fill_Rect / Draw_Line / Put_Pixel / Draw_Shape) route
         *  through GpuSurface vtable methods which enqueue to the GPU
         *  queues. Vanilla code that writes pixels directly via Lock
         *  (voxels, waves, anything we haven't intercepted) writes to the
         *  dummy buffer and disappears — already sacrificed, recovered by
         *  Steps 7–8.
         */
        CompositeSurface = new GpuSurface(composite_rect.Width, composite_rect.Height,
                                          Vinifera::Gfx::GpuRenderTarget::Scene);
        DEBUG_INFO("CompositeSurface (%dx%d) [GpuSurface]\n", composite_rect.Width, composite_rect.Height);
    }

    if (tile_rect.Is_Valid()) {
        /**
         *  Stage 7 Step 1: `TileSurface` is the first surface to commit to
         *  the `GpuSurface` class. Terrain pixels live in `SceneRT`; the
         *  CPU-side dummy buffer satisfies vanilla's `Tactical::Render` outer
         *  Lock without backing real pixel data. Skip the post-allocation
         *  Fill(0) — there's nothing to initialise (and `GpuSurface::Fill`
         *  is a stub).
         */
        TileSurface = new GpuSurface(tile_rect.Width, tile_rect.Height,
                                     Vinifera::Gfx::GpuRenderTarget::Scene);
        DEBUG_INFO("TileSurface (%dx%d) [GpuSurface]\n", tile_rect.Width, tile_rect.Height);
    }

    if (sidebar_rect.Is_Valid()) {
        /**
         *  Stage 7 Step 3: `SidebarSurface` commits to `GpuSurface`. Sidebar
         *  SHP cameos + primitive frames render directly into `SidebarRT`
         *  via the queues; sidebar text (WWFont) and the radar minimap are
         *  sacrificed until Steps 5–6 GPU-port them. The CPU upload path
         *  retires here.
         */
        SidebarSurface = new GpuSurface(sidebar_rect.Width, sidebar_rect.Height,
                                        Vinifera::Gfx::GpuRenderTarget::Sidebar);
        DEBUG_INFO("SidebarSurface (%dx%d) [GpuSurface]\n", sidebar_rect.Width, sidebar_rect.Height);
    }

    if (!hidden_first && cpu_surface_rect.Is_Valid()) {
        HiddenSurface = new SDLSurface(cpu_surface_rect.Width, cpu_surface_rect.Height);
        HiddenSurface->Fill(0);
        DEBUG_INFO("HiddenSurface (%dx%d)\n", cpu_surface_rect.Width, cpu_surface_rect.Height);
    }

    if (cpu_surface_rect.Is_Valid()) {
        AlternateSurface = new SDLSurface(cpu_surface_rect.Width, cpu_surface_rect.Height);
        AlternateSurface->Fill(0);
        DEBUG_INFO("AlternateSurface (%dx%d)\n", cpu_surface_rect.Width, cpu_surface_rect.Height);
    }

    if (window_surface_rect.Is_Valid()) {
        OwnerDrawAlternate = new SDLSurface(window_surface_rect.Width, window_surface_rect.Height);
        OwnerDrawAlternate->Fill(0);
        DEBUG_INFO("OwnerDrawAlternate (%dx%d)\n", window_surface_rect.Width, window_surface_rect.Height);

        OwnerDrawVisible = new SDLSurface(window_surface_rect.Width, window_surface_rect.Height);
        OwnerDrawVisible->Fill(0);
        DEBUG_INFO("OwnerDrawVisible (%dx%d)\n", window_surface_rect.Width, window_surface_rect.Height);
    }

    SDL_Register_Surface_Targets();

    return true;
}


/**
 *  Initializes the SDL presentation layer.
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
     *  Query the window's pixel format.
     */
    SDL_PixelFormat pixel_format = SDL_GetWindowPixelFormat(SDLWindow);
    if (pixel_format == SDL_PIXELFORMAT_UNKNOWN || SDL_BITSPERPIXEL(pixel_format) < 16) {
        DEBUG_ERROR("SDL3 window pixel format unsupported: %s (%d bpp)\n", SDL_GetPixelFormatName(pixel_format), SDL_BITSPERPIXEL(pixel_format));
        return false;
    }

    DEBUG_INFO("Pixel format: %s (%d bpp)\n", SDL_GetPixelFormatName(pixel_format), SDL_BITSPERPIXEL(pixel_format));

    /**
     *  Create the D3D11 renderer (device, swap chain, present-quad pipeline).
     */
    if (Vinifera::Gfx::Device == nullptr) {
        Vinifera::Gfx::Device = new Vinifera::Gfx::GraphicsDevice();
    }
    if (!Vinifera::Gfx::Device->Initialize(MainWindow, SDLWindowWidth, SDLWindowHeight, OptionsExtension->IsVSync)) {
        DEBUG_ERROR("GraphicsDevice could not be initialized.\n");
        delete Vinifera::Gfx::Device;
        Vinifera::Gfx::Device = nullptr;
        return false;
    }

    /**
     *  Size the scene-side render targets (SceneRT, depth buffer, alpha buffer)
     *  to vanilla's logical render resolution. The present quad upscales
     *  SceneRT → Backbuffer on a borderless 4K display, so the heavy
     *  per-sprite pixel work happens at logical res rather than at display res.
     */
    if (!Vinifera::Gfx::Device->Set_Logical_Resolution(width, height)) {
        DEBUG_ERROR("GraphicsDevice logical-resolution targets failed (%dx%d).\n", width, height);
        return false;
    }

    /**
     *  Save video mode information. `VideoWidth/Height` advertise vanilla's
     *  logical game resolution — CPU surfaces (HiddenSurface, AlternateSurface,
     *  VisibleSurface) are sized to it and the GPU upscales them to the
     *  backbuffer at present time.
     */
    VideoWidth = width;
    VideoHeight = height;
    VideoBitsPerPixel = bits_per_pixel;

    /**
     *  OwnerDraw position math (dialog centering, window placement) reads
     *  these instead of `VideoWidth/Height` via Patch_Dword redirects, so
     *  dialogs stay positioned relative to the actual window.
     */
    OwnerDrawWidth  = SDLWindowWidth;
    OwnerDrawHeight = SDLWindowHeight;

    if (!ViniferaImGui::Initialize(MainWindow, Vinifera::Gfx::Device->Get_Device(), Vinifera::Gfx::Device->Get_Context())) {
        DEBUG_ERROR("Vinifera ImGui could not be initialized.\n");
    }

    /**
     *  PaletteArray must come up before SpriteQueue / VoxelQueue
     *  because PaletteLUT::Update_Palette (called lazily on first cache hit)
     *  allocates layers in the array.
     */
    if (!Vinifera::Gfx::PaletteArray::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera PaletteArray could not be initialized.\n");
    }

    if (!Vinifera::Gfx::SpriteQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera SpriteQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::TileQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera TileQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::ShroudFogQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera ShroudFogQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::PrimitiveQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera PrimitiveQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::FontQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera FontQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::TacticalLineQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera TacticalLineQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::VoxelQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera VoxelQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::DistortionQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera DistortionQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::SceneCopy::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera SceneCopy could not be initialized.\n");
    }

    if (!Vinifera::Gfx::UnitScratch::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera UnitScratch could not be initialized.\n");
    }

    if (!Vinifera::Gfx::WaveQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera WaveQueue could not be initialized.\n");
    }

    if (!Vinifera::Gfx::SpotLightQueue::Get().Initialize(*Vinifera::Gfx::Device)) {
        DEBUG_ERROR("Vinifera SpotLightQueue could not be initialized.\n");
    }

    return true;
}


/**
 *  Resets video mode and deletes the SDL presentation layer.
 *
 *  @author: ZivDero
 */
void SDL_Reset_Video_Mode()
{
    ViniferaImGui::Shutdown();

    /**
     *  Asset caches and the sprite/tile queues hold textures bound to the
     *  GraphicsDevice — release them before the device tears down, since
     *  re-initializing on a new device requires fresh resources anyway.
     */
    Vinifera::Gfx::TileQueue::Get().Shutdown();
    Vinifera::Gfx::SpriteQueue::Get().Shutdown();
    Vinifera::Gfx::ShroudFogQueue::Get().Shutdown();
    Vinifera::Gfx::PrimitiveQueue::Get().Shutdown();
    Vinifera::Gfx::FontQueue::Get().Shutdown();
    Vinifera::Gfx::TacticalLineQueue::Get().Shutdown();
    Vinifera::Gfx::VoxelQueue::Get().Shutdown();
    Vinifera::Gfx::VoxelAssetCache::Get().Shutdown();
    Vinifera::Gfx::DistortionQueue::Get().Shutdown();
    Vinifera::Gfx::SceneCopy::Get().Shutdown();
    Vinifera::Gfx::UnitScratch::Get().Shutdown();
    Vinifera::Gfx::WaveQueue::Get().Shutdown();
    Vinifera::Gfx::SpotLightQueue::Get().Shutdown();
    Vinifera::Gfx::IsoTileCache::Get().Clear();
    Vinifera::Gfx::IsoTileAtlas::Get().Shutdown();
    Vinifera::Gfx::ShpCache::Get().Clear();
    Vinifera::Gfx::ShpAtlas::Get().Shutdown();
    Vinifera::Gfx::PaletteCache::Get().Clear();
    Vinifera::Gfx::PaletteArray::Get().Shutdown();
    Vinifera::Gfx::FontCache::Get().Clear();

    ViniferaImGui::Shutdown();

    /**
     *  Tear down the D3D11 renderer (device, swap chain, surface texture).
     */
    if (Vinifera::Gfx::Device != nullptr) {
        Vinifera::Gfx::SurfaceTargetRegistry::Get().Clear();
        delete Vinifera::Gfx::Device;
        Vinifera::Gfx::Device = nullptr;
    }

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
        if (Vinifera_ModernMoviePlaying) {
            SDL_Movie_Repaint();
        } else if (MouseCursor != nullptr && VisibleSurface != nullptr && HiddenSurface != nullptr && CompositeSurface != nullptr) {
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

    case WM_SIZE: {
        const int new_w = LOWORD(lParam);
        const int new_h = HIWORD(lParam);
        if (new_w > 0 && new_h > 0) {
            if (Vinifera::Gfx::Device != nullptr) {
                Vinifera::Gfx::Device->Resize_Backbuffer(new_w, new_h);
            }
            SDLWindowWidth = new_w;
            SDLWindowHeight = new_h;
        }
        break;
    }

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
     *  Create the window.
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


/**
 *  Update the screen with any rendering performed since the previous call.
 *
 *  @author: ZivDero, CCHyper, tomsons26
 */
bool SDL_Update_Screen(Surface* surface)
{
    if (Vinifera::Gfx::Device == nullptr) {
        return false;
    }

    Vinifera::Gfx::PerfMonitor::Get().Begin_Frame();
    Vinifera::Gfx::PerfMonitor::Get().Set_Cache_Sizes(
        Vinifera::Gfx::ShpCache::Get().Size(),
        Vinifera::Gfx::IsoTileCache::Get().Size(),
        Vinifera::Gfx::PaletteCache::Get().Size());

    Vinifera::Gfx::Device->Set_VSync(OptionsExtension->IsVSync);

    static bool scaled = SDL_Should_Scale();
    SDL_ScaleMode scale_mode = OptionsExtension->ScaleMode;
    if (scale_mode == SDL_SCALEMODE_INVALID) {
        scale_mode = SDL_SCALEMODE_NEAREST;
    }

    if (surface) {
        /**
         *  If the scale has changed, recalculate the mouse cursor image.
         */
        if (scaled != SDL_Should_Scale()) {
            scaled = SDL_Should_Scale();
            static_cast<SDLMouseClass*>(MouseCursor)->Recalc_Cursor_Image();
        }
    }

    /**
     *  Routing: `CompositeSurface` (and nullptr fallback) take the GPU
     *  pipeline — queue flushes into SceneRT then upscale-present. Any
     *  other surface is a CPU `SDLSurface` (VisibleSurface for menus /
     *  score / escape / VQA); we upload its pixels and present them as a
     *  fullscreen quad against a black backbuffer (no "last battle frame"
     *  preserved behind the dialog — score / escape menus draw against the
     *  clear-color backdrop).
     *
     *  `Is_Direct_Draw()` returns true only for `SDLSurface`; `GpuSurface`
     *  (CompositeSurface, TileSurface, SidebarSurface) returns false. Any
     *  non-SDL surface routes to the GPU pipeline rather than risking a
     *  bogus static_cast.
     */
    const bool is_gpu = (surface == nullptr
                       || surface == CompositeSurface
                       || !surface->Is_Direct_Draw());

    Vinifera::Gfx::Device->Begin_Frame();

    /**
     *  Reset the per-frame SceneCopy guard so the first PostEffects-pass
     *  caller (DistortionQueue or VoxelQueue's predator path) triggers a
     *  fresh `CopyResource` of SceneRT into the snapshot.
     */
    Vinifera::Gfx::SceneCopy::Get().Begin_Frame();

    if (is_gpu) {
        Vinifera::Gfx::Device->Bind_Scene_Target();

        /**
         *  Stage 7 Step 2: `CompositeSurface` is `GpuSurface`. There is no
         *  CPU buffer to upload — the entire tactical scene + HUD comes
         *  from the GPU queue flushes below, drawing into `SceneRT`.
         */

        /**
         *  Shroud / fog alpha writes — vanilla's `Draw_Shroud_Or_Fog_Shape`
         *  and `Draw_Fog_Shape` were patched to enqueue commands here
         *  instead of blitting into the CPU AlphaBuffer. Must run before
         *  alpha lights so the multiplicative light formula composes over
         *  the shroud baseline (alpha = 0 at shrouded cells → light × 0
         *  = 0, which is correct).
         */
        Vinifera::Gfx::ShroudFogQueue::Get().Flush(*Vinifera::Gfx::Device);

        /**
         *  Replay vanilla's `AlphaShapeClass::Draw_In_Area` blits onto the
         *  GPU alpha buffer (the CPU paths were no-op'd by
         *  `AlphaShape_Hooks`). Runs once before the pass loop so every
         *  subsequent tile/sprite shader samples the up-to-date alpha
         *  state.
         */
        Vinifera::Gfx::SpriteQueue::Get().Flush_Alpha_Lights(*Vinifera::Gfx::Device);

        /**
         *  Size + bind + clear `SidebarRT` so the queue passes below can
         *  paint the sidebar's Sidebar-bucketed commands (cameos, frames,
         *  primitives) directly into it. The Compose call after the queue
         *  flushes blits `SidebarRT` onto `SceneRT` at the sidebar's screen
         *  position.
         */
        SDL_Prepare_Sidebar_RT(scale_mode);

        /**
         *  Flush GPU queues populated by patched Draw_Tile / Draw_Shape
         *  callsites during the game's render pass.
         */
        for (int pass = 0; pass < (int)Vinifera::Gfx::RenderPass::Count; ++pass) {
            const auto render_pass = (Vinifera::Gfx::RenderPass)pass;

            /**
             *  Capture SceneCopy explicitly at the start of PostEffects,
             *  AFTER all the pre-PostEffects passes have written their
             *  content (terrain, shroud, buildings, ObjectLayer units) but
             *  BEFORE any predator/distortion sampling and BEFORE UI
             *  overlays. Predator units sampling SceneCopy then see the
             *  fully-rendered tactical scene without UI bleed-through.
             *
             *  VoxelQueue's and DistortionQueue's own `Ensure_Copied` calls
             *  later in the pass become no-ops (per-frame idempotent).
             */
            if (render_pass == Vinifera::Gfx::RenderPass::PostEffects) {
                Vinifera::Gfx::SceneCopy::Get().Ensure_Copied(*Vinifera::Gfx::Device);
            }

            Vinifera::Gfx::TileQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, render_pass);
            Vinifera::Gfx::SpriteQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, render_pass);
            Vinifera::Gfx::VoxelQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, render_pass);
            /**
             *  Drain the deferred composite-unit queue right after the regular
             *  voxel/sprite flushes for this pass. By now terrain depth +
             *  ObjectLayer voxels/sprites are all on the scene RT, so each
             *  unit's scratch-composite blit gets correct terrain occlusion
             *  and lands above same-pass content in the per-pixel SV_Depth
             *  order. Only ObjectLayer carries composite captures today;
             *  the function is a no-op for other passes (empty queue).
             */
            if (render_pass == Vinifera::Gfx::RenderPass::ObjectLayer) {
                Composite_Process_Deferred(*Vinifera::Gfx::Device);
            }
            /**
             *  Composite the radar minimap (or ingame movie) onto
             *  `SidebarRT` between the cameo (sprite) and view-box /
             *  border (primitive) flushes so the layer order is
             *  `RadarAnim` frame → radar/movie pixels → rectangles.
             *  Both textures are kept current by hooks in
             *  `radarext_hooks.cpp`; `RadarMode` is mutually exclusive
             *  between `RMODE_TACTICAL` and `RMODE_MOVIE`.
             */
            if (render_pass == Vinifera::Gfx::RenderPass::UiOverlay) {
                if (Map.RadarMode == RadarClass::RMODE_TACTICAL
                    && Map.RadarState == RadarClass::RSTATE_ACTIVE) {
                    Vinifera::Gfx::Device->Bind_Sidebar_Target();
                    Vinifera::Gfx::Device->Draw_Radar_To_Sidebar(Map.RadarRect);
                } else if (Map.RadarMode == RadarClass::RMODE_MOVIE
                           && IngameVQ.Count() > 0
                           && IngameVQ[0] != nullptr) {
                    Vinifera::Gfx::Device->Bind_Sidebar_Target();
                    Vinifera::Gfx::Device->Draw_Sidebar_Movie(IngameVQ[0]->StretchRect);
                }
            }
            Vinifera::Gfx::PrimitiveQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, render_pass);
            /**
             *  WaveQueue (sonic + laser shockwaves) runs BEFORE the tactical
             *  line queue so that LaserDrawClass's thin inner beam — which
             *  vanilla pairs with `WAVE_BIG_LASER` / `WAVE_LASER` for
             *  `IsLaser=true` weapons — lands on top of the wide shockwave
             *  glow. Wave shaders write opaque, so any line drawn beforehand
             *  gets overwritten; flipping the order makes the line visible.
             */
            Vinifera::Gfx::WaveQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, (int)render_pass);
            Vinifera::Gfx::SpotLightQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, (int)render_pass);
            Vinifera::Gfx::TacticalLineQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, render_pass);
            Vinifera::Gfx::FontQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, render_pass);
            Vinifera::Gfx::DistortionQueue::Get().Flush_Pass(*Vinifera::Gfx::Device, render_pass);
        }
        Vinifera::Gfx::TileQueue::Get().Clear();
        Vinifera::Gfx::SpriteQueue::Get().Clear();
        Vinifera::Gfx::VoxelQueue::Get().Clear();
        Vinifera::Gfx::PrimitiveQueue::Get().Clear();
        Vinifera::Gfx::TacticalLineQueue::Get().Clear();
        Vinifera::Gfx::FontQueue::Get().Clear();
        Vinifera::Gfx::DistortionQueue::Get().Clear();
        Vinifera::Gfx::WaveQueue::Get().Clear();
        Vinifera::Gfx::SpotLightQueue::Get().Clear();
        Vinifera::Gfx::Reset_Current_Render_Pass();

        SDL_Draw_Sidebar_RT_Compose(scale_mode);

        /**
         *  SceneRT is at vanilla's logical render resolution; Draw_Texture
         *  sets the viewport to the backbuffer rect and the point-clamp
         *  sampler upscales to display size.
         */
        Vinifera::Gfx::Device->Bind_Backbuffer_Color_Only();
        Rect scene_dst(0, 0,
            Vinifera::Gfx::Device->Get_Backbuffer_Width(),
            Vinifera::Gfx::Device->Get_Backbuffer_Height());
        Vinifera::Gfx::Device->Draw_Texture(
            Vinifera::Gfx::Device->Get_Scene_SRV(),
            scene_dst,
            SDL_SCALEMODE_NEAREST);
    } else {
        /**
         *  CPU presentation path: VQA, main menu, map selection, score
         *  screen, escape menu, generic dialogs. The source can be either
         *  logical-res (HiddenSurface) or window-res (VisibleSurface);
         *  `Upload_Surface` lazily resizes the upload texture to whichever
         *  dims the caller passes, and `Draw_Surface` always targets the
         *  full backbuffer so logical-res inputs get GPU-upscaled.
         */
        SDLSurface* sdl_surface = static_cast<SDLSurface*>(surface);
        void* pixels = sdl_surface->Lock();
        if (pixels != nullptr) {
            Vinifera::Gfx::Device->Upload_Surface(
                pixels, sdl_surface->Stride(),
                sdl_surface->Get_Width(), sdl_surface->Get_Height());
            sdl_surface->Unlock();
        }
        Vinifera::Gfx::Device->Bind_Backbuffer_Color_Only();
        Rect cpu_dst(0, 0,
            Vinifera::Gfx::Device->Get_Backbuffer_Width(),
            Vinifera::Gfx::Device->Get_Backbuffer_Height());
        Vinifera::Gfx::Device->Draw_Surface(cpu_dst, scale_mode);
    }

    /**
     *  OwnerDraw overlay — `OwnerDrawVisible` is a window-res CPU surface
     *  containing the dialog widgets (filled by the patched OwnerDraw
     *  WindowProc callbacks). Upload + draw 1:1 over the backbuffer so
     *  dialog content stays sharp regardless of the logical-res upscale
     *  underneath. Gated on vanilla's `_dialog_count` global so the
     *  surface's stale / uninitialised pixels don't blank the screen
     *  outside of dialogs.
     */
    {
        const int dialog_count = *reinterpret_cast<const int*>(0x007E492C);
        if (dialog_count > 0 && OwnerDrawVisible != nullptr) {
            void* od_pixels = OwnerDrawVisible->Lock();
            if (od_pixels != nullptr) {
                Vinifera::Gfx::Device->Upload_OwnerDraw_Surface(
                    od_pixels, OwnerDrawVisible->Stride(),
                    OwnerDrawVisible->Get_Width(), OwnerDrawVisible->Get_Height());
                OwnerDrawVisible->Unlock();
                Vinifera::Gfx::Device->Bind_Backbuffer_Color_Only();
                Rect od_dst(0, 0,
                    Vinifera::Gfx::Device->Get_Backbuffer_Width(),
                    Vinifera::Gfx::Device->Get_Backbuffer_Height());
                Vinifera::Gfx::Device->Draw_OwnerDraw_Overlay(od_dst);
            }
        }
    }

    /**
     *  Draw overlays, then present. ImGui runs last in both paths so perf
     *  and debug panels stay visible across tactical, menus, and dialogs.
     */
    ViniferaImGui::Render();

    Vinifera::Gfx::Device->End_Frame();
    Vinifera::Gfx::PerfMonitor::Get().End_Frame();

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
    return WSDialogCount == 0 && SpecialDialog == SDLG_NONE;
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
        Vinifera::Gfx::SurfaceTargetRegistry::Get().Unbind(VisibleSurface);
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
