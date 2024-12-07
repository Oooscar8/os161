#ifndef _SWAP_H_
#define _SWAP_H_

#include <types.h>
#include <vm.h>
#include <vnode.h>
#include <spinlock.h>

/* 
 * Swap disk configurations 
 */
#define SWAP_DEVICE "lhd0raw:"    /* Raw disk device for swap */
#define SWAP_MAX_PAGES 1280       /* Maximum pages that can be swapped */

/* 
 * Bitmap operations for swap 
 */
/* Number of bits per word */
#define BITS_PER_WORD (sizeof(unsigned long) * 8)
/* Size of bitmap array in words, rounded up */
#define SWAP_BITMAP_WORDS ((SWAP_MAX_PAGES + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define SWAP_WORD_OFFSET(slot)  ((slot) / BITS_PER_WORD)
#define SWAP_BIT_OFFSET(slot)   ((slot) % BITS_PER_WORD)
#define SWAP_BITMAP_SET(bitmap, slot) \
    ((bitmap)[SWAP_WORD_OFFSET(slot)] |= (1UL << SWAP_BIT_OFFSET(slot)))
#define SWAP_BITMAP_CLEAR(bitmap, slot) \
    ((bitmap)[SWAP_WORD_OFFSET(slot)] &= ~(1UL << SWAP_BIT_OFFSET(slot)))
#define SWAP_BITMAP_TEST(bitmap, slot) \
    ((bitmap)[SWAP_WORD_OFFSET(slot)] & (1UL << SWAP_BIT_OFFSET(slot)))

/*
 * Swap error codes 
 */
#define SWAP_SUCCESS    0        /* Operation successful */
#define SWAP_FULL      -1        /* No free swap slots */
#define SWAP_IO_ERROR  -2        /* I/O error occurred */
#define SWAP_INVALID   -3        /* Invalid swap entry */
#define SWAP_NOMEM     -4        /* Out of memory */
#define SWAP_ALIGN     -5        /* Alignment error */

/*
 * PTE manipulation macros for swap
 */
#define PTE_ONSWAP(pte) (((pte)->valid) && ((pte)->swap))
#define PTE_GET_SWAP_SLOT(pte) ((pte)->pfn_or_swap_slot)
#define PTE_SET_SWAP_SLOT(pte, slot) do { \
    (pte)->swap = 1; \
    (pte)->pfn_or_swap_slot = (slot); \
} while(0)

/*
 * Structures for swap management
 */
struct swap_manager {
    struct vnode *swap_dev;      /* Swap device vnode */
    struct spinlock swap_lock;   /* Lock for swap operations */
    unsigned long bitmap[SWAP_BITMAP_WORDS]; /* Bitmap: 1=used, 0=free */
    unsigned int count;          /* Number of used entries */
    bool swap_in_progress;
    struct semaphore *swap_sem;
};

/* Global instance */
extern struct swap_manager swap_manager;

/* Function declarations */
int swap_init(void);
void swap_shutdown(void);
bool need_swap(void);
void do_swap(paddr_t *victim_pa, bool emergency);
int swap_out_page(struct page_table *pt, vaddr_t vaddr, bool emergency);
int swap_in_page(struct page_table *pt, vaddr_t vaddr);

/* Utility macro */
#define SWAP_PAGE_TO_OFFSET(page_num) ((off_t)(page_num) * PAGE_SIZE)

#endif /* _SWAP_H_ */