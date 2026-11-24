/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_VIRTUAL_SPRITES_H
#define GBA_VIRTUAL_SPRITES_H

#include <mgba-util/common.h>

CXX_GUARD_START

// Sprites an embedder draws among the game's own, outside OAM and VRAM, so
// the GBA's 128-sprite and per-scanline limits don't apply and nothing
// reaches game memory or save states. The software renderer draws them in
// its sprite pass with the same background priority bits, ordered among the
// real sprites by how low on screen their feet are.
//
// Every frame is 16x32 pixels, as overworld characters are. Change the
// sprites only while the core thread waits, such as in a frame callback.

#define GBA_VIRTUAL_SPRITE_WIDTH 16
#define GBA_VIRTUAL_SPRITE_HEIGHT 32
#define GBA_VIRTUAL_SPRITE_PIXELS (GBA_VIRTUAL_SPRITE_WIDTH * GBA_VIRTUAL_SPRITE_HEIGHT)

struct GBAVideoSoftwareRenderer;

// `pixels` holds GBA_VIRTUAL_SPRITE_PIXELS palette indices per frame, row by
// row, with 0 transparent; `palettes` holds 16 BGR555 colors per frame.
bool GBAVirtualSpritesSetFrames(const uint8_t* pixels, const uint16_t* palettes, size_t count);

// `entries` holds four int16 values per sprite: screen x, screen y, frame,
// and flags: background priority (0 to 3) in bits 0 and 1, and a horizontal
// flip in bit 2. Later sprites are drawn behind earlier
// ones on the same row, and sprites lower on screen in front of higher ones.
bool GBAVirtualSpritesSet(const int16_t* entries, size_t count);

// Drawing one scanline: begin it, then before each real sprite that
// crosses it, call GBAVirtualSpritesDrawUntil with that sprite's bottom row
// (exclusive) to draw the virtual sprites whose feet are lower on screen,
// which belong in front of it. Call it again with INT_MIN after the last
// real sprite, then end the scanline. Real sprites are drawn front to back,
// so a virtual sprite on the same row as a real one goes behind it. Each
// call returns a bit per background priority that got pixels, as
// GBAVideoSoftwareRendererPreprocessSpriteLayer does.
void GBAVirtualSpritesBeginScanline(int y);
int GBAVirtualSpritesDrawUntil(struct GBAVideoSoftwareRenderer* renderer, int bottom);
void GBAVirtualSpritesEndScanline(void);

// Whether the sprites changed since the last call, so every scanline has to
// be drawn again.
bool GBAVirtualSpritesTakeChanged(void);

// Milliseconds the renderer spent drawing virtual sprites in the last
// complete frame.
double GBAVirtualSpritesLastFrameMs(void);

CXX_GUARD_END

#endif
