#include <types.h>
#include <lib.h>
#include <vm.h>
#include <pmm.h>
#include <spinlock.h>
#include <kern/errno.h>
#include <pr.h>

/* Global variables */
static volatile unsigned long *volatile bitmap;     /* Bitmap array */
static struct spinlock pmm_lock;  /* Lock for PMM operations */
static size_t total_pages;        /* Total number of pages */
static size_t free_pages;         /* Number of free pages */

/* Bitmap operations */
#define BITS_PER_WORD (sizeof(unsigned long) * 8)
#define WORD_OFFSET(bit) ((bit) / BITS_PER_WORD)
#define BIT_OFFSET(bit)  ((bit) % BITS_PER_WORD)
#define BITMAP_SET(bitmap, bit)   ((bitmap)[WORD_OFFSET(bit)] |= (1UL << BIT_OFFSET(bit)))
#define BITMAP_CLEAR(bitmap, bit) ((bitmap)[WORD_OFFSET(bit)] &= ~(1UL << BIT_OFFSET(bit)))
#define BITMAP_TEST(bitmap, bit)  ((bitmap)[WORD_OFFSET(bit)] & (1UL << BIT_OFFSET(bit)))

/* Initialize the PMM */
int pmm_init(void)
{
    paddr_t last_addr;
    size_t bitmap_size;

    /* Initialize the spinlock */
    spinlock_init(&pmm_lock);

    /* Get physical memory range */
    last_addr = ram_getsize();
    paddr_t base_addr = ram_getfirstfree();

    base_addr = ROUNDUP(base_addr, PAGE_SIZE);

    /* Calculate total number of pages */
    free_pages = (last_addr - base_addr) / PAGE_SIZE;
    total_pages = last_addr / PAGE_SIZE;

    /* Check if we have any memory to manage */
    if (free_pages == 0) {
        return ENOMEM;
    }

    /* Calculate bitmap size (in bytes) and round up to word size */
    bitmap_size = (total_pages + BITS_PER_WORD - 1) / BITS_PER_WORD * sizeof(unsigned long);

    /* Allocate bitmap array */
    bitmap = kmalloc(bitmap_size);
    if (bitmap == NULL) {
        return ENOMEM;
    }

    /* Initialize bitmap - (0 = free, 1 = allocated) */
    size_t early_pages = total_pages - free_pages;
    for (size_t i = 0; i < early_pages + 1; i++) {
        BITMAP_SET(bitmap, i);
    }

    return 0;
}

/* Find first fit of n continuous free pages */
static ssize_t find_continuous_pages(size_t npages)
{
    size_t current_count = 0;
    size_t i;

    for (i = 0; i < total_pages; i++) {
        if (!BITMAP_TEST(bitmap, i)) {
            current_count++;
            if (current_count == npages) {
                return i - npages + 1; // Return start index
            }
        } else {
            current_count = 0;
        }
    }
    return -1; // Not found
}

/* Allocate a physical page */
paddr_t pmm_alloc_page(void)
{
    ssize_t page_index;
    paddr_t addr;

    /* Acquire the lock */
    spinlock_acquire(&pmm_lock);

    // /* Check if we have free pages */
    // if (free_pages == 0) {
    //     spinlock_release(&pmm_lock);
    //     return 0;
    // }

    /* Check if we have free pages */
    if (free_pages == 0) {
        /* Release lock before trying to evict */
        spinlock_release(&pmm_lock);
        
        /* Try to evict a page */
        int result = evict_page(&addr, false);  // false means don't use reserved slot
        if (result != PR_SUCCESS) {
            panic("pmm_alloc_page: failed to evict page\n");
            return 0;
        }

        return addr;
    }

    /* Find a free page */
    page_index = find_continuous_pages(1);
    if (page_index < 0) {
        spinlock_release(&pmm_lock);
        return 0;
    }

    /* Mark page as allocated */
    BITMAP_SET(bitmap, page_index);
    free_pages--;

    /* Calculate physical address */
    addr = page_index * PAGE_SIZE;

    /* Release the lock */
   spinlock_release(&pmm_lock);

    return addr;
}

/* Allocate n continuous physical pages */
paddr_t pmm_alloc_npages(size_t npages)
{
    ssize_t start_index;
    paddr_t base_paddr;

    if (npages == 0) {
        return 0;
    }

    /* Acquire the lock */
    spinlock_acquire(&pmm_lock);

    /* Check if we have enough free pages */
    if (free_pages < npages) {
        spinlock_release(&pmm_lock);
        return 0;
    }

    /* Find continuous free pages */
    start_index = find_continuous_pages(npages);
    if (start_index < 0) {
        spinlock_release(&pmm_lock);
        return 0;
    }

    /* Mark pages as allocated */
    for (size_t i = 0; i < npages; i++) {
        BITMAP_SET(bitmap, start_index + i);
    }
    free_pages -= npages;

    /* Calculate base physical address */
    base_paddr = start_index * PAGE_SIZE;

    /* Release the lock */
    spinlock_release(&pmm_lock);

    return base_paddr;
}

