#include "alloc.h"

/* implementation using stdlib: */

#include <stdlib.h>


alloc_result init_allocator(uint32_t page_size_bytes, uint32_t initial_page_number, PFN_alloc_log log_function, allocator* out_alloc) {
    if(out_alloc == NULL) {
        if(log_function != NULL) log_function(INITIALIZATION_ERROR, "Out-parameter for Allocator struct is NULL!");
        return INVALID_PARAMETER;
    }
    if(page_size_bytes % 64 != 0) {
        if(log_function != NULL) log_function(INITIALIZATION_ERROR, "Page size not a multiple of 64 bytes!");
        return INVALID_PARAMETER;
    }
    if(initial_page_number % 4 != 0) {
        if(log_function != NULL) log_function(INITIALIZATION_ERROR, "Page number not a multiple of 4!");
        return INVALID_PARAMETER;
    }
     
    out_alloc[0].page_size = page_size_bytes;
    out_alloc[0].allocated_pages = initial_page_number;
    out_alloc[0].log_function = log_function;
    
    size_t allocation_size = initial_page_number * page_size_bytes;
    size_t PAT_size = allocation_size / 4;
    
    out_alloc[0].PAT = calloc(PAT_size,sizeof(uint8_t));
    if(out_alloc[0].PAT == NULL) {
        if(out_alloc[0].log_function != NULL) out_alloc[0].log_function(INITIALIZATION_ERROR, "Ran out of system memory when trying to allocate Page Allocation Table");
        return OUT_OF_MEMORY;
    }
    out_alloc[0].data = calloc(allocation_size,sizeof(uint8_t));
    if(out_alloc[0].data == NULL) {
        if(out_alloc[0].log_function != NULL) out_alloc[0].log_function(INITIALIZATION_ERROR, "Ran out of system memory when trying to allocate memory pages");
        return OUT_OF_MEMORY;
    }
    
    if(out_alloc[0].log_function != NULL) out_alloc[0].log_function(INITIALIZATION_SUCCESS, "Successfully initialized the allocator memory pages");
    
    return SUCCESS;
}

alloc_result expand_alloctor(allocator* p_alloc, uint32_t new_page_number) {
    if(p_alloc == NULL) {
        return INVALID_PARAMETER;
    }
    if(new_page_number % 4 != 0) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(EXPANSION_ERROR, "New page number not a multiple of 4!");
        return INVALID_PARAMETER;
    }
    if(new_page_number <= p_alloc[0].allocated_pages) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(EXPANSION_SUCCESS, "Allocator memory expansion successful due to not exceeding old allocation size");
        return SUCCESS;
    }
    
    size_t new_alloc_size = new_page_number * p_alloc[0].page_size;
    size_t new_PAT_size = new_alloc_size / 4;
    
    void* new_PAT_ptr = realloc(p_alloc[0].PAT, new_PAT_size);
    if(new_PAT_ptr == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(EXPANSION_ERROR, "Ran out of system memory when trying to expand Page Allocation Table");
        return OUT_OF_MEMORY;
    }
    p_alloc[0].PAT = new_PAT_ptr;
    void* new_data_ptr = realloc(p_alloc[0].data, new_alloc_size);
    if(new_data_ptr == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(EXPANSION_ERROR, "Ran out of system memory when trying to expand memory pages");
        return OUT_OF_MEMORY;
    }
    p_alloc[0].data = new_data_ptr;
    
    p_alloc[0].allocated_pages = new_page_number;
    
    if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(EXPANSION_SUCCESS, "Successfully expanded the allocator memory pages");
    
    return SUCCESS;
}

alloc_result deinit_allocator(allocator* p_alloc) {
    if(p_alloc == NULL) {
        return INVALID_PARAMETER;
    }
    
    PFN_alloc_log log_function = p_alloc[0].log_function;
    
    free(p_alloc[0].PAT);
    free(p_alloc[0].data);
    
    void* memset_return = memset(p_alloc, 0, sizeof(allocator));
    if(memset_return != p_alloc) {
        if(log_function != NULL) log_function(DEINITIALIZATION_ERROR, "Unknown error: memset returns wrong pointer!");
        return ERROR_UNKNOWN;
    }
    
    if(log_function != NULL) log_function(DEINITIALIZATION_SUCCESS, "Successfully deinitialized the allocator memory pages");
    
    return SUCCESS;
}


