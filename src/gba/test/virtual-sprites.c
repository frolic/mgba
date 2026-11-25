/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <mgba/internal/gba/renderers/virtual-sprites.h>
#include <mgba-util/image.h>

#define RED 0x001F
#define BLUE 0x7C00
#define FADE(N) ((N) << 8)

static uint16_t vram[0xC000];
static uint16_t palette[512];
static union GBAOAM oam;
static mColor output[GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS];
static struct GBAVideoSoftwareRenderer renderer;

// A blue backdrop with objects on, and one frame that is red everywhere.
static void _start(void) {
	memset(&renderer, 0, sizeof(renderer));
	GBAVideoSoftwareRendererCreate(&renderer);
	renderer.d.vram = vram;
	renderer.d.palette = palette;
	renderer.d.oam = &oam;
	renderer.outputBuffer = output;
	renderer.outputBufferStride = GBA_VIDEO_HORIZONTAL_PIXELS;
	renderer.d.init(&renderer.d);
	palette[0] = BLUE;
	renderer.d.writePalette(&renderer.d, 0, BLUE);
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_DISPCNT, 0x1000);

	uint8_t pixels[GBA_VIRTUAL_SPRITE_PIXELS];
	memset(pixels, 1, sizeof(pixels));
	uint16_t framePalette[16] = { 0, RED };
	assert_true(GBAVirtualSpritesSetFrames(pixels, framePalette, 1));
}

static void _stop(void) {
	GBAVirtualSpritesSet(NULL, 0);
	renderer.d.deinit(&renderer.d);
}

// Draws a whole frame, since the renderer redraws a cached scanline only
// when a frame starts with changed sprites, and returns one pixel.
static mColor _pixel(int x, int y) {
	int line;
	for (line = 0; line < GBA_VIDEO_VERTICAL_PIXELS; ++line) {
		renderer.d.drawScanline(&renderer.d, line);
	}
	renderer.d.finishFrame(&renderer.d);
	return output[y * GBA_VIDEO_HORIZONTAL_PIXELS + x] & 0x00FFFFFF;
}

M_TEST_DEFINE(opaque) {
	_start();
	int16_t sprites[] = { 10, 10, 0, 0 };
	assert_true(GBAVirtualSpritesSet(sprites, 1));
	assert_int_equal(_pixel(12, 20), mColorFrom555(RED));
	assert_int_equal(_pixel(40, 20), mColorFrom555(BLUE));
	_stop();
}

M_TEST_DEFINE(fade) {
	_start();
	int16_t sprites[] = { 10, 10, 0, FADE(8) };
	assert_true(GBAVirtualSpritesSet(sprites, 1));
	assert_int_equal(_pixel(12, 20), mColorMix5Bit(8, mColorFrom555(RED), 8, mColorFrom555(BLUE)));
	_stop();
}

M_TEST_DEFINE(identicalDrawnOnce) {
	_start();
	int16_t sprites[] = { 10, 10, 0, FADE(8), 10, 10, 0, FADE(8) };
	assert_true(GBAVirtualSpritesSet(sprites, 2));
	assert_int_equal(_pixel(12, 20), mColorMix5Bit(8, mColorFrom555(RED), 8, mColorFrom555(BLUE)));
	_stop();
}

M_TEST_DEFINE(opaqueCoversFaded) {
	_start();
	int16_t sprites[] = { 10, 10, 0, 0, 10, 10, 0, FADE(8) };
	assert_true(GBAVirtualSpritesSet(sprites, 2));
	assert_int_equal(_pixel(12, 20), mColorFrom555(RED));
	_stop();
}

M_TEST_DEFINE(darken) {
	_start();
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_BLDCNT, 0x10 | (3 << 6));
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_BLDY, 16);
	int16_t sprites[] = { 10, 10, 0, 0 };
	assert_true(GBAVirtualSpritesSet(sprites, 1));
	assert_int_equal(_pixel(12, 20), mColorFrom555(0));
	_stop();
}

M_TEST_DEFINE(windows) {
	_start();
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_BLDCNT, 0x10 | (3 << 6));
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_BLDY, 16);
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_DISPCNT, 0x1000 | 0x2000);
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_WIN0H, 16);
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_WIN0V, GBA_VIDEO_VERTICAL_PIXELS);
	// Objects and blending inside window 0, objects only outside it.
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_WININ, 0x30);
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_WINOUT, 0x10);
	int16_t sprites[] = { 8, 10, 0, 0 };
	assert_true(GBAVirtualSpritesSet(sprites, 1));
	assert_int_equal(_pixel(10, 20), mColorFrom555(0));
	assert_int_equal(_pixel(20, 20), mColorFrom555(RED));
	renderer.d.writeVideoRegister(&renderer.d, GBA_REG_WINOUT, 0);
	assert_int_equal(_pixel(20, 20), mColorFrom555(BLUE));
	_stop();
}

M_TEST_SUITE_DEFINE(GBAVirtualSprites,
	cmocka_unit_test(opaque),
	cmocka_unit_test(fade),
	cmocka_unit_test(identicalDrawnOnce),
	cmocka_unit_test(opaqueCoversFaded),
	cmocka_unit_test(darken),
	cmocka_unit_test(windows))
