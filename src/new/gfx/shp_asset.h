/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  SHP (Tiberian Sun shape-set) loader producing a paletted GPU atlas.
 *
 *          Reads the on-disk ShapeSet header + per-frame ShapeRecord array,
 *          decompresses any RLE-encoded frames (zero-run RLE with 16-bit
 *          per-row length prefixes), and packs all frames into a single
 *          R8_UINT atlas texture (paletted indices). Each frame's UV rect in
 *          the atlas is tracked alongside the per-frame X/Y offset so callers
 *          can position frames the same way Draw_Shape would.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct ShpFrameInfo
    {
        int  X = 0;          // origin X relative to logical (0,0)
        int  Y = 0;          // origin Y relative to logical (0,0)
        int  W = 0;          // frame width in pixels
        int  H = 0;          // frame height in pixels
        int  AtlasX = 0;     // pixel offset of this frame in the atlas
        int  AtlasY = 0;
        bool Transparent = false;
        bool RLE = false;
    };


    class ShpAsset
    {
    public:
        ShpAsset() = default;
        ~ShpAsset() = default;

        ShpAsset(const ShpAsset&) = delete;
        ShpAsset& operator=(const ShpAsset&) = delete;

        /**
         *  Load `shp_path` via the game's virtual filesystem, decompress all
         *  frames, build a single R8_UINT atlas Texture2D, and populate the
         *  per-frame info table. Returns false on read or upload failure.
         */
        bool Load(GraphicsDevice& device, const char* shp_path);

        /**
         *  Load directly from an already-resident SHP blob (e.g. a pointer
         *  vanilla TS handed us via the Draw_Shape proxy). The blob layout
         *  matches Tiberian-Sun ShapeSet — header followed by N records and
         *  per-frame data referenced by absolute offsets from the blob start.
         *  `blob_size` may be 0 if unknown; in that case the loader walks the
         *  records to compute a safe upper bound.
         */
        bool Load_From_Memory(GraphicsDevice& device, const void* blob, size_t blob_size,
                              const char* debug_name = "<memory>");

        void Unload();

        bool Is_Loaded() const { return Atlas.Get_SRV() != nullptr; }

        int                 Frame_Count() const { return (int)Frames.size(); }
        int                 Logical_Width() const { return LogicalWidth; }
        int                 Logical_Height() const { return LogicalHeight; }
        const ShpFrameInfo* Get_Frame(int index) const;

        Texture2D&          Get_Atlas() { return Atlas; }
        const Texture2D&    Get_Atlas() const { return Atlas; }

        const std::string&  Source_Path() const { return SourcePath; }

    private:
        bool Decompress_Line_RLE(const uint8_t*& src, const uint8_t* src_end, uint8_t* row_dst, int row_w);

        Texture2D                  Atlas;
        std::vector<ShpFrameInfo>  Frames;
        int                        LogicalWidth = 0;
        int                        LogicalHeight = 0;
        std::string                SourcePath;
    };
}
