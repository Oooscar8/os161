#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <spinlock.h>
#include <page_replacement.h>
#include <swap.h>

/* Global FIFO queue */
static struct fifo_queue fifo;

int
fifo_init(unsigned int nframes)
{
    /* Allocate the array for storing pages */
    fifo.pages = kmalloc(sizeof(vaddr_t) * nframes);
    if (fifo.pages == NULL) {
        return PR_NOMEM;
    }

    /* Initialize queue parameters */
    fifo.head = 0;
    fifo.tail = 0;
    fifo.nframes = nframes;

    /* Initialize lock */
    spinlock_init(&fifo.lock);

    return PR_OK;
}

int
fifo_evict_page(struct page_table *pt, vaddr_t *evicted_addr)
{
    struct pte *pte;
    int result;

    /* Acquire lock to protect queue operations */
    spinlock_acquire(&fifo.lock);

    /* Check if queue is empty */
    if (fifo.head == fifo.tail) {
        spinlock_release(&fifo.lock);
        return PR_ERROR;
    }

    /* Get the oldest page from queue */
    *evicted_addr = fifo.pages[fifo.head];
    fifo.head = (fifo.head + 1) % fifo.nframes;

    spinlock_release(&fifo.lock);

    /* Get PTE for the page to be evicted */
    pte = pte_get(pt, *evicted_addr);
    if (pte == NULL) {
        return PR_ERROR;
    }

    /* If page is dirty, write it to swap */
    if (pte->dirty) {
        result = swap_out_page(pt, *evicted_addr);
        if (result != SWAP_SUCCESS) {
            return PR_ERROR;
        }
    }

    /* Unmap the page */
    result = pte_unmap(pt, *evicted_addr);
    if (result != 0) {
        return PR_ERROR;
    }

    /* Invalidate TLB entry */
    tlb_invalidate(*evicted_addr);

    return PR_OK;
}

int
fifo_page_fault(struct page_table *pt, vaddr_t faultaddr)
{
    paddr_t newframe;
    vaddr_t evicted_addr;
    uint32_t flags;
    int result;

    /* Try to get a new frame first */
    newframe = getppages(1);
    if (newframe == 0) {
        /* No free frames, need to evict a page */
        result = fifo_evict_page(pt, &evicted_addr);
        if (result != PR_OK) {
            return result;
        }
        newframe = evicted_addr;
    }

    /* If page was swapped, bring it back */
    result = swap_in_page(pt, faultaddr);
    if (result == SWAP_SUCCESS) {
        /* Map the page */
        flags = PTE_VALID | PTE_USER | PTE_WRITE;
        result = pte_map(pt, faultaddr, newframe, flags);
        if (result != 0) {
            return PR_ERROR;
        }
    }

    /* Add new page to FIFO queue */
    spinlock_acquire(&fifo.lock);
    
    /* Check if queue is full */
    if ((fifo.tail + 1) % fifo.nframes == fifo.head) {
        spinlock_release(&fifo.lock);
        return PR_NOSPACE;
    }

    /* Add page to queue */
    fifo.pages[fifo.tail] = faultaddr;
    fifo.tail = (fifo.tail + 1) % fifo.nframes;

    spinlock_release(&fifo.lock);

    return PR_OK;
}