static bool alignment_satisfied(int i, uint32_t page_size, int alignment_bits, size_t offset_to_alignment, uint8_t *data) {
    if(alignment_bits <= 6 && offset % 64 == 0) return true; /* this is the multiplier in init_allocator and expand_alloctor, currently 64 = 2**6 */
    uint8_t *actual_address = &(data[i*page_size + offset]);
    size_t alignment_mask = (1 << alignment_bits) - 1;
    return ((((size_t)actual_address) & alignment_mask) == 0);
}

alloc_result alloc_align_offset_zeroable(const allocator* p_alloc, size_t size, int alignment_bits, size_t offset_to_alignment, bool zeroed, void** out_ptr) {
    if(p_alloc == NULL) {
        return INVALID_PARAMETER;
    }
    if(out_ptr == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(ALLOCATION_ERROR, "Out-pointer to store memory pointer is NULL!");
        return INVALID_PARAMETER;
    }
    if(size == 0) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(ALLOCATION_ERROR, "Attempt to allocate 0 bytes!");
        return INVALID_PARAMETER;
    }
    
    /* we need to divide as floating point so we can round up */
    size_t used_pages = (size_t) ceil(((float)size)/((float)p_alloc[0].page_size));
    
    if(used_pages > p_alloc[0].allocated_pages) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(ALLOCATION_ERROR, "Asked-for memory exceeds allocator memory pages!");
        return OUT_OF_MEMORY;
    }
    
    bool found = false;
    int length_found = 0;
    int initial_index = 0;
    for(int i = 0; i < p_alloc[0].allocated_pages; i++) {
        int state = (p_alloc[0].PAT[i/4] >> (i%4)*2) & 0x3;
        switch(state) {
            /* 00 */ case 0x00:
            /* 01 */ case 0x01:
                if(!found && (alignment_bits == 0 || alignment_satisfied(i, p_alloc[0].page_size, alignment_bits, offset_to_alignment, p_alloc[0].data))) {
                    found = true;
                    initial_index = i;
                }
                length_found++;
            break;
            /* 10 */ case 0x02:
            /* 11 */ case 0x03:
                if(length_found >= used_pages) goto after_loop;
                else {
                    found = false;
                    length_found = 0;
                }
            break;
        }
    }
    after_loop:
    
    if(!found) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(ALLOCATION_ERROR, "The allocator has no area available for allocation due to use or fragmentation");
        return OUT_OF_MEMORY;
    }
    
    /* for zeroeing memory and setting the markings, we need to go through the array again, since the first round we couldn't know if the page gap was large enough: */
    for(int i = initial_index; i < used_pages; i++) {
        int old_value = p_alloc[0].PAT[i/4];
        int old_state = (old_value >> (i%4)*2) & 0x3;
        if(zeroed) {
            switch(old_state) {
                /* 00 */ case 0x00:
                break;
                /* 01 */ case 0x01:
                    void* memset_return = memset(p_alloc[0].data[i*page_size], 0, page_size);
                    if(memset_return != p_alloc) {
                        if(log_function != NULL) log_function(ALLOCATION_ERROR, "Unknown error: memset returns wrong pointer!");
                        return ERROR_UNKNOWN;
                    }
                break;
                /* 10 */ case 0x02:
                /* 11 */ case 0x03:
                    if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(ALLOCATION_ERROR, "During zeroing of the memory allocation the PAT appears to have been externally manipulated!");
                    return ERROR_UNKNOWN;
                break;
            }
        }
        int new_state = (i == initial_index) ? 0x02 : 0x03;
        int new_value = new_state << (i%4)*2;
        int write_mask = (0x3 << (i%4)*2);
        int old_mask = 0xFF - write_mask;
        p_alloc[0].PAT[i/4] = (old_value&old_mask) | new_value;
    }
    
    out_ptr[0] = &(p_alloc[0].data[initial_index*page_size]);
    if(log_function != NULL) log_function(ALLOCATION_SUCCESS, "Successfully allocated memory");
    return SUCCESS;
}

