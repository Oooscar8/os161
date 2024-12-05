/*
 * Physical Memory Manager (PMM)
 * 
 * The PMM manages physical memory pages in OS161, used after the 
 * initialization of the VM system. It provides functionality
 * for allocating and freeing physical pages, tracking free pages, and 
 * obtaining memory usage statistics.
 */

#ifndef _PMM_H_
#define _PMM_H_

#include <types.h>

/* 
 * Represents a physical page frame
 * Used internally by the PMM to track free pages
 */
struct page_frame {
    paddr_t physical_addr;      /* Physical address of the page */
    struct page_frame *next;    /* Next free page in the list */
};

/*
 * Initialize the Physical Memory Manager
 * Must be called after ram_bootstrap() and before any other PMM functions
 * 
 * This function:
 * - Sets up the free page list
 * - Initializes memory tracking structures
 * - Prepares the PMM for page allocation requests
 * 
 * Returns: 0 on success, non-zero on failure
 */
int pmm_init(void);

/*
 * Allocate a physical page
 * 
 * Returns:
 * - Physical address of the allocated page on success
 * - 0 on failure (out of memory)
 * 
 * The returned page is guaranteed to be:
 * - Page-aligned
 * - Zeroed out
 * - Not already allocated
 */
paddr_t pmm_alloc_page(void);

/*
 * Free a previously allocated physical page
 * 
 * Parameters:
 * addr - Physical address of the page to free
 *        Must be page-aligned and previously allocated
 * 
 * Returns: 0 on success, non-zero on failure (invalid address)
 */
int pmm_free_page(paddr_t addr);

/*
 * Allocate n continuous physical pages
 * 
 * Parameters:
 * npages - Number of pages to allocate
 * 
 * Returns:
 * - Physical address of the first page on success
 * - 0 on failure (out of memory)
 * 
 * The returned pages are guaranteed to be:
 * - Page-aligned
 * - Zeroed out
 * - Contiguous
 * - Not already allocated
 */
paddr_t pmm_alloc_npages(size_t npages);

paddr_t* pmm_alloc_npages_noncontiguous(size_t npages);

void pmm_free_npages_noncontiguous(paddr_t *pages, size_t npages);

//not implement pmm_destroy

#endif /* _PMM_H_ */