/* Free a physical page */
int pmm_free_page(paddr_t addr)
{
    if (addr == 0) {
        return 0;
    }
    size_t page_index;

    /* Verify the address is page-aligned */
    if ((addr & PAGE_FRAME) != addr) {
        panic("pmm_free_page: invalid address\n");
        return EINVAL;
    }

    /* Calculate page index */
    if ( addr >= total_pages * PAGE_SIZE) {
        panic("pmm_free_page: address out of range\n");
        return EINVAL;
    }
    page_index = addr / PAGE_SIZE;
    if (page_index >= total_pages) {
        panic("pmm_free_page: address out of range\n");
        return EINVAL;
    }

    /* Acquire the lock */
    spinlock_acquire(&pmm_lock);

    /* Check if page is already free */
    if (!BITMAP_TEST(bitmap, page_index)) {
        spinlock_release(&pmm_lock);
        panic("pmm_free_page: page already free\n");
        return EINVAL;
    }

    /* Mark page as free */
    BITMAP_CLEAR(bitmap, page_index);
    free_pages++;

    /* Release the lock */
    spinlock_release(&pmm_lock);

    return 0;
}

/* Allocate n non-continuous physical pages */
paddr_t* pmm_alloc_npages_noncontiguous(size_t npages) {
    paddr_t *page_array = NULL;
    size_t pages_allocated = 0;

    if (npages == 0) {
        return NULL;
    }

    /* Allocate array to store physical addresses */
    page_array = kmalloc(sizeof(paddr_t) * npages);
    if (page_array == NULL) {
        return NULL;
    }

    /* Acquire the lock */
    spinlock_acquire(&pmm_lock);

    /* First try to allocate from free pages */
    for (size_t i = 0; i < total_pages && pages_allocated < npages; i++) {
        if (!BITMAP_TEST(bitmap, i)) {
            BITMAP_SET(bitmap, i);
            page_array[pages_allocated] = i * PAGE_SIZE;
            pages_allocated++;
            free_pages--;
        }
    }

    /* If we need more pages, try evicting */
    if (pages_allocated < npages) {
        /* Release lock before trying to evict */
        spinlock_release(&pmm_lock);

        /* Try to evict remaining needed pages */
        for (size_t i = pages_allocated; i < npages; i++) {
            paddr_t evicted_addr;
            int result = evict_page(&evicted_addr, false);  // false means don't use reserved slot
            if (result != PR_SUCCESS) {
                /* If eviction fails, call panic */
                panic('pmm_alloc_npages_noncontiguous: eviction failed\n');
                return NULL;
            }
            page_array[i] = evicted_addr;
            pages_allocated++;
        }

        // /* Reacquire lock to update bitmap for evicted pages */
        // spinlock_acquire(&pmm_lock);
        // for (size_t i = 0; i < npages; i++) {
        //     size_t page_index = page_array[i] / PAGE_SIZE;
        //     if (!BITMAP_TEST(bitmap, page_index)) {
        //         BITMAP_SET(bitmap, page_index);
        //     }
        // }
    }
    // /* Release the lock */
    // spinlock_release(&pmm_lock);

    return page_array;
}

/* Free multiple non-continuous pages */
void pmm_free_npages_noncontiguous(paddr_t *pages, size_t npages) {
    if (pages == NULL || npages == 0) {
        return;
    }

    spinlock_acquire(&pmm_lock);

    for (size_t i = 0; i < npages; i++) {
        /* Verify the address is valid */
        if (pages[i] == 0 || !(pages[i] & PAGE_MASK) || 
            pages[i] >= total_pages * PAGE_SIZE) {
            spinlock_release(&pmm_lock);
            panic("pmm_free_npages_noncontiguous: invalid address\n");
            return;
        }

        size_t page_index = pages[i] / PAGE_SIZE;
        
        /* Check if page is already free */
        if (!BITMAP_TEST(bitmap, page_index)) {
            spinlock_release(&pmm_lock);
            panic("pmm_free_npages_noncontiguous: page already free\n");
            return;
        }

        /* Free the page */
        BITMAP_CLEAR(bitmap, page_index);
        free_pages++;
    }

    spinlock_release(&pmm_lock);
}