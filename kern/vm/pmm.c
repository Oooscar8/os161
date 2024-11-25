#include <types.h>
#include <lib.h>
#include <vm.h>
#include <pmm.h>
#include <spinlock.h>
#include <kern/errno.h>

/* Global variables */
static unsigned long *bitmap;     /* Bitmap array */
static struct spinlock pmm_lock;  /* Lock for PMM operations */
static size_t total_pages;        /* Total number of pages */
static size_t free_pages;         /* Number of free pages */
static paddr_t base_addr;         /* First free physical address */

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
    base_addr = ram_getfirstfree();

    /* Calculate total number of pages */
    total_pages = (last_addr - base_addr) / PAGE_SIZE;
    free_pages = total_pages;

    /* Check if we have any memory to manage */
    if (total_pages == 0) {
        return ENOMEM;
    }

    /* Calculate bitmap size (in bytes) and round up to word size */
    bitmap_size = (total_pages + BITS_PER_WORD - 1) / BITS_PER_WORD * sizeof(unsigned long);

    /* Allocate bitmap array */
    bitmap = kmalloc(bitmap_size);
    if (bitmap == NULL) {
        return ENOMEM;
    }

    /* Initialize bitmap - clear all bits (0 = free, 1 = allocated) */
    memset(bitmap, 0, bitmap_size);

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

    /* Check if we have free pages */
    if (free_pages == 0) {
        spinlock_release(&pmm_lock);
        return 0;
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
    addr = base_addr + (page_index * PAGE_SIZE);

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
    base_paddr = base_addr + (start_index * PAGE_SIZE);

    /* Release the lock */
    spinlock_release(&pmm_lock);

    return base_paddr;
}

/* Free a physical page */
int pmm_free_page(paddr_t addr)
{
    size_t page_index;

    /* Verify the address is page-aligned */
    if ((addr & PAGE_FRAME) != addr) {
        return EINVAL;
    }

    /* Calculate page index */
    if (addr < base_addr) {
        return EINVAL;
    }
    page_index = (addr - base_addr) / PAGE_SIZE;
    if (page_index >= total_pages) {
        return EINVAL;
    }

    /* Acquire the lock */
    spinlock_acquire(&pmm_lock);

    /* Check if page is already free */
    if (!BITMAP_TEST(bitmap, page_index)) {
        spinlock_release(&pmm_lock);
        return EINVAL;
    }

    /* Mark page as free */
    BITMAP_CLEAR(bitmap, page_index);
    free_pages++;

    /* Release the lock */
    spinlock_release(&pmm_lock);

    return 0;
}