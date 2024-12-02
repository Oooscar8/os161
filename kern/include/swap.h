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
#define SWAP_MAX_PAGES 4096       /* Maximum pages that can be swapped */

/*
 * Swap error codes 
 */
#define SWAP_SUCCESS    0         /* Operation successful */
#define SWAP_FULL      -1        /* No free swap slots */
#define SWAP_IO_ERROR  -2        /* I/O error occurred */
#define SWAP_INVALID   -3        /* Invalid swap entry */
#define SWAP_NOMEM     -4        /* Out of memory */
#define SWAP_ALIGN     -5        /* Alignment error */

/*
 * PTE manipulation macros for swap
 */
#define PTE_ONSWAP(pte) (!((pte)->valid) && ((pte)->swap))
#define PTE_GET_SWAP_SLOT(pte) ((pte)->pfn_or_swap_slot)
#define PTE_SET_SWAP_SLOT(pte, slot) do { \
    (pte)->valid = 0; \
    (pte)->swap = 1; \
    (pte)->pfn_or_swap_slot = (slot); \
} while(0)

/*
 * Structures for swap management
 */
struct swap_entry {
    pid_t pid;                   /* Process ID */
    bool used;                   /* Entry in use */
};

struct swap_manager {
    struct vnode *swap_dev;      /* Swap device vnode */
    struct spinlock swap_lock;   /* Lock for swap operations */
    struct swap_entry *entries;  /* Array of swap entries */
    unsigned int count;          /* Number of used entries */
    bool emergency_slot_use;     // Flag to allow using the last(reserved) slot
};

/* Global instance */
extern struct swap_manager swap_manager;

/* Function declarations */
int swap_init(void);
void swap_shutdown(void);
int swap_out_page(struct page_table *pt, vaddr_t vaddr);
int swap_in_page(struct page_table *pt, vaddr_t vaddr);

/* Utility macro */
#define SWAP_PAGE_TO_OFFSET(page_num) ((off_t)(page_num) * PAGE_SIZE)

#endif /* _SWAP_H_ */