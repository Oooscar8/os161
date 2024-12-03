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

    /* Write it to swap (allowed to use the reserved slot)*/
    result = swap_out_page(pt, *evicted_addr, 1);
    if (result != SWAP_SUCCESS) {
        return PR_ERROR;
    }

    return PR_OK;
}
