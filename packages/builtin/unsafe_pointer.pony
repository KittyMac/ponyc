struct UnsafePointer[A]
  """
  A UnsafePointer[A] is a raw memory pointer. It has no descriptor and thus can't be
  included in a union or intersection, or be a subtype of any interface. UnsafePointer[A]
  is exactly like Pointer[A] except none of the method are private, allowing the Pony developer
  the freedom to use unsafe pointer capabilities if they so choose.
  """
  new create() =>
    """
    A null pointer.
    """
    compile_intrinsic

  new alloc(len: USize) =>
    """
    Space for len instances of A.
    """
    compile_intrinsic

  fun ref realloc(len: USize): UnsafePointer[A] =>
    """
    Keep the contents, but reserve space for len instances of A.
    """
    compile_intrinsic

  fun tag unsafe(): UnsafePointer[A] ref =>
    """
    Unsafe change in reference capability.
    """
    compile_intrinsic

  fun convert[B](): this->UnsafePointer[B] =>
    """
    Convert from UnsafePointer[A] to UnsafePointer[B].
    """
    compile_intrinsic
  
  fun apply(i: USize): this->A =>
    """
    Retrieve index i.
    """
    compile_intrinsic
  
  fun pointee(): this->A =>
    """
    Dereference the pointer
    """
    apply(0)

  fun ref update(i: USize, value: A!): A^ =>
    """
    Set index i and return the previous value.
    """
    compile_intrinsic

  fun offset(n: USize): this->UnsafePointer[A] =>
    """
    Return a pointer to the n-th element.
    """
    compile_intrinsic

  fun tag element_size(): USize =>
    """
    Return the size of a single element in an array of type A.
    """
    compile_intrinsic

  fun ref insert(n: USize, len: USize): UnsafePointer[A] =>
    """
    Creates space for n new elements at the head, moving following elements.
    The array length before this should be len, and the available space should
    be at least n + len.
    """
    compile_intrinsic

  fun ref delete(n: USize, len: USize): A^ =>
    """
    Delete n elements from the head of pointer, compact remaining elements of
    the underlying array. The array length before this should be n + len.
    Returns the first deleted element.
    """
    compile_intrinsic

  fun copy_to(that: UnsafePointer[this->A], n: USize): this->UnsafePointer[A] =>
    """
    Copy n elements from this to that.
    """
    compile_intrinsic

  fun tag usize(): USize =>
    """
    Convert the pointer into an integer.
    """
    compile_intrinsic

  fun tag is_null(): Bool =>
    """
    Return true for a null pointer, false for anything else.
    """
    compile_intrinsic

  fun tag eq(that: UnsafePointer[A] tag): Bool =>
    """
    Return true if this address is that address.
    """
    compile_intrinsic

  fun tag lt(that: UnsafePointer[A] tag): Bool =>
    """
    Return true if this address is less than that address.
    """
    compile_intrinsic

  fun tag ne(that: UnsafePointer[A] tag): Bool => not eq(that)
  fun tag le(that: UnsafePointer[A] tag): Bool => lt(that) or eq(that)
  fun tag ge(that: UnsafePointer[A] tag): Bool => not lt(that)
  fun tag gt(that: UnsafePointer[A] tag): Bool => not le(that)

  fun tag hash(): USize =>
    """
    Returns a hash of the address.
    """
    usize().hash()

  fun tag hash64(): U64 =>
    """
    Returns a 64-bit hash of the address.
    """
    usize().hash64()
