/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Low-level helpers shared by the Vinifera Gfx layer.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "gfx_utils.h"

// Translation unit intentionally otherwise empty -- everything in gfx_utils
// is inline template / RAII at the moment. Kept as a build target so future
// non-template helpers have a place to land.
