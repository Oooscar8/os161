/*
 * Virtual Address Allocator (VAA)
 *
 * The VAA manages virtual address space allocation in OS161 on a per-page basis,
 * working together with the Physical Memory Manager (PMM).
 * It handles page-aligned virtual address allocation in both kernel space (KSEG2).
 *  After the initialization of the VM system, we use KSEF2 instead
 * of KSEG0 (direct map) for kernel space. Since I am work on limited RAM, I need
 * to swap out the kernel pages to disk and bring them back when needed. 
 */

#ifndef _VAA_H_
#define _VAA_H_

#include <types.h>
#include <vm.h>

/* 
 * Structure to track virtual address regions
 * Used internally by the VAA to manage free virtual pages
 */
struct vaddr_region {
    vaddr_t vaddr;               /* Virtual address of the page */
    struct vaddr_region *next;   /* Next free region in the list */
};

/*
 * Initialize the Virtual Address Allocator
 * Must be called after vm_bootstrap() and before any other VAA functions
 *
 * This function:
 * - Sets up the free virtual page tracking for kernel space
 * - Initializes internal management structures
 *
 * Returns: 0 on success, non-zero on failure
 */
int vaa_init(void);

/*
 * Allocate a virtual page in kernel space (KSEG2)
 *
 * Returns:
 * - Virtual address of the allocated page on success (in KSEG2)
 * - 0 on failure (out of virtual address space)
 *
 * The returned address is guaranteed to be:
 * - Page-aligned
 * - In KSEG2 segment
 * - Not already allocated
 */
vaddr_t vaa_alloc_kpage(void);


/*
 * Free a previously allocated kernel virtual page
 *
 * Parameters:
 * addr - Virtual address of the page to free
 *       Must be page-aligned and previously allocated
 *
 * Returns: 0 on success, non-zero on failure (invalid address)
 */
int vaa_free_kpage(vaddr_t addr);

/*
 * Allocate a contiguous region of virtual pages in kernel space
 *
 * Parameters:
 * npages - Number of pages to allocate
 *
 * Returns:
 * - Virtual address of the first page on success
 * - 0 on failure (out of virtual address space)
 *
 * The returned address is guaranteed to be:
 * - Page-aligned
 * - In KSEG2 segment
 * - Contiguous with the requested number of pages
 */
vaddr_t vaa_alloc_npages(size_t npages);

#endif /* _VAA_H_ */