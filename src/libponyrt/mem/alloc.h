#ifndef mem_alloc_h
#define mem_alloc_h

/**
 * Called periodically by scheduler 0 to update total memory usage as seen by the OS
 */
void ponyint_update_memory_usage();

/**
 * Total memory allocated by the runtime
 */
size_t ponyint_total_memory();

/**
 * Allocates memory in the virtual address space.
 */
void* ponyint_virt_alloc(size_t bytes);

/**
 * Deallocates a chunk of memory that was previously allocated with
 * ponyint_virt_alloc.
 */
void ponyint_virt_free(void* p, size_t bytes);

#endif
