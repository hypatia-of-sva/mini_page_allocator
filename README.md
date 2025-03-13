# Mini-Page-Allocator

This is a minmal page allocator in C. It allows for the creation of new allocator structs with a given size limit that then allow allocations within them. They are created with a given page size and always allocate multiples of that page size. The functions of the public interface are, first, for the allocators themselves:

```c
alloc_result init_allocator(uint32_t page_size_bytes, uint32_t initial_page_number,
    PFN_alloc_log log_function, allocator* out_alloc);
alloc_result expand_alloctor(allocator* p_alloc, uint32_t new_page_number);
alloc_result deinit_allocator(allocator* p_alloc);
```

All functions return a results enum value, which can be checked against `SUCCESS` which is 0. The allocator is initialized with a number of pages of a certain size; the number can later be increased, but the size remains fixed because otherwise the pointers would have to be touched. Also at creation, a log function can be supplied that has the type:

```c
typedef void (*PFN_alloc_log)(alloc_code code, char* msg);
```

and can be used to log speciic allocation error messages; if not logging is necessary, `NULL` can be supplied as a parameter. The actual allocation functions are:

```c
alloc_result get_size(const allocator* p_alloc, void* ptr, size_t** size);
alloc_result alloc_align_offset_zeroable(const allocator* p_alloc, size_t size, int alignment_bits,
    size_t offset_to_alignment, bool zeroed, void** out_ptr);
alloc_result resize_oldsize_zeroable(const allocator* p_alloc, void* old_ptr, size_t old_size,
    size_t new_size, int alignment_bits, size_t offset_to_alignment, bool allow_new_alignment,
    bool zero_new_pages, void** new_ptr);
alloc_result resize_oldsize_zeroable_copy(const allocator* p_alloc, void* old_ptr, size_t old_size,
    size_t new_size, int alignment_bits, size_t offset_to_alignment, bool zero_new_pages, void** new_ptr);
alloc_result free_size(const allocator* p_alloc, void* ptr, size_t old_size);
```

Here the allocator is always constant, since the number of pages cannot be changed during allocation to prevent runaway memory use. The size can be given back for any pointer, which will be a multiple of the page size. Allocation has next to the desired size also alignment parameters: this means that for a result `ptr` written to `out_ptr`, the address of `((char*)out_ptr)[offset_to_alignment]` has `alignment_bits`-many zeroes as its last bits. The resize function can take the old size, and checks if it is correct as long as it's not 0, and there are flags whether the allocated or newly allocated pages for upsizing should be zeroed. `resize_oldsize_zeroable_copy` always allocates a new region and deletes the old, even if the old one would be expandable, guaranteeing the result being a new pointer. For the free size the same holds as for the resizing old size.

The current implementation is based on the stdlib allocator; it `calloc`s (and if need be `realloc`s and `free`s) two arrays: the actual data array, which is  `page_size_bytes*page_number` big, and the PAT (Page Allocation Table), which holds two bits for each page, meaning

- `00` for unused and zeroed
- `01` for unused and potentially with old data
- `10` for used as an initial page of an allocation
- `11` for used as a subsequent page for an allocation

The implementations are, beyond that, quite bare-bones and not as heavily optimized, esp. when it comes to fragmentation. It seems sensible to use several of these allocators with different page sizes for different allocation size buckets to prevent excessive fragmentation.

