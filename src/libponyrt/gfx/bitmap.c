#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <platform.h>
#include <string.h>
#include <stdio.h>

// a series of high-efficiency methods for bitmap manipulation, used by the
// Pony Bitmap class

typedef struct RGBA
{
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
} RGBA;

typedef struct Rect
{
	size_t x;
	size_t y;
	size_t w;
	size_t h;
} Rect;

inline size_t max(size_t a, size_t b) {
  return a > b ? a : b;
}

inline size_t min(size_t a, size_t b) {
  return a < b ? a : b;
}

void pony_bitmap_fillRect(uint32_t * s, size_t width, size_t height, size_t rX, size_t rY, size_t rW, size_t rH, uint8_t cR, uint8_t cG, uint8_t cB, uint8_t cA) {
#if defined(NATIVE_LITTLE_ENDIAN)
	uint32_t color = (cR << 24) | (cG << 16) | (cB << 8) | cA;
#else
	uint32_t color = (cA << 24) | (cB << 16) | (cG << 8) | cR;
#endif
	uint32_t * p;
	uint32_t * e;

	size_t minY = max(rY, 0);
	size_t maxY = min(rY + rH, height);
	size_t y = minY;
	
	size_t minX = max(rX, 0);
	size_t maxX = min(rX + rW, width);
		
	while ( y < maxY ) {
		p = s + (y * width) + minX;
		e = s + (y * width) + maxX;
		while (p < e) {
			*(p++) = color;
		}
		y++;
	}
}

void pony_bitmap_blit(uint32_t * d_ptr, size_t d_width, size_t d_height, size_t dX, size_t dY, uint32_t * s_ptr, size_t s_width, size_t s_height) {
	uint32_t * p;
	uint32_t * e;
	uint32_t * s;

	size_t minY = 0;
	size_t maxY = min(d_height - dY, s_height);
	size_t y = minY;
	
	size_t minX = 0;
	size_t maxX = min(d_width - dX, s_width);
		
	while ( y < maxY ) {
		p = d_ptr + ((y+dY) * d_width) + (dX+minX);
		e = d_ptr + ((y+dY) * d_width) + (dX+maxX);
		s = s_ptr + (y * s_width) + maxX;
		while (p < e) {
			*(p++) = *(s++);
		}
		y++;
	}
}

void * pony_bitmap_row_pointers(char * ptr, size_t width, size_t height) {
    char ** row_pointers = (char **)malloc(sizeof(char *) * height);
    for (size_t y = 0; y < height; y++)
	{
		row_pointers[y] = ptr + (y * width * 4);
    }
	return row_pointers;
}


void pony_bitmap_row_pointers_free(char * ptr) {
	free(ptr);
}
