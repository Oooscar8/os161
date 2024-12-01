#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <vaa.h>
#include <cpu.h>
#include <addrspace.h>
#include <pmm.h>

/* Constants for virtual memory regions */
#define KSEG2_END 0xFFFFFFFE /* End of KSEG2 */

/* Bitmap operations */
#define BITS_PER_WORD (sizeof(unsigned long) * 8)
#define WORD_OFFSET(bit) ((bit) / BITS_PER_WORD)
#define BIT_OFFSET(bit)  ((bit) % BITS_PER_WORD)
#define BITMAP_SET(bitmap, bit)   ((bitmap)[WORD_OFFSET(bit)] |= (1UL << BIT_OFFSET(bit)))
#define BITMAP_CLEAR(bitmap, bit) ((bitmap)[WORD_OFFSET(bit)] &= ~(1UL << BIT_OFFSET(bit)))
#define BITMAP_TEST(bitmap, bit)  ((bitmap)[WORD_OFFSET(bit)] & (1UL << BIT_OFFSET(bit)))

/* Global state */
static volatile unsigned long *volatile kernel_bitmap = NULL;  /* Bitmap for kernel pages */
static volatile size_t kernel_total_pages = 0;       /* Total pages in kernel space */
static volatile size_t kernel_free_pages = 0;        /* Number of free kernel pages */
static struct spinlock vaa_lock; /* Lock for VAA operations */

/* Internal functions */
static void init_kernel_region(void)
{
    size_t bitmap_size;

    /* Calculate total pages in kernel space */
    kernel_total_pages = 200;
    kernel_free_pages = kernel_total_pages;

    /* Calculate bitmap size (in bytes) and round up to word size */
    bitmap_size = (kernel_total_pages + BITS_PER_WORD - 1) / BITS_PER_WORD * sizeof(unsigned long);

    /* Allocate bitmap array */
    kernel_bitmap = kmalloc(bitmap_size);
    if (kernel_bitmap == NULL) {
        panic("VAA: Failed to allocate kernel bitmap\n");
    }

    /* Initialize bitmap - clear all bits (0 = free, 1 = allocated) */
    //memset(kernel_bitmap, 0, bitmap_size);
}

/* Find first fit of n continuous free pages */
static ssize_t find_continuous_pages(size_t npages)
{
    size_t current_count = 0;
    size_t i;

    for (i = 0; i < kernel_total_pages; i++) {
        if (!BITMAP_TEST(kernel_bitmap, i)) {
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

/* Convert page index to virtual address */
static inline vaddr_t index_to_vaddr(size_t index)
{
    return MIPS_KSEG2 + (index * PAGE_SIZE);
}

/* Convert virtual address to page index */
static inline ssize_t vaddr_to_index(vaddr_t addr)
{
    if (addr < MIPS_KSEG2 || addr >= KSEG2_END) {
        return -1;
    }
    return (addr - MIPS_KSEG2) / PAGE_SIZE;
}

/* Interface implementation */
int vaa_init(void)
{
    init_kernel_region();
    spinlock_init(&vaa_lock);
    return 0;
}

vaddr_t vaa_alloc_kpage(void)
{
    ssize_t page_index;
    vaddr_t addr;

    if (kernel_free_pages == 0) {
        return 0;
    }

    /* Find a free page */
    spinlock_acquire(&vaa_lock);
    page_index = find_continuous_pages(1);
    if (page_index < 0) {
        return 0;
    }

    /* Mark page as allocated */
    BITMAP_SET(kernel_bitmap, page_index);
    kernel_free_pages--;
    spinlock_release(&vaa_lock);

    /* Convert index to virtual address */
    addr = index_to_vaddr(page_index);
    return addr;
}

int vaa_free_kpage(vaddr_t addr)
{
    ssize_t page_index;

    /* Verify the address is page-aligned */
    if ((addr & PAGE_FRAME) != addr) {
        return EINVAL;
    }

    /* Convert address to page index and validate */
    page_index = vaddr_to_index(addr);
    if (page_index < 0) {
        return EINVAL;
    }

    spinlock_acquire(&vaa_lock);

    /* Check if page is already free */
    if (!BITMAP_TEST(kernel_bitmap, page_index)) {
        spinlock_release(&vaa_lock);
        return EINVAL; /* Page already free */
    }

    /* Mark page as free */
    BITMAP_CLEAR(kernel_bitmap, page_index);
    kernel_free_pages++;

    spinlock_release(&vaa_lock);
    return 0;
}

vaddr_t vaa_alloc_npages(size_t npages)
{
    ssize_t start_index;
    vaddr_t base_addr;

    if (npages == 0 || kernel_free_pages < npages) {
        return 0;
    }

    /* Find continuous free pages */
    start_index = find_continuous_pages(npages);
    if (start_index < 0) {
        return 0;
    }

    /* Mark pages as allocated */
    for (size_t i = 0; i < npages; i++) {
        BITMAP_SET(kernel_bitmap, start_index + i);
    }
    kernel_free_pages -= npages;

    /* Convert start index to virtual address */
    base_addr = index_to_vaddr(start_index);
    return base_addr;
}