/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  SHP loader producing a paletted GPU atlas.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "shp_asset.h"

#include "ccfile.h"
#include "debughandler.h"
#include "graphics_device.h"

#include <algorithm>
#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Mirrors `class ShapeSet` in Tiberian-Sun/code/shapeset.h with
         *  #pragma pack(push, 4). Defined locally so we don't depend on the
         *  hooker-touched TS headers.
         */
#pragma pack(push, 4)
        struct ShpHeader
        {
            int16_t Flags;
            int16_t Width;
            int16_t Height;
            int16_t Count;
        };

        struct ShpRecord
        {
            int16_t X;
            int16_t Y;
            int16_t Width;
            int16_t Height;
            int16_t Flags;
            uint8_t pad[2];
            uint8_t Color[3];
            uint8_t Unused[5];
            int32_t Data;        // absolute file offset
        };
#pragma pack(pop)

        static_assert(sizeof(ShpHeader) == 8, "ShpHeader layout broken");
        static_assert(sizeof(ShpRecord) == 24, "ShpRecord layout broken");

        constexpr int16_t SFLAG_TRANSPARENT = 0x01;
        constexpr int16_t SFLAG_RLE         = 0x02;

        constexpr int kAtlasMaxWidth  = 2048;
        constexpr int kAtlasPad       = 1;     // guard pixel between frames
    }


    const ShpFrameInfo* ShpAsset::Get_Frame(int index) const
    {
        if (index < 0 || index >= (int)Frames.size()) {
            return nullptr;
        }
        return &Frames[index];
    }


    bool ShpAsset::Decompress_Line_RLE(const uint8_t*& src, const uint8_t* src_end, uint8_t* row_dst, int row_w)
    {
        /**
         *  Per-row layout from RLEEngine::Line_Decompress: uint16 row_length,
         *  followed by `row_length - 2` bytes of RLE data using the
         *  zero-run encoding (a non-zero byte is output as-is; a zero byte is
         *  followed by a uint8 count of zeros to emit).
         */
        if (src + 2 > src_end) {
            return false;
        }
        const uint16_t row_length = (uint16_t)(src[0] | (src[1] << 8));
        src += 2;
        if (row_length < 2) {
            return false;
        }
        const uint8_t* row_end = src + (row_length - 2);
        if (row_end > src_end) {
            return false;
        }

        int written = 0;
        while (src < row_end && written < row_w) {
            const uint8_t v = *src++;
            if (v == 0) {
                if (src >= row_end) break;
                int count = *src++;
                if (count > row_w - written) count = row_w - written;
                memset(row_dst + written, 0, (size_t)count);
                written += count;
            } else {
                row_dst[written++] = v;
            }
        }

        /**
         *  If the row underran (data ended early), pad with transparent (0).
         */
        if (written < row_w) {
            memset(row_dst + written, 0, (size_t)(row_w - written));
        }

        /**
         *  Skip any leftover bytes inside the row block — defensive.
         */
        src = row_end;
        return true;
    }


    bool ShpAsset::Load(GraphicsDevice& device, const char* shp_path)
    {
        Unload();
        if (shp_path == nullptr || *shp_path == '\0') {
            return false;
        }

        CCFileClass file(shp_path);
        if (!file.Is_Available()) {
            DEBUG_ERROR("ShpAsset: '%s' not found in any mix.\n", shp_path);
            return false;
        }
        if (!file.Open(FILE_ACCESS_READ)) {
            DEBUG_ERROR("ShpAsset: failed to open '%s'.\n", shp_path);
            return false;
        }

        const off_t size = file.Size();
        if (size < (off_t)sizeof(ShpHeader)) {
            file.Close();
            return false;
        }

        std::vector<uint8_t> blob((size_t)size);
        const long read_bytes = file.Read(blob.data(), (int)size);
        file.Close();
        if (read_bytes != (long)size) {
            DEBUG_ERROR("ShpAsset: short read on '%s'.\n", shp_path);
            return false;
        }

        if (!Load_From_Memory(device, blob.data(), blob.size(), shp_path)) {
            return false;
        }
        SourcePath = shp_path;
        return true;
    }


    bool ShpAsset::Load_From_Memory(GraphicsDevice& device, const void* blob_ptr, size_t blob_size,
                                    const char* debug_name)
    {
        Unload();
        if (blob_ptr == nullptr) {
            return false;
        }
        if (debug_name == nullptr) debug_name = "<memory>";
        const uint8_t* blob = static_cast<const uint8_t*>(blob_ptr);

        if (blob_size != 0 && blob_size < sizeof(ShpHeader)) {
            return false;
        }

        const ShpHeader* hdr = reinterpret_cast<const ShpHeader*>(blob);
        const int frame_count = hdr->Count;
        if (frame_count <= 0 || frame_count > 4096) {
            DEBUG_ERROR("ShpAsset: '%s' has bad frame count %d.\n", debug_name, frame_count);
            return false;
        }

        LogicalWidth = hdr->Width;
        LogicalHeight = hdr->Height;

        const ShpRecord* records = reinterpret_cast<const ShpRecord*>(blob + sizeof(ShpHeader));

        /**
         *  If blob_size is unknown, derive a safe upper bound from the records:
         *  the highest data offset plus a generous slack. Used only for the
         *  per-frame "out of bounds" sanity check.
         */
        size_t safe_size = blob_size;
        if (safe_size == 0) {
            size_t high = sizeof(ShpHeader) + (size_t)frame_count * sizeof(ShpRecord);
            for (int i = 0; i < frame_count; ++i) {
                if (records[i].Data > 0 && (size_t)records[i].Data > high) {
                    high = (size_t)records[i].Data
                         + (size_t)records[i].Width * (size_t)records[i].Height;
                }
            }
            /* Add slack for RLE expansion overhead; pessimistic but bounded. */
            safe_size = high + (1u << 20);
        }
        if ((size_t)sizeof(ShpHeader) + (size_t)frame_count * sizeof(ShpRecord) > safe_size) {
            return false;
        }

        Frames.resize(frame_count);

        /**
         *  Pass 1: decompress each frame into a per-frame uint8 buffer; track
         *  per-frame size; lay out into an atlas via simple shelf packing.
         */
        std::vector<std::vector<uint8_t>> frame_pixels(frame_count);

        int atlas_x = 0;
        int atlas_y = 0;
        int row_h = 0;
        int atlas_w = 0;

        for (int i = 0; i < frame_count; ++i) {
            const ShpRecord& rec = records[i];
            ShpFrameInfo& fi = Frames[i];
            fi.X = rec.X;
            fi.Y = rec.Y;
            fi.W = rec.Width;
            fi.H = rec.Height;
            fi.Transparent = (rec.Flags & SFLAG_TRANSPARENT) != 0;
            fi.RLE = (rec.Flags & SFLAG_RLE) != 0;

            if (fi.W <= 0 || fi.H <= 0 || rec.Data == 0) {
                fi.W = 0;
                fi.H = 0;
                continue;
            }

            const size_t frame_size = (size_t)fi.W * (size_t)fi.H;
            std::vector<uint8_t>& buf = frame_pixels[i];
            buf.resize(frame_size, 0);

            if ((size_t)rec.Data >= safe_size) {
                DEBUG_ERROR("ShpAsset: '%s' frame %d data offset out of bounds.\n", debug_name, i);
                continue;
            }

            const uint8_t* src = blob + rec.Data;
            const uint8_t* src_end = blob + safe_size;

            if (fi.RLE) {
                for (int y = 0; y < fi.H; ++y) {
                    if (!Decompress_Line_RLE(src, src_end, buf.data() + y * fi.W, fi.W)) {
                        DEBUG_WARNING("ShpAsset: '%s' frame %d row %d decompress short.\n", debug_name, i, y);
                        break;
                    }
                }
            } else {
                const size_t available = (size_t)(src_end - src);
                const size_t copy = available < frame_size ? available : frame_size;
                memcpy(buf.data(), src, copy);
            }

            const int placed_w = fi.W + kAtlasPad;
            if (atlas_x + placed_w > kAtlasMaxWidth && atlas_x > 0) {
                atlas_x = 0;
                atlas_y += row_h + kAtlasPad;
                row_h = 0;
            }
            fi.AtlasX = atlas_x;
            fi.AtlasY = atlas_y;
            atlas_x += placed_w;
            if (fi.H > row_h) row_h = fi.H;
            if (atlas_x > atlas_w) atlas_w = atlas_x;
        }

        const int atlas_h = atlas_y + row_h;
        if (atlas_w <= 0 || atlas_h <= 0) {
            DEBUG_ERROR("ShpAsset: '%s' yielded an empty atlas.\n", debug_name);
            return false;
        }

        /**
         *  Pass 2: assemble the atlas pixel buffer and upload as R8_UINT.
         */
        std::vector<uint8_t> atlas_pixels((size_t)atlas_w * (size_t)atlas_h, 0);
        for (int i = 0; i < frame_count; ++i) {
            const ShpFrameInfo& fi = Frames[i];
            if (fi.W <= 0 || fi.H <= 0) continue;
            const std::vector<uint8_t>& src = frame_pixels[i];
            for (int y = 0; y < fi.H; ++y) {
                memcpy(&atlas_pixels[(fi.AtlasY + y) * atlas_w + fi.AtlasX],
                       &src[y * fi.W],
                       (size_t)fi.W);
            }
        }

        if (!Atlas.Initialize(device, atlas_w, atlas_h, DXGI_FORMAT_R8_UINT,
                              D3D11_USAGE_DEFAULT, atlas_pixels.data(), atlas_w)) {
            DEBUG_ERROR("ShpAsset: '%s' failed to create atlas texture (%dx%d).\n", debug_name, atlas_w, atlas_h);
            return false;
        }

        SourcePath = debug_name;
        DEBUG_INFO("ShpAsset: '%s' loaded — %d frames, atlas %dx%d.\n",
            debug_name, frame_count, atlas_w, atlas_h);
        return true;
    }


    void ShpAsset::Unload()
    {
        Atlas.Shutdown();
        Frames.clear();
        LogicalWidth = 0;
        LogicalHeight = 0;
        SourcePath.clear();
    }
}
