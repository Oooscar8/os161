#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <spinlock.h>
#include <page_replacement.h>
#include <swap.h>

/* Global FIFO queue */
static struct fifo_queue *fifo;

int
fifo_init(unsigned int nframes)
{
    fifo = kmalloc(sizeof(struct fifo_queue));
    if (fifo == NULL) {
        return PR_NOMEM;
    }

    fifo->head = NULL;
    fifo->tail = NULL;
    spinlock_init(&fifo->fifo_lock);

    return PR_OK;
}

int
fifo_evict_page(struct page_table *pt, vaddr_t *evicted_addr)
{
    struct fifo_node *old_head;
    int result;

    /* Acquire lock to protect queue operations */
    spinlock_acquire(&fifo->fifo_lock);

    /* Check if queue is empty */
    if (fifo->head == NULL) {
        spinlock_release(&fifo->fifo_lock);
        return PR_ERROR;
    }

    /* Get the oldest page from queue */
    old_head = fifo->head;
    *evicted_addr = old_head->page_addr;

    /* Update head pointer */
    fifo->head = old_head->next;
    if (fifo->head == NULL) {
        fifo->tail = NULL;
    }

    /* Free the removed node */
    kfree(old_head);

    spinlock_release(&fifo->fifo_lock);

    /* Write it to swap (allowed to use the reserved slot)*/
    result = swap_out_page(pt, *evicted_addr, 1);
    if (result != SWAP_SUCCESS) {
        return PR_ERROR;
    }

    return PR_OK;
}

int
fifo_add_page(struct page_table *pt, vaddr_t addr)
{
    struct fifo_node *new_node;

    /* Create new node */
    new_node = kmalloc(sizeof(struct fifo_node));
    if (new_node == NULL) {
        return PR_NOMEM;
    }

    new_node->page_addr = addr;
    new_node->pt = pt;
    new_node->next = NULL;

    /* Acquire lock */
    spinlock_acquire(&fifo->fifo_lock);

    /* Add to tail */
    if (fifo->tail == NULL) {
        fifo->head = fifo->tail = new_node;
    } else {
        fifo->tail->next = new_node;
        fifo->tail = new_node;
    }

    spinlock_release(&fifo->fifo_lock);
    return PR_OK;
}

int
fifo_remove_page(struct page_table *pt, vaddr_t addr)
{
    struct fifo_node *curr, *prev;
    
    spinlock_acquire(&fifo->fifo_lock);
    
    prev = NULL;
    curr = fifo->head;
    
    /* Search for the page - match both pt and addr */
    while (curr != NULL) {
        if (curr->page_addr == addr && curr->pt == pt) {
            /* Found it - remove from list */
            if (prev == NULL) {
                /* It's the head */
                fifo->head = curr->next;
            } else {
                prev->next = curr->next;
            }
            
            /* Update tail if necessary */
            if (curr == fifo->tail) {
                fifo->tail = prev;
            }
            
            kfree(curr);
            spinlock_release(&fifo->fifo_lock);
            return PR_OK;
        }
        prev = curr;
        curr = curr->next;
    }
    
    spinlock_release(&fifo->fifo_lock);
    return PR_ERROR;  /* Page not found */
}