alloc_result get_size(const allocator* p_alloc, void* ptr, size_t* size) {
    if(p_alloc == NULL) {
        return INVALID_PARAMETER;
    }
    if(ptr == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(SIZE_ERROR, "Poitnter ro calculate size is NULL!");
        return INVALID_PARAMETER;
    }
    if(size == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(SIZE_ERROR, "Out-pointer to store size is NULL!");
        return INVALID_PARAMETER;
    }
    
    int first_index = (((size_t)ptr) - ((size_t)p_alloc[0].data))/p_alloc[0].page_size;
    if(first_index < 0 || first_index >= p_alloc[0].allocated_pages) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(SIZE_ERROR, "Pointer outside of memory pages!");
        return INVALID_ADDRESS;
    }
    
    int nr_of_used_pages = 1;
    for(int i = first_index; i < p_alloc[0].allocated_pages; i++) {
        int value = p_alloc[0].PAT[i/4];
        int state = (value >> (i%4)*2) & 0x3;
        if(((i == first_index) && (state != 0x02))) {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(SIZE_ERROR, "Pointer doesn't point to begin of allocation!");
            return INVALID_ADDRESS;
        }
        if((i != first_index)) {
            if(state == 0x03) nr_of_used_pages++;
            else break;
        }
    }
    
    size[0] = nr_of_used_pages*p_alloc[0].page_size;
    if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(SIZE_SUCCESS, "Size successfully measured");
    return SUCCESS;
}

alloc_result resize_oldsize_zeroable(const allocator* p_alloc, void* old_ptr, size_t old_size, size_t new_size, int alignment_bits, size_t offset_to_alignment, bool allow_new_alignment, bool zero_new_pages, void** new_ptr) {
    if(p_alloc == NULL) {
        return INVALID_PARAMETER;
    }
    if(old_ptr == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Poitnter ro resize/reallocate is NULL!");
        return INVALID_PARAMETER;
    }
    if(new_ptr == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Out-pointer to store memory pointer is NULL!");
        return INVALID_PARAMETER;
    }
    if(new_size == 0) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Attempt to resize to 0 bytes!");
        return INVALID_PARAMETER;
    }
    
    /* we need to divide as floating point so we can round up */
    size_t new_pages = (size_t) ceil(((float)new_size)/((float)p_alloc[0].page_size));
    if(new_pages > p_alloc[0].allocated_pages) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Asked-for memory exceeds allocator memory pages!");
        return OUT_OF_MEMORY;
    }
    
    int old_index = (((size_t)old_ptr) - ((size_t)p_alloc[0].data))/p_alloc[0].page_size;
    if(old_index < 0 || old_index >= p_alloc[0].allocated_pages) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Pointer to be resized outside of memory pages!");
        return INVALID_ADDRESS;
    }
    
    
    size_t old_pages;
    alloc_result size_result = get_size(p_alloc, old_ptr, &old_pages);
    if(size_result != SUCCESS) return size_result;
    
    if(old_size != 0) {
        size_t nominal_old_pages = (size_t) ceil(((float)old_size)/((float)p_alloc[0].page_size));
        if(nominal_old_pages > p_alloc[0].allocated_pages) {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Old given size for memory exceeds allocator memory pages!");
            return OUT_OF_MEMORY;
        }
        if(nominal_old_pages != old_pages) {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Pointer to be resized doesn't have the expected length!");
            return INVALID_ADDRESS;
        }
    }
    
    
    if((alignment_bits != 0) && !alignment_satisfied(old_index, p_alloc[0].page_size, alignment_bits, offset_to_alignment, p_alloc[0].data)) {
        if(allow_new_alignment) {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(NOTE, "Due to alignment differences the new allocation will be handled by copying");
            return resize_oldsize_zeroable_copy(p_alloc, old_ptr, old_size, new_size, alignment_bits, offset_to_alignment, zero_new_pages, new_ptr);
        } else {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "New alignment was given but not allowed!");
            return INVALID_ADDRESS;
        }
    }
    
    
    if(new_pages == old_pages) {
        new_ptr[0} = old_ptr;
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_SUCCESS, "Pointer resized to same page number size and returned without change");
        return SUCCESS;
    } else if (new_pages < old_pages) {
        for(int i = old_index + new_pages; i < old_index + old_pages; i++) {
            int old_value = p_alloc[0].PAT[i/4];
            int new_state = 0x01;
            int new_value = new_state << (i%4)*2;
            int write_mask = (0x3 << (i%4)*2);
            int old_mask = 0xFF - write_mask;
            p_alloc[0].PAT[i/4] = (old_value&old_mask) | new_value;
        }
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_SUCCESS, "Pointer resized to a smaller page number size, old superfluous pages marked as freed");
        return SUCCESS;
    } else if (new_pages > old_pages) {
        bool enough_space_in_place = true;
        for(int i = old_index + old_pages; i < old_index + new_pages; i++) {
            int value = p_alloc[0].PAT[i/4];
            int state = (old_value >> (i%4)*2) & 0x3;
            if(state == 0x2 || state == 0x3) {
                enough_space_in_place = false;
                break;
            }
        }
        if(enough_space_in_place) {
            /* for zeroeing memory and setting the markings, we need to go through the array again, since the first round we couldn't know if the page gap was large enough: */
            for(int i = old_index + old_pages; i < old_index + new_pages; i++) {
                int old_value = p_alloc[0].PAT[i/4];
                int old_state = (old_value >> (i%4)*2) & 0x3;
                if(zeroed) {
                    switch(old_state) {
                        /* 00 */ case 0x00:
                        break;
                        /* 01 */ case 0x01:
                            void* memset_return = memset(p_alloc[0].data[i*page_size], 0, page_size);
                            if(memset_return != p_alloc) {
                                if(log_function != NULL) log_function(REALLOCATION_ERROR, "Unknown error: memset returns wrong pointer!");
                                return ERROR_UNKNOWN;
                            }
                        break;
                        /* 10 */ case 0x02:
                        /* 11 */ case 0x03:
                            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "During zeroing of the memory reallocation the PAT appears to have been externally manipulated!");
                            return ERROR_UNKNOWN;
                        break;
                    }
                }
                int new_value = 0x03 << (i%4)*2;
                int write_mask = (0x3 << (i%4)*2);
                int old_mask = 0xFF - write_mask;
                p_alloc[0].PAT[i/4] = (old_value&old_mask) | new_value;
            }
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_SUCCESS, "Pointer resized to a bigger page number size, new pages marked as allocated");
            return SUCCESS;
        } else {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(NOTE, "Due to size limitations the new allocation will be handled by copying");
            return resize_oldsize_zeroable_copy(p_alloc, old_ptr, old_size, new_size, alignment_bits, offset_to_alignment, zero_new_pages, new_ptr);
        }
    }
}

