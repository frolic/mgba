/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/renderers/virtual-sprites.h>

#include <mgba/internal/gba/renderers/video-software.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define NOW_MS() emscripten_get_now()
#else
#define NOW_MS() 0.0
#endif

// Sprites are bucketed by their top row, offset so a sprite starting above
// the screen still gets a bucket.
#define BUCKET_OFFSET GBA_VIRTUAL_SPRITE_HEIGHT
#define BUCKET_COUNT (GBA_VIDEO_VERTICAL_PIXELS + BUCKET_OFFSET)

struct VirtualSprite {
	int16_t x;
	int16_t y;
	uint16_t frame;
	uint8_t priority;
	bool hFlip;
};

static struct {
	uint8_t* pixels;
	// One bit per opaque pixel, per row of each frame, so rows of a frame
	// that are fully transparent cost nothing.
	uint16_t* rowMasks;
	mColor* palettes;
	size_t frameCount;

	struct VirtualSprite* sprites;
	size_t spriteCount;
	size_t spriteCapacity;
	// Sprite indices sorted by bucket, and where each bucket starts.
	uint32_t* order;
	uint32_t bucketStart[BUCKET_COUNT + 1];

	bool changed;
	double frameMs;
	double lastFrameMs;
} state;

bool GBAVirtualSpritesSetFrames(const uint8_t* pixels, const uint16_t* palettes, size_t count) {
	uint8_t* newPixels = malloc(count * GBA_VIRTUAL_SPRITE_PIXELS);
	uint16_t* newMasks = malloc(count * GBA_VIRTUAL_SPRITE_HEIGHT * sizeof(uint16_t));
	mColor* newPalettes = malloc(count * 16 * sizeof(mColor));
	if (count && (!newPixels || !newMasks || !newPalettes)) {
		free(newPixels);
		free(newMasks);
		free(newPalettes);
		return false;
	}
	memcpy(newPixels, pixels, count * GBA_VIRTUAL_SPRITE_PIXELS);
	size_t row;
	for (row = 0; row < count * GBA_VIRTUAL_SPRITE_HEIGHT; ++row) {
		uint16_t mask = 0;
		int x;
		for (x = 0; x < GBA_VIRTUAL_SPRITE_WIDTH; ++x) {
			if (newPixels[row * GBA_VIRTUAL_SPRITE_WIDTH + x] & 0xF) {
				mask |= 1 << x;
			}
		}
		newMasks[row] = mask;
	}
	size_t i;
	for (i = 0; i < count * 16; ++i) {
		newPalettes[i] = mColorFrom555(palettes[i]);
	}
	free(state.pixels);
	free(state.rowMasks);
	free(state.palettes);
	state.pixels = newPixels;
	state.rowMasks = newMasks;
	state.palettes = newPalettes;
	state.frameCount = count;
	state.changed = true;
	return true;
}

bool GBAVirtualSpritesSet(const int16_t* entries, size_t count) {
	if (count > state.spriteCapacity) {
		struct VirtualSprite* sprites = realloc(state.sprites, count * sizeof(*sprites));
		if (!sprites) {
			return false;
		}
		state.sprites = sprites;
		uint32_t* order = realloc(state.order, count * sizeof(*order));
		if (!order) {
			return false;
		}
		state.order = order;
		state.spriteCapacity = count;
	}

	size_t kept = 0;
	size_t i;
	for (i = 0; i < count; ++i) {
		const int16_t* entry = &entries[i * 4];
		int16_t x = entry[0];
		int16_t y = entry[1];
		uint16_t frame = entry[2];
		if (frame >= state.frameCount || x <= -GBA_VIRTUAL_SPRITE_WIDTH || x >= GBA_VIDEO_HORIZONTAL_PIXELS ||
		    y <= -GBA_VIRTUAL_SPRITE_HEIGHT || y >= GBA_VIDEO_VERTICAL_PIXELS) {
			continue;
		}
		state.sprites[kept] = (struct VirtualSprite) { x, y, frame, entry[3] & 3, (entry[3] & 4) != 0 };
		++kept;
	}
	state.spriteCount = kept;

	// Counting sort by top row.
	memset(state.bucketStart, 0, sizeof(state.bucketStart));
	for (i = 0; i < kept; ++i) {
		++state.bucketStart[state.sprites[i].y + BUCKET_OFFSET + 1];
	}
	int bucket;
	for (bucket = 1; bucket <= BUCKET_COUNT; ++bucket) {
		state.bucketStart[bucket] += state.bucketStart[bucket - 1];
	}
	uint32_t next[BUCKET_COUNT];
	memcpy(next, state.bucketStart, sizeof(next));
	for (i = 0; i < kept; ++i) {
		state.order[next[state.sprites[i].y + BUCKET_OFFSET]++] = i;
	}
	state.changed = true;
	return true;
}

