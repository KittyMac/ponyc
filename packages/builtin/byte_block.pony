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
	var _ptr: Pointer[U8] ref
	
	fun _final() =>
		@free(_ptr.offset(0))

	new create(len: USize = 0) =>
		"""
		Create an byte block of len bytes
		"""
		_size = len
		_ptr = @malloc(len)

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
