/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 4: neutralize vanilla's CPU AlphaShape blits.
 *
 *          The GPU pipeline reproduces the alpha-light effect via a UAV pass
 *          in `SpriteQueue::Flush_Alpha_Lights`. The CPU `Draw_In_Area` and
 *          `Draw_All` methods on `AlphaShapeClass` are no-op'd at their
 *          entry points so vanilla's CPU `AlphaBuffer` no longer receives
 *          duplicate alpha-light contributions.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

void AlphaShape_Hooks();
