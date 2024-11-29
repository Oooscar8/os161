#include <types.h>
#include <lib.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <vm.h>
#include <pagetable.h>
#include <swap.h>
#include <uio.h>
#include <kern/iovec.h>

/* Global instances */
struct swap_manager swap_manager;
struct swap_table swap_table;

/* Initialize swap table */
int swap_table_init(void) {
    swap_table.entries = kmalloc(sizeof(struct swap_entry) * SWAP_MAX_PAGES);
    if (swap_table.entries == NULL) {
        return ENOMEM;
    }
    
    memset(swap_table.entries, 0, sizeof(struct swap_entry) * SWAP_MAX_PAGES);
    swap_table.count = 0;
    
    return 0;
}

/* Initialize swap subsystem */
int swap_init(void) {
    int result;
    char path[] = SWAP_DEVICE;

    /* Initialize the lock */
    spinlock_init(&swap_manager.swap_lock);

    /* Open the swap device */
    result = vfs_open(path, O_RDWR, 0, &swap_manager.swap_dev);
    if (result) {
        return result;
    }

    /* Allocate the bitmap for swap slots */
    swap_manager.swap_map = kmalloc(SWAP_MAX_PAGES / 8);
    if (swap_manager.swap_map == NULL) {
        vfs_close(swap_manager.swap_dev);
        return ENOMEM;
    }

    /* Initialize swap space parameters */
    swap_manager.total_slots = SWAP_MAX_PAGES;
    swap_manager.free_slots = SWAP_MAX_PAGES;
    
    /* Clear the bitmap */
    memset(swap_manager.swap_map, 0, SWAP_MAX_PAGES / 8);

    /* Initialize swap table */
    result = swap_table_init();
    if (result) {
        kfree(swap_manager.swap_map);
        vfs_close(swap_manager.swap_dev);
        return result;
    }

    return 0;
}

/* Find first free swap slot */
static int find_free_slot(void) {
    unsigned int i, j;
    unsigned char byte;

    for (i = 0; i < SWAP_MAX_PAGES / 8; i++) {
        byte = swap_manager.swap_map[i];
        if (byte != 0xFF) {
            /* Found a byte with free bit */
            for (j = 0; j < 8; j++) {
                if ((byte & (1u << j)) == 0) {
                    return (i * 8) + j;
                }
            }
        }
    }
    return SWAP_FULL;
}

/* Mark a slot as used/free in bitmap */
static void mark_slot(unsigned int slot, int used) {
    unsigned int byte_index = slot / 8;
    unsigned int bit_index = slot % 8;

    if (used) {
        swap_manager.swap_map[byte_index] |= (1u << bit_index);
        swap_manager.free_slots--;
    } else {
        swap_manager.swap_map[byte_index] &= ~(1u << bit_index);
        swap_manager.free_slots++;
    }
}

/* Add entry to swap table */
struct swap_entry *swap_table_add(pid_t pid, vaddr_t vaddr, unsigned int slot) {
    unsigned int i;
    struct swap_entry *entry = NULL;
    
    /* Find empty slot */
    for (i = 0; i < SWAP_MAX_PAGES; i++) {
        if (swap_table.entries[i].pid == 0) {
            entry = &swap_table.entries[i];
            break;
        }
    }
    
    if (entry == NULL) {
        return NULL;
    }
    
    entry->pid = pid;
    entry->vaddr = vaddr;
    entry->slot = slot;
    swap_table.count++;
    
    return entry;
}

/* Find entry in swap table */
struct swap_entry *swap_table_find(pid_t pid, vaddr_t vaddr) {
    unsigned int i;
    
    for (i = 0; i < SWAP_MAX_PAGES; i++) {
        if (swap_table.entries[i].pid == pid && 
            swap_table.entries[i].vaddr == vaddr) {
            return &swap_table.entries[i];
        }
    }
    
    return NULL;
}

/* Remove entry from swap table */
void swap_table_remove(struct swap_entry *entry) {
    entry->pid = 0;
    entry->vaddr = 0;
    entry->slot = 0;
    swap_table.count--;
}

