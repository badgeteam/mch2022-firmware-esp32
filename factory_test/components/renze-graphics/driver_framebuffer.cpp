/*
 * BADGE.TEAM framebuffer driver
 * Uses parts of the Adafruit GFX Arduino libray
 * Renze Nicolai 2019
 */

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#include "include/driver_framebuffer_internal.h"
#define TAG "fb"

static uint8_t* framebuffer;

/* Color space conversions */

inline uint16_t convert24to16(uint32_t in) //RGB24 to 565
{
	uint8_t b = (in>>16)&0xFF;
	uint8_t r = in&0xFF;
	uint8_t g = (in>>8)&0xFF;
	return ((b & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (r >> 3);
}

inline uint8_t convert24to8C(uint32_t in) //RGB24 to 256-color
{
	uint8_t b = ((in>>16)&0xFF) >> 5;
	uint8_t r = ( in     &0xFF) >> 6;
	uint8_t g = ((in>> 8)&0xFF) >> 5;
	return r | (g<<3) | (b<<6);
}

inline uint32_t convert8Cto24(uint8_t in) //256-color to RGB24
{
	uint8_t r = in & 0x07;
	uint8_t b = in >> 6;
	uint8_t g = (in>>3) & 0x07;
	return b | (g << 8) | (r << 16);
}

inline uint8_t convert24to8(uint32_t in) //RGB24 to 8-bit greyscale
{
	uint8_t r = (in>>16)&0xFF;
	uint8_t b = in&0xFF;
	uint8_t g = (in>>8)&0xFF;
	return ( r + g + b + 1 ) / 3;
}

inline bool convert8to1(uint8_t in) //8-bit greyscale to black&white
{
	return in >= 128;
}

esp_err_t driver_framebuffer_init(uint8_t* buffer)
{
	static bool driver_framebuffer_init_done = false;
	if (driver_framebuffer_init_done) return ESP_OK;
	ESP_LOGD(TAG, "init called");

    framebuffer = buffer;

	driver_framebuffer_fill(NULL, COLOR_FILL_DEFAULT);

	driver_framebuffer_set_orientation_angle(NULL, 0); //Apply global orientation (needed for flip)
	driver_framebuffer_init_done = true;
	ESP_LOGD(TAG, "init done");
	return ESP_OK;
}

bool _getFrameContext(Window* window, uint8_t** buffer, int16_t* width, int16_t* height)
{
	if (window == NULL) {
		//No window provided, use global context
		*width = FB_WIDTH;
		*height = FB_HEIGHT;
		*buffer = framebuffer;
		if (!framebuffer) {
			ESP_LOGE(TAG, "Framebuffer not allocated!");
			return false;
		}
	} else {
		*width  = window->width;
		*height = window->height;
		*buffer = window->buffer;
	}
	return true;
}

void driver_framebuffer_fill(Window* window, uint32_t value)
{
	uint8_t* buffer;
	int16_t width, height;
	if (!_getFrameContext(window, &buffer, &width, &height)) return;
	if (!window) driver_framebuffer_set_dirty_area(0,0,width-1,height-1, true);
    value = convert24to16(value);
    uint8_t c0 = (value>>8)&0xFF;
    uint8_t c1 = value&0xFF;
    for (uint32_t i = 0; i < width*height*2; i+=2) {
        buffer[i + 0] = c0;
        buffer[i + 1] = c1;
    }
}

void driver_framebuffer_setPixel(Window* window, int16_t x, int16_t y, uint32_t value)
{
	uint8_t* buffer; int16_t width, height;
	if (!_getFrameContext(window, &buffer, &width, &height)) return;
	if (!driver_framebuffer_orientation_apply(window, &x, &y)) return;
	bool changed = false;
    value = convert24to16(value);
    uint8_t c0 = (value>>8)&0xFF;
    uint8_t c1 = value&0xFF;
    uint32_t position = (y * width * 2) + (x * 2);
    if (buffer[position + 0] != c0 || buffer[position + 1] != c1) changed = true;
    buffer[position + 0] = c0;
    buffer[position + 1] = c1;
	if ((!window) && changed) driver_framebuffer_set_dirty_area(x,y,x,y,false);
}

uint32_t driver_framebuffer_getPixel(Window* window, int16_t x, int16_t y)
{
	uint8_t* buffer; int16_t width, height;
	if (!_getFrameContext(window, &buffer, &width, &height)) return 0;
	if (!driver_framebuffer_orientation_apply(window, &x, &y)) return 0;
    uint32_t position = (y * width * 2) + (x * 2);
    uint32_t color = (buffer[position] << 8) + (buffer[position + 1]);
    uint8_t r = ((((color >> 11) & 0x1F) * 527) + 23) >> 6;
    uint8_t g = ((((color >> 5 ) & 0x3F) * 259) + 33) >> 6;
    uint8_t b = ((((color      ) & 0x1F) * 527) + 23) >> 6;
    return r << 16 | g << 8 | b;
}

void driver_framebuffer_blit(Window* source, Window* target)
{
	if (source->vOffset >= source->height) return; //The vertical offset is larger than the height of the window
	if (source->hOffset >= source->width)  return; //The horizontal offset is larger than the width of the window
	for (uint16_t wy = source->vOffset; wy < source->drawHeight; wy++) {
		for (uint16_t wx = source->hOffset; wx < source->drawWidth; wx++) {
			if (wy >= source->height) continue; //Out-of-bounds
			if (wx >= source->width) continue;  //Out-of-bounds
			uint32_t color = driver_framebuffer_getPixel(source, wx, wy); //Read the pixel from the window framebuffer
			if (source->enableTransparentColor && source->transparentColor == color) continue; //Transparent
			driver_framebuffer_setPixel(target, source->x + wx, source->y + wy, color); //Write the pixel to the global framebuffer
		}
	}
}

void _render_windows()
{
	//Step through the linked list of windows and blit each of the visible windows to the main framebuffer
	Window* currentWindow = driver_framebuffer_window_first();
	while (currentWindow != NULL) {
		if (currentWindow->visible) {
			driver_framebuffer_blit(currentWindow, NULL);
		}
		currentWindow = currentWindow->_nextWindow;
	}
}

bool driver_framebuffer_flush(uint32_t flags)
{
	if (!framebuffer) {
		ESP_LOGE(TAG, "flush without alloc!");
		return false;
	}

	_render_windows();

	uint32_t eink_flags = 0;

	if ((flags & FB_FLAG_FULL) || (flags & FB_FLAG_FORCE)) {
		driver_framebuffer_set_dirty_area(0, 0, FB_WIDTH-1, FB_HEIGHT-1, true);
	} else if (!driver_framebuffer_is_dirty()) {
		return false; //No need to update, stop.
	}

	driver_framebuffer_set_dirty_area(FB_WIDTH-1, FB_HEIGHT-1, 0, 0, true); //Not dirty.
	return true;
}

uint16_t driver_framebuffer_getWidth(Window* window)
{
	int16_t width, height;
	driver_framebuffer_get_orientation_size(window, &width, &height);
	return width;
}

uint16_t driver_framebuffer_getHeight(Window* window)
{
	int16_t width, height;
	driver_framebuffer_get_orientation_size(window, &width, &height);
	return height;
}
