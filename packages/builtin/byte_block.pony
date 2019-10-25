use "files"

use @memcpy[Pointer[None]](dst: Pointer[None], src: Pointer[None], n: USize)
use @malloc[Pointer[U8]](bytes: USize)
use @free[None](pointer: Pointer[None] tag)

class ByteBlock
	"""
	A contiguos, non-resizable block of memory which is not subject to the Pony
	runtime's garbage collected memory system.  This means this unit of memory
	will be dealloc immediately when this object is finalized.
	Contiguous, resizable memory to store elements of type A.

	## Usage

	Creating an ByteBlock:
	```pony
	let block: ByteBlock = ByteBlock(512)
	```

	Accessing elements in a ByteBlock:
	let x = block(5)?
	"""
	var _size: USize
	var _ptr: Pointer[U8] box
	
	fun _final() =>
		@free(_ptr.offset(0))

	new create(len: USize = 0) =>
		"""
		Create an byte block of len bytes
		"""
		_size = len
		_ptr = recover @malloc(len) end
	
	new val from_array(data: Array[U8] val) =>
		"""
		Create a string from an array
		"""
		_size = data.size()
		_ptr = recover @malloc(_size) end
		@memcpy(_ptr, data.cpointer(), _size)
	
	new iso from_iso_array(data: Array[U8] iso) =>
		"""
		Create a string from an array
		"""
		_size = data.size()
		_ptr = recover @malloc(_size) end
		@memcpy(_ptr, data.cpointer(), _size)
	
    fun val array(): Array[U8] val =>
      """
      Returns an Array[U8] that is a copy of the data in the byteblock
      """
		let a = recover Array[U8](_size) end
		@memcpy(a.cpointer(), _ptr, _size)
		a

	
	fun iso_array(): Array[U8] iso^ =>
		"""
		Returns an Array[U8] iso that reuses the underlying data pointer.
		"""
		let a = recover Array[U8](_size) end
		@memcpy(a.cpointer(), _ptr, _size)
		a
	
    fun string(): String val =>
		"""
		Returns an String that is a copy of the data in the byteblock
		"""
		recover String.copy_cpointer(_ptr, _size) end

	fun iso_string(): String iso^ =>
		"""
		Returns an String iso that reuses the underlying data pointer.
		"""
		recover iso String.copy_cpointer(_ptr, _size) end

	fun apply(i: USize): U8 ? =>
		"""
		Get the i-th element, raising an error if the index is out of bounds.
		"""
		if i < _size then
			_ptr._apply(i)
		else
			error
		end
	
	fun cpointer(offset: USize = 0): Pointer[U8] tag =>
		"""
		Return the underlying C-style pointer.
		"""
		_ptr._offset(offset)
	
	fun ref set(value:U8 = 0) =>
		"""
		replace all elements in the block with value
		"""
		@memset(_ptr, value.u32(), _size)
	
	fun ref clear() =>
		"""
		replace all elements in the block with 0. Just sugar for set(0)
		"""
		set(0)

	fun size(): USize =>
		"""
		The number of elements in the array.
		"""
		_size
	
	fun ref truncate(len: USize) =>
		"""
		Truncate an array to the given length. If the array is already smaller than len, do nothing.
		Note that this doesn't affect allocated memory.
		"""
		_size = _size.min(len)
