#ifndef _SWAP_H_
#define _SWAP_H_

#include <types.h>
#include <vnode.h>
#include <spinlock.h>
#include <vm.h>
#include <pagetable.h>

/* 
 * Swap disk configurations
 */
#define SWAP_DEVICE "lhd0raw:"    /* Raw disk device for swap */
#define SWAP_MAX_PAGES 4096       /* Maximum pages that can be swapped */

/*
 * Swap error codes 
 */
#define SWAP_SUCCESS    0         /* Operation successful */
#define SWAP_FULL      -1         /* No free swap slots */
#define SWAP_IO_ERROR  -2         /* I/O error occurred */
#define SWAP_INVALID   -3         /* Invalid swap entry */
#define SWAP_NOMEM     -4         /* Out of memory */

/*
 * Swap manager structure 
 * Controls the swap space on disk
 */
struct swap_manager {
    struct vnode *swap_dev;       /* Swap device vnode */
    struct spinlock swap_lock;    /* Lock for swap operations */
    unsigned char *swap_map;       /* Bitmap of swap slots */
    unsigned int total_slots;     /* Total number of swap slots */
    unsigned int free_slots;      /* Number of free slots */
};

struct swap_entry {
    vaddr_t vaddr;       /* Virtual address of the swapped page */
    pid_t pid;    /* Process ID */
    unsigned int slot;   /* Swap slot number */
};

struct swap_table {
    struct swap_entry *entries;   /* Array of swap entries */
    unsigned int count;           /* Number of used entries */
};

/* Global instance declarations */
extern struct swap_manager swap_manager;
extern struct swap_table swap_table;


/*
 * Function declarations
 */

/*
 * Swap table management
 */
int swap_table_init(void);
struct swap_entry *swap_table_add(pid_t pid, vaddr_t vaddr, unsigned int slot);
struct swap_entry *swap_table_find(pid_t pid, vaddr_t vaddr);
void swap_table_remove(struct swap_entry *entry);


/*
 * Swap device and map management
 */
/**
 * Initialize swap subsystem
 * Opens swap device and sets up management structures
 * 
 * @return: 0 on success, error code on failure
 */
int swap_init(void);

/**
 * Shut down swap subsystem
 * Cleans up swap space and closes swap device
 */
void swap_shutdown(void);

/**
 * Get number of free swap slots
 * 
 * @return: Number of free slots available
 */
unsigned int swap_free_count(void);

/**
 * Check if swap is full
 * 
 * @return: true if no free slots available, false otherwise
 */
bool swap_is_full(void);


/*
 * Page swapping operations
 */
/**
 * Swap out a page to disk
 * 
 * @param pt: Page table containing the page
 * @param vaddr: Virtual address of page to swap out
 * @param paddr: Physical address of page to swap out
 * @return: Swap slot number on success, error code on failure
 */
int swap_out_page(struct page_table *pt, vaddr_t vaddr);

/**
 * Swap in a page from disk
 * 
 * @param pt: Page table to update
 * @param vaddr: Virtual address to swap in to
 * @param paddr: Physical address to swap in to
 * @param swap_slot: Swap slot to read from
 * @return: 0 on success, error code on failure
 */
int swap_in_page(struct page_table *pt, vaddr_t vaddr, unsigned int swap_slot);

/**
 * Free a swap slot
 * 
 * @param swap_slot: Slot number to free
 * @return: 0 on success, error code on failure
 */
int swap_free_slot(unsigned int swap_slot);


/* 
 * Utility macros 
 */
/* Convert page number to swap device offset */
#define SWAP_PAGE_TO_OFFSET(page_num) ((off_t)(page_num) * PAGE_SIZE)


#endif /* _SWAP_H_ */