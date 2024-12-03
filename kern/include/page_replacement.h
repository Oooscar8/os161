#ifndef _PAGE_REPLACEMENT_H_
#define _PAGE_REPLACEMENT_H_

#include <types.h>
#include <pagetable.h>

/* Node structure for the linked list */
struct fifo_node {
    vaddr_t page_addr;           /* Virtual address of the page */
    struct page_table *pt;       /* Page table this page belongs to */
    struct fifo_node *next;      /* Next node in the list */
};

/* FIFO queue structure */
struct fifo_queue {
    struct fifo_node *head;      /* First node (oldest page) */
    struct fifo_node *tail;      /* Last node (newest page) */
    struct spinlock fifo_lock;        /* Lock for FIFO queue */
};

/* Error codes */
#define PR_OK           0    /* Success */
#define PR_ERROR       -1    /* Generic error */
#define PR_NOMEM      -2    /* Out of memory */
#define PR_NOSPACE    -3    /* No space for new page */

/*
 * Initialize FIFO page replacement algorithm
 * 
 * @param nframes: Number of physical frames available
 * @return: 0 on success, error code on failure
 */
int fifo_init(unsigned int nframes);

/*
 * Handle page fault using FIFO replacement if needed
 * 
 * @param pt: Page table of the faulting process
 * @param faultaddr: Faulting virtual address
 * @return: 0 on success, error code on failure
 */
int fifo_page_fault(struct page_table *pt, vaddr_t faultaddr);

/*
 * Force eviction of the oldest page
 * 
 * @param pt: Page table to evict from
 * @param evicted_addr: Will hold the evicted page's virtual address
 * @return: 0 on success, error code on failure
 */
int fifo_evict_page(struct page_table *pt, vaddr_t *evicted_addr);

int fifo_add_page(struct page_table *pt, vaddr_t addr);

int fifo_remove_page(struct page_table *pt, vaddr_t addr);

#endif /* _PAGE_REPLACEMENT_H_ */