bool GBAVirtualSpritesTakeChanged(void) {
	bool changed = state.changed;
	state.changed = false;
	return changed;
}

double GBAVirtualSpritesLastFrameMs(void) {
	return state.lastFrameMs;
}

// The scanline being drawn, and the next bucket and sprite to draw on it.
// Buckets go from the bottom of the screen up, so sprites come front to
// back, the order real sprites are drawn in.
static struct {
	int y;
	int bucket;
	int firstBucket;
	uint32_t index;
	double startMs;
} cursor;

void GBAVirtualSpritesBeginScanline(int y) {
	cursor.y = y;
	cursor.bucket = y + BUCKET_OFFSET;
	cursor.firstBucket = cursor.bucket - GBA_VIRTUAL_SPRITE_HEIGHT + 1;
	if (cursor.firstBucket < 0) {
		cursor.firstBucket = 0;
	}
	cursor.index = state.spriteCount ? state.bucketStart[cursor.bucket] : 0;
}

static void _drawRow(struct GBAVideoSoftwareRenderer* renderer, const struct VirtualSprite* sprite, int y) {
	int row = y - sprite->y;
	size_t frameRow = (size_t) sprite->frame * GBA_VIRTUAL_SPRITE_HEIGHT + row;
	uint16_t mask = state.rowMasks[frameRow];
	if (!mask) {
		return;
	}
	const uint8_t* pixels = &state.pixels[frameRow * GBA_VIRTUAL_SPRITE_WIDTH];
	const mColor* palette = &state.palettes[sprite->frame * 16];
	uint32_t flags = (uint32_t) sprite->priority << OFFSET_PRIORITY;
	int x;
	for (x = 0; x < GBA_VIRTUAL_SPRITE_WIDTH; ++x) {
		int sourceX = sprite->hFlip ? GBA_VIRTUAL_SPRITE_WIDTH - 1 - x : x;
		if (!(mask & (1 << sourceX))) {
			continue;
		}
		int outX = sprite->x + x;
		if (outX < 0 || outX >= GBA_VIDEO_HORIZONTAL_PIXELS) {
			continue;
		}
		uint32_t current = renderer->spriteLayer[outX];
		if ((current & FLAG_ORDER_MASK) > flags) {
			renderer->spriteLayer[outX] = palette[pixels[sourceX] & 0xF] | flags;
		}
	}
}

int GBAVirtualSpritesDrawUntil(struct GBAVideoSoftwareRenderer* renderer, int bottom) {
	if (!state.spriteCount) {
		return 0;
	}
	double start = NOW_MS();
	int layers = 0;
	for (; cursor.bucket >= cursor.firstBucket; --cursor.bucket) {
		int spriteBottom = cursor.bucket - BUCKET_OFFSET + GBA_VIRTUAL_SPRITE_HEIGHT;
		if (spriteBottom <= bottom) {
			break;
		}
		uint32_t end = state.bucketStart[cursor.bucket + 1];
		for (; cursor.index < end; ++cursor.index) {
			const struct VirtualSprite* sprite = &state.sprites[state.order[cursor.index]];
			_drawRow(renderer, sprite, cursor.y);
			layers |= 1 << sprite->priority;
		}
		if (cursor.bucket > cursor.firstBucket) {
			cursor.index = state.bucketStart[cursor.bucket - 1];
		}
	}
	state.frameMs += NOW_MS() - start;
	return layers;
}

void GBAVirtualSpritesEndScanline(void) {
	if (cursor.y == GBA_VIDEO_VERTICAL_PIXELS - 1) {
		state.lastFrameMs = state.frameMs;
		state.frameMs = 0;
	}
}
