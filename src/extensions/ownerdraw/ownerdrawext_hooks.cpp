/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Redirects OwnerDraw / dialog code from vanilla's logical-res
 *          `AlternateSurface` / `VisibleSurface` / `VideoModeWidth/Height`
 *          globals to the window-res `OwnerDrawAlternate` /
 *          `OwnerDrawVisible` / `OwnerDrawWidth/Height` so dialog widgets
 *          render and hit-test at true window pixels.
 *
 *          Each line below rewrites a single `mov`/`cmp` instruction's
 *          imm32 operand. Encoding offsets:
 *              A1 imm32         mov  eax, ds:[imm32]   -> +1
 *              A3 imm32         mov  ds:[imm32], eax   -> +1
 *              8B /r imm32      mov  reg, ds:[imm32]   -> +2
 *              39 /r imm32      cmp  ds:[imm32], reg   -> +2
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "ownerdrawext_hooks.h"

#include "hooker.h"
#include "sdlsurface.h"


SDLSurface* OwnerDrawAlternate = nullptr;
SDLSurface* OwnerDrawVisible = nullptr;

int OwnerDrawWidth = 0;
int OwnerDrawHeight = 0;


void OwnerDraw_Hooks()
{
    /**
     *  AlternateSurface (0x0074C5E0) -> &OwnerDrawAlternate.
     *  Vanilla OwnerDraw uses AlternateSurface as a scratch (paints,
     *  then blits to VisibleSurface). With our split, that scratch
     *  lives at window res in OwnerDrawAlternate.
     */
    // OwnerDraw paint helpers (ownrdraw.cpp)
    Patch_Dword(0x0059F1F7 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate)); // ODDrawText
    Patch_Dword(0x0059F364 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate)); // OwnerDraw::DrawItem
    Patch_Dword(0x0059F74E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate)); // ODDrawDimmedBackground
    Patch_Dword(0x005A0365 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate)); // OwnerDraw::DrawDialogBack

    // ComboDropWinCtrlProc
    Patch_Dword(0x0058FBF1 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0058FC3C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0058FD90 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0058FEFB + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0058FF3B + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0058FFC8 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0058FFF1 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // CtrlProc (top-level dialog message router)
    Patch_Dword(0x00593F5E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00593F7E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00593F8D + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005940D1 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005940F3 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059410C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059434A + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059436E + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059437C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059446E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594490 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059449F + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // ButtonCtrlProc
    Patch_Dword(0x005949DF + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594A59 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594B8B + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594C53 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594CE3 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594D7A + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594E1D + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00594E46 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // TextBoxCtrlProc
    Patch_Dword(0x00595140 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00595194 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005951F6 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00595254 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005952AE + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00595317 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059537C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005953EB + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005955A3 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00595675 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059573F + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005957C5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // EditBoxCtrlProc
    Patch_Dword(0x00595D9A + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // StaticCtrlProc
    Patch_Dword(0x00596120 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059643E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005964CE + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // CheckBoxCtrlProc
    Patch_Dword(0x0059696A + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059699D + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005969EE + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // ComboBoxCtrlProc
    Patch_Dword(0x0059703C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00597052 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005970A4 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005970D4 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00597270 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // ListBoxCtrlProc
    Patch_Dword(0x00597B29 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00597CA5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00597E0E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00597F1D + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x00598203 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // ScrollBarCtrlProc
    Patch_Dword(0x0059ACC0 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059ACE5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AD78 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059ADE1 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AE26 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AE81 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AED4 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AEE6 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AF37 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AF90 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059AFA2 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // ProgressBarCtrlProc
    Patch_Dword(0x0059B68F + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059B69D + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059B70E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // TrackBarCtrlProc
    Patch_Dword(0x0059BBC5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059BC63 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059BCB2 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059BD2F + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059BDCF + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059BE73 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059BF96 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059BFB6 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C048 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // GroupBoxCtrlProc
    Patch_Dword(0x0059C4CE + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C4E5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C4F1 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C500 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C5B1 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C5C5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C642 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C67F + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C6B5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C6DF + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C715 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // HotkeyCtrlProc
    Patch_Dword(0x0059C8F5 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C929 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C943 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x0059C99A + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // Sync-frame bars on multiplayer connection dialog
    Patch_Dword(0x005B3BA0 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));
    Patch_Dword(0x005B3BBC + 2, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate));

    // RestateMission custom button (logical content stays; only the
    // widget itself is redirected).
    Patch_Dword(0x005C1165 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawAlternate)); // MyButton::Draw

    /**
     *  VisibleSurface (0x0074C5D8) -> &OwnerDrawVisible.
     *  OwnerDraw widgets blit their final paint onto VisibleSurface; with
     *  VisibleSurface now at logical res, route those blits to the
     *  parallel window-res `OwnerDrawVisible` instead.
     */
    // OwnerDraw paint helpers
    Patch_Dword(0x0059F350 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawVisible)); // OwnerDraw::DrawItem
    Patch_Dword(0x0059F61B + 1, reinterpret_cast<uintptr_t>(&OwnerDrawVisible)); // ODDrawDimmedBackground

    // Tooltip system
    Patch_Dword(0x00591F1C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible)); // StartTooltip
    Patch_Dword(0x00592081 + 1, reinterpret_cast<uintptr_t>(&OwnerDrawVisible)); // ShowTooltip
    Patch_Dword(0x0059212F + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00592140 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00592151 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059215C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059216F + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x005921B5 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059225C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible)); // HideTooltip
    Patch_Dword(0x005922E6 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible)); // EndTooltip

    // ComboDropWinCtrlProc
    Patch_Dword(0x0058FF4C + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0058FFD1 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0058FFE6 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));

    // CtrlProc
    Patch_Dword(0x00592F63 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x005930EC + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059328B + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00593352 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059351E + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00593701 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x005938CD + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00593F51 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00593F6D + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00593F98 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00594025 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x005940B8 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x005940E2 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00594101 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059419B + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059431D + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059435B + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00594387 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x00594459 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x0059447F + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));
    Patch_Dword(0x005944AA + 2, reinterpret_cast<uintptr_t>(&OwnerDrawVisible));

    /**
     *  VideoModeWidth/Height (0x007A1EC0 / 0x007A1EC4) -> OwnerDrawWidth/Height.
     *  Used by `DrawDialogBack` to centre the backdrop art tile against
     *  the screen area the dialog occupies. The dialog window itself is
     *  positioned in window-pixel space (via the Vinifera
     *  `_Center_Window_Within_Window` / `_ODMoveDialog` replacements),
     *  so the source-rect math has to match.
     */
    Patch_Dword(0x0059FFEA + 2, reinterpret_cast<uintptr_t>(&OwnerDrawWidth));  // DrawDialogBack: cmp VideoModeWidth, eax
    Patch_Dword(0x0059FFF9 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawWidth));  // DrawDialogBack: mov ecx, VideoModeWidth
    Patch_Dword(0x005A0021 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawHeight)); // DrawDialogBack: cmp VideoModeHeight, eax
    Patch_Dword(0x005A0030 + 2, reinterpret_cast<uintptr_t>(&OwnerDrawHeight)); // DrawDialogBack: mov ecx, VideoModeHeight
}