/* Swap out a page to disk */
int swap_out_page(struct page_table *pt, vaddr_t vaddr) {
    int result;
    int slot;
    struct pte *pte;
    struct uio ku;
    struct iovec iov;
    struct swap_entry *entry;
    
    /* Verify this is a kuseg/kseg2 address */
    if (vaddr < 0xC0000000 && vaddr >= 0x80000000) {
        return SWAP_INVALID;
    }
    
    spinlock_acquire(&swap_manager.swap_lock);
    
    /* Find a free swap slot */
    slot = find_free_slot();
    if (slot < 0) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }
    
    /* Write page to swap slot */
    uio_kinit(&iov, &ku, (void *)vaddr, 
              PAGE_SIZE, SWAP_PAGE_TO_OFFSET(slot), UIO_WRITE);
    
    result = VOP_WRITE(swap_manager.swap_dev, &ku);
    if (result) {
        spinlock_release(&swap_manager.swap_lock);
        return result;
    }
    
    if (ku.uio_resid != 0) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_IO_ERROR;
    }
    
    /* Add entry to swap table */
    entry = swap_table_add(pt->pid, vaddr, slot);
    if (entry == NULL) {
        mark_slot(slot, 0);
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_ERROR;
    }
    
    /* Mark slot as used */
    mark_slot(slot, 1);
    
    /* Update page table entry */
    pte = pte_get(pt, vaddr);
    if (pte != NULL) {
        pte->valid = 0;
    }
    
    /* Invalidate TLB entry */
    tlb_invalidate(vaddr);
    
    spinlock_release(&swap_manager.swap_lock);
    return slot;
}

/* Swap in a page from disk */
int swap_in_page(struct page_table *pt, vaddr_t vaddr, unsigned int swap_slot) {
    int result;
    struct pte *pte;
    struct uio ku;
    struct iovec iov;
    struct swap_entry *entry;
    paddr_t newpaddr;
    
    /* Verify this is a kuseg/kseg2 address */
    if (vaddr < 0xC0000000 && vaddr >= 0x80000000) {
        return SWAP_INVALID;
    }
    
    spinlock_acquire(&swap_manager.swap_lock);
    
    /* Find swap entry */
    entry = swap_table_find(pt->pid, vaddr);
    if (entry == NULL || entry->slot != swap_slot) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_INVALID;
    }
    
    /* Allocate new physical page */
    newpaddr = pmm_alloc_page();
    if (newpaddr == 0) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_NOMEM;
    }
    
    /* Read page from swap */
    uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(newpaddr),
              PAGE_SIZE, SWAP_PAGE_TO_OFFSET(entry->slot), UIO_READ);
    
    vfs_biglock_acquire();
    result = VOP_READ(swap_manager.swap_dev, &ku);
    vfs_biglock_release();
    
    if (result || ku.uio_resid != 0) {
        pmm_free_page(newpaddr);
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_IO_ERROR;
    }
    
    /* Update page table */
    pte = pte_get(pt, vaddr);
    if (pte == NULL) {
        pmm_free_page(newpaddr);
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_ERROR;
    }
    
    pte->pfn = newpaddr >> PAGE_SHIFT;
    pte->valid = 1;
    
    /* Free swap slot and remove entry */
    mark_slot(entry->slot, 0);
    swap_table_remove(entry);
    
    /* Update TLB */
    tlb_update(pt, vaddr);
    
    spinlock_release(&swap_manager.swap_lock);
    return 0;
}

/* Utility functions */
int swap_free_slot(unsigned int swap_slot) {
    if (swap_slot >= SWAP_MAX_PAGES) {
        return SWAP_INVALID;
    }
    
    spinlock_acquire(&swap_manager.swap_lock);
    mark_slot(swap_slot, 0);
    spinlock_release(&swap_manager.swap_lock);
    
    return 0;
}

unsigned int swap_free_count(void) {
    return swap_manager.free_slots;
}

bool swap_is_full(void) {
    return swap_manager.free_slots == 0;
}

void swap_shutdown(void) {
    if (swap_manager.swap_dev) {
        vfs_close(swap_manager.swap_dev);
        swap_manager.swap_dev = NULL;
    }
    
    if (swap_manager.swap_map) {
        kfree(swap_manager.swap_map);
        swap_manager.swap_map = NULL;
    }
    
    if (swap_table.entries) {
        kfree(swap_table.entries);
        swap_table.entries = NULL;
    }
    
    spinlock_cleanup(&swap_manager.swap_lock);
}