#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <uio.h>
#include <vm.h>
#include <pagetable.h>
#include <swap.h>
#include <current.h>
#include <proc.h>

/* Global instance */
struct swap_manager swap_manager;

int
swap_init(void)
{
    int result;

    spinlock_init(&swap_manager.swap_lock);
    
    swap_manager.entries = kmalloc(sizeof(struct swap_entry) * SWAP_MAX_PAGES);
    if (swap_manager.entries == NULL) {
        return SWAP_NOMEM;
    }

    memset(swap_manager.entries, 0, sizeof(struct swap_entry) * SWAP_MAX_PAGES);
    
    swap_manager.count = 0;

    result = vfs_open((char *)SWAP_DEVICE, O_RDWR, 0, &swap_manager.swap_dev);
    if (result) {
        kfree(swap_manager.entries);
        return SWAP_IO_ERROR;
    }

    return SWAP_SUCCESS;
}

void
swap_shutdown(void)
{
    if (swap_manager.swap_dev != NULL) {
        vfs_close(swap_manager.swap_dev);
    }
    
    if (swap_manager.entries != NULL) {
        kfree(swap_manager.entries);
    }
    
    spinlock_cleanup(&swap_manager.swap_lock);
}

static
int
find_free_slot(void)
{
    for (unsigned int i = 0; i < SWAP_MAX_PAGES; i++) {
        if (!swap_manager.entries[i].used) {
            return i;
        }
    }
    return SWAP_FULL;
}

static int find_swap_entry(pid_t pid, vaddr_t vaddr)
{
    for (unsigned int i = 0; i < SWAP_MAX_PAGES; i++) {
        if (swap_manager.entries[i].used && 
            swap_manager.entries[i].pid == pid && 
            swap_manager.entries[i].vaddr == vaddr) {
            return i;
        }
    }
    return -1;
}

int
swap_out_page(struct page_table *pt, vaddr_t vaddr)
{
    struct iovec iov;
    struct uio ku;
    int result;
    int slot;
    vaddr_t page_start;
    
    KASSERT(pt != NULL);
    page_start = vaddr & PAGE_FRAME;

    spinlock_acquire(&swap_manager.swap_lock);

    if (swap_manager.count >= SWAP_MAX_PAGES) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }

    slot = find_free_slot();
    if (slot < 0) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }

    uio_kinit(&iov, &ku, (void *)page_start, PAGE_SIZE,
              SWAP_PAGE_TO_OFFSET(slot), UIO_WRITE);

    result = VOP_WRITE(swap_manager.swap_dev, &ku);
    if (result) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_IO_ERROR;
    }

    swap_manager.entries[slot].vaddr = vaddr;
    swap_manager.entries[slot].pid = curproc->p_pid;
    swap_manager.entries[slot].used = true;  /* Mark the entry as used */

    swap_manager.count++;

    spinlock_release(&swap_manager.swap_lock);
    return SWAP_SUCCESS;
}

int
swap_in_page(struct page_table *pt, vaddr_t vaddr)
{
    struct iovec iov;
    struct uio ku;
    int result;
    int slot;
    vaddr_t page_start;
    
    KASSERT(pt != NULL);
    page_start = vaddr & PAGE_FRAME;

    spinlock_acquire(&swap_manager.swap_lock);

    slot = find_swap_entry(curproc->p_pid, vaddr);
    if (slot < 0) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_INVALID;
    }

    uio_kinit(&iov, &ku, (void *)page_start, PAGE_SIZE,
              SWAP_PAGE_TO_OFFSET(slot), UIO_READ);

    result = VOP_READ(swap_manager.swap_dev, &ku);
    if (result) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_IO_ERROR;
    }

    /* Mark the entry as unused */
    swap_manager.entries[slot].used = false;

    swap_manager.count--;

    spinlock_release(&swap_manager.swap_lock);
    return SWAP_SUCCESS;
}