#include <stdbool.h>
#include <stdint.h>

typedef enum alloc_code {
    INITIALIZATION_SUCCESS,
    INITIALIZATION_ERROR,
    EXPANSION_SUCCESS,
    EXPANSION_ERROR,
    DEINITIALIZATION_SUCCESS,
    DEINITIALIZATION_ERROR,
    ALLOCATION_SUCCESS,
    ALLOCATION_ERROR,
    REALLOCATION_SUCCESS,
    REALLOCATION_ERROR,
    DEALLOCATION_SUCCESS,
    DEALLOCATION_ERROR.
    SIZE_SUCCESS,
    SIZE_ERROR,
    NOTE,
    /* ... */
} alloc_code;

typedef enum alloc_result {
    SUCCESS,
    OUT_OF_MEMORY,
    INVALID_ADDRESS,
    INVALID_PARAMETER,
    ERROR_UNKNOWN
} alloc_result;


typedef void (*PFN_alloc_log)(alloc_code code, char* msg);

typedef struct allocator {
    uint32_t page_size;
    uint32_t allocated_pages;
        /* Page Allocation Table - 2 bits per page: 
            00 - unallocated, zeroed
            01 - unallocated, freed but not zeroed
            10 - initial page of an allocation
            11 - continuation chunk of allocation
            stored as _little endian_, so for pages A B C D as bit pattern DDCCBBAA
        */
    uint8_t *PAT;  
    uint8_t *data;
    PFN_alloc_log log_function;
} allocator;

/* to be used for the old_size parameters in case of no data */
#define NO_OLD_SIZE_DATA ((size_t) (-1L))



alloc_result init_allocator(uint32_t page_size_bytes, uint32_t initial_page_number, PFN_alloc_log log_function, allocator* out_alloc);
alloc_result expand_alloctor(allocator* p_alloc, uint32_t new_page_number);
alloc_result deinit_allocator(allocator* p_alloc);


alloc_result get_size(const allocator* p_alloc, void* ptr, size_t** size);
alloc_result alloc_align_offset_zeroable(const allocator* p_alloc, size_t size, int alignment_bits, size_t offset_to_alignment, bool zeroed, void** out_ptr);
alloc_result resize_oldsize_zeroable(const allocator* p_alloc, void* old_ptr, size_t old_size, size_t new_size, int alignment_bits, size_t offset_to_alignment, bool allow_new_alignment, bool zero_new_pages, void** new_ptr);
alloc_result resize_oldsize_zeroable_copy(const allocator* p_alloc, void* old_ptr, size_t old_size, size_t new_size, int alignment_bits, size_t offset_to_alignment, bool zero_new_pages, void** new_ptr);
alloc_result free_size(const allocator* p_alloc, void* ptr, size_t old_size);


/* usable like:

static allocator default_alloc;

static void* malloc(size_t size) {
    void* ptr;
    alloc_result res = alloc_align_offset_zeroable(default_alloc, size, 0, 0, false, &ptr);
    return (res == SUCCESS) ? ptr : NULL;
}

etc.

*/