alloc_result resize_oldsize_zeroable_copy(const allocator* p_alloc, void* old_ptr, size_t old_size, size_t new_size, int alignment_bits, size_t offset_to_alignment, bool zero_new_pages, void** new_ptr) {
    alloc_result new_address_result = alloc_align_offset_zeroable(p_alloc, new_size, alignment_bits, offset_to_alignment, zero_new_pages, new_ptr);
    if(new_address_result != SUCCESS) return new_address_result;
    if(memmove(new_ptr, old_ptr, min(old_size, new_size)) != new_ptr) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_ERROR, "Unknown memmove error, where it returned a different pointer than expected");
        return ERROR_UNKNOWN;
    }
    alloc_result free_result = free_size(p_alloc, old_ptr, old_size);
    if(free_result != SUCCESS) return free_result;
    
    if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(REALLOCATION_SUCCESS, "Reallocation by copying was successful");
    return SUCCESS;
}

alloc_result free_size(const allocator* p_alloc, void* ptr, size_t old_size) {
    if(p_alloc == NULL) {
        return INVALID_PARAMETER;
    }
    if(ptr == NULL) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(DEALLOCATION_ERROR, "Poitnter ro be freed is NULL!");
        return INVALID_PARAMETER;
    }
    
    
    int old_index = (((size_t)old_ptr) - ((size_t)p_alloc[0].data))/p_alloc[0].page_size;
    if(old_index < 0 || old_index >= p_alloc[0].allocated_pages) {
        if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(DEALLOCATION_ERROR, "Pointer to be freed outside of memory pages!");
        return INVALID_ADDRESS;
    }
    
    size_t old_pages;
    alloc_result size_result = get_size(p_alloc, old_ptr, &old_pages);
    if(size_result != SUCCESS) return size_result;
    
    if(old_size != 0) {
        size_t nominal_old_pages = (size_t) ceil(((float)old_size)/((float)p_alloc[0].page_size));
        if(nominal_old_pages > p_alloc[0].allocated_pages) {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(DEALLOCATION_ERROR, "Old given size for memory exceeds allocator memory pages!");
            return OUT_OF_MEMORY;
        }
        if(nominal_old_pages != old_pages) {
            if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(DEALLOCATION_ERROR, "Pointer to be freed doesn't have the expected length!");
            return INVALID_ADDRESS;
        }
    }
    
    
    for(int i = old_index; i < old_index + old_pages; i++) {
        int old_value = p_alloc[0].PAT[i/4];
        int new_state = 0x01;
        int new_value = new_state << (i%4)*2;
        int write_mask = (0x3 << (i%4)*2);
        int old_mask = 0xFF - write_mask;
        p_alloc[0].PAT[i/4] = (old_value&old_mask) | new_value;
    }
    if(p_alloc[0].log_function != NULL) p_alloc[0].log_function(DEALLOCATION_SUCCESS, "Pointer deallocated, old pages marked as freed");
    return SUCCESS;
}

