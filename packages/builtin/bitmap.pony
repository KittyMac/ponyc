
use @pony_bitmap_fillRect[None](s:Pointer[U8], width:USize, height:USize, rX:USize, rY:USize, rW:USize, rH:USize, cR:U8, cG:U8, cB:U8, cA:U8)
use @pony_bitmap_blit[None](d_ptr:Pointer[U8], d_width:USize, d_height:USize, dX:USize, dY:USize, s_ptr:Pointer[U8], s_width:USize, s_height:USize)

struct RGBA
	var r:U8 = 0
	var g:U8 = 0
	var b:U8 = 0
	var a:U8 = 0
	
	new create(r':U8, g':U8, b':U8, a':U8) =>
		r = r'
		g = g'
		b = b'
		a = a'
	
	new clear() => r = 0; g = 0; b = 0; a = 0
	new black() => r = 0; g = 0; b = 0; a = 255
	new red() => r = 255; g = 0; b = 0; a = 255
	new green() => r = 0; g = 255; b = 0; a = 255
	new blue() => r = 0; g = 0; b = 255; a = 255
	new yellow() => r = 255; g = 255; b = 0; a = 255

struct Rect
	var x:USize = 0
	var y:USize = 0
	var w:USize = 0
	var h:USize = 0

	new create(x':USize, y':USize, w':USize, h':USize) =>
		x = x'
		y = y'
		w = w'
		h = h'


class Bitmap
	"""
	A contiguos, non-resizable block of memory representing an RGBA image.  Memory for this
	is allocated outside of pony's normal memory system.
	"""

	var width:USize
	var height:USize
	var _ptr: Pointer[U8] ref
	
	fun _final() =>
		@pony_free(_ptr)
	
	fun ref free() =>
		"""
		Pre-emptively deallocate the bitmap. Technically this frees it and then replaces it with a 
		bitmap of 1 pixel bitmap
		"""
		@pony_free(_ptr)
		width = 1
		height = 1
		_ptr = @pony_malloc(1 * 4)
	
	new create(width':USize, height':USize) =>
		width = width'
		height = height'
		_ptr = @pony_malloc(width * height * 4)
	
	fun size(): USize =>
		width * height * 4
	
	fun cpointer(offset: USize = 0): Pointer[U8] tag =>
		_ptr._offset(offset)
	
	fun ref clear(c:RGBA) =>
		@pony_bitmap_fillRect[None](_ptr, width, height, 0, 0, width, height, c.r, c.g, c.b, c.a)
	
	fun ref getPixel(x: USize, y: USize):RGBA =>
		let i = (y * width * 4) + (x * 4)
		if (i >= 0) and (i <= ((width * height * 4) - 4)) then
			return RGBA(
					_ptr._apply(i + 0),
					_ptr._apply(i + 1),
					_ptr._apply(i + 2),
					_ptr._apply(i + 3)
				)
		end
		RGBA.clear()
	
	fun ref setPixel(x: USize, y: USize, c:RGBA) =>
		let i = (y * width * 4) + (x * 4)
		if (i >= 0) and (i <= ((width * height * 4) - 4)) then
			_ptr._update(i + 0, c.r)
			_ptr._update(i + 1, c.g)
			_ptr._update(i + 2, c.b)
			_ptr._update(i + 3, c.a)
		end
	
	fun ref fillRect(r:Rect, c:RGBA) =>
		@pony_bitmap_fillRect[None](_ptr, width, height, r.x, r.y, r.w, r.h, c.r, c.g, c.b, c.a)
	
	fun ref blit(x:USize, y:USize, o:Bitmap) =>
		@pony_bitmap_blit[None](_ptr, width, height, x, y, o._ptr, o.width, o.height)
