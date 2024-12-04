#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <types.h>
#include <spinlock.h>

/*
 * Page size and address translation constants
 */
#define PAGE_SHIFT         12          /* Bits to shift for page offset */
#define PDE_SHIFT          22          /* Bits to shift for PDE index */
#define PTE_SHIFT          12          /* Bits to shift for PTE index */
#define PAGE_MASK          (PAGE_SIZE - 1)  /* Mask for page offset */
#define KERNEL_BASE        0x80000000  /* Start of kernel virtual address space */

/* Page table size constants */
#define PT_ENTRIES_PER_PAGE 1024    /* Number of PTEs per page table */
#define PD_ENTRIES          1024    /* Number of PDEs per page directory */

/*
 * PTE flags
 */
#define PTE_VALID          0x00000001  /* Valid mapping */
#define PTE_DIRTY          0x00000002  /* Page has been written */
#define PTE_ACCESSED       0x00000004  /* Page has been accessed */
#define PTE_WRITE          0x00000008  /* Page is writable */
#define PTE_USER           0x00000010  /* User mode accessible */
#define PTE_NOCACHE        0x00000020  /* Disable caching */
#define PTE_CACHE_MASK     0x00000070  /* Cache control mask */
#define PTE_PFN_MASK       0xFFFFF000  /* Physical page frame number mask */

/*
 * PDE flags
 */
#define PDE_VALID          0x00000001  /* Valid page table */
#define PDE_WRITE          0x00000008  /* Pages are writable */
#define PDE_USER           0x00000010  /* User mode accessible */
#define PDE_PFN_MASK       0xFFFFF000  /* Page table physical address mask */

/*  
 * Protection flags combinations
 */
#define PROT_NONE          0x0         /* No access */
#define PROT_READ          0x1         /* Read access */
#define PROT_WRITE         0x2         /* Write access */
#define PROT_EXEC          0x4         /* Execute access */
#define PROT_KERNEL        0x8         /* Kernel only access */

/* Address translation macros */
#define PDE_INDEX(va)      (((va) >> PDE_SHIFT) & 0x3FF)
#define PTE_INDEX(va)      (((va) >> PTE_SHIFT) & 0x3FF)
#define PAGE_OFFSET(va)    ((va) & PAGE_MASK)
#define PAGE_ALIGN(va)     (((va) + PAGE_MASK) & ~PAGE_MASK)

/* 
 * Page Table Entry 
 */
struct pte {
    uint32_t valid:1;      /* Valid bit */
    uint32_t dirty:1;      /* Dirty bit */
    uint32_t accessed:1;   /* Accessed bit */ 
    uint32_t write:1;      /* Write permission */
    uint32_t user:1;       /* User mode access */
    uint32_t nocache:1;    /* Cache disable */
    uint32_t cacheid:2;    /* Cache control */
    uint32_t swap:1;       /* Swap slot */
    uint32_t pfn_or_swap_slot:20;       /* Physical page frame number or swap slot number */
    uint32_t _reserved:3;  /* Reserved */
};

/* 
 * Page Directory Entry
 */
struct pde {
    uint32_t valid:1;      /* Valid bit */
    uint32_t write:1;      /* Write permission */
    uint32_t user:1;       /* User mode access */
    uint32_t _reserved:9;  /* Reserved */
    uint32_t pt_pfn:20;    /* Page table virtual addr */
};

/*
 * Page Table Structure
 */
struct page_table {
    struct pde *pgdir;           /* Page directory pointer */
    struct spinlock pt_lock;     /* Page table lock */
    pid_t pid;                   /* Process ID */
    vaddr_t heap_start;         /* Start of process heap */
    vaddr_t heap_end;           /* End of process heap */
};

struct page_table *kernel_pt;

struct page_table **pt_list;

/*
 * Function Declarations
 */

/**
 * Initialize the page table system.
 * Must be called during kernel initialization before any memory management operations.
 * Initializes global page table structures and memory management subsystems.
 * 
 * @return: void
 */
void pagetable_init(void);

/**
 * Early bootstrap initialization of the page table system.
 * Called very early in boot before MM is fully operational.
 * Sets up initial kernel page tables and enables virtual memory.
 * 
 * @return: void
 * @note: Must be called before pagetable_init()
 */
void pagetable_bootstrap(void);

/*
 * Page Table Management Functions
 */

/**
 * Create a new page table for a process.
 * Allocates and initializes a new page directory and associated structures.
 * 
 * @return: Pointer to new page_table structure on success, NULL on failure
 * @note: Returns with page table lock NOT held
 */
struct page_table *pagetable_create(void);

/**
 * Destroy a page table and free all associated resources.
 * Unmaps all pages, frees page tables, and releases associated memory.
 * 
 * @param pt: Page table to destroy
 * @note: Caller must ensure no references to this page table remain
 */
void pagetable_destroy(struct page_table *pt);

/**
 * Activate a page table for the current CPU.
 * Loads page table base register and flushes TLB.
 * 
 * @param pt: Page table to activate
 * @return: 0 on success, error code on failure
 * @note: Must be called with interrupts disabled
 */
int pagetable_activate(struct page_table *pt);

/*
 * Memory Mapping Operations
 */

/**
 * Map a virtual page to a physical page.
 * Creates PDE/PTE entries as needed and establishes mapping.
 * 
 * @param pt: Page table to modify
 * @param vaddr: Virtual address to map (must be page-aligned)
 * @param paddr: Physical address to map to (must be page-aligned)
 * @param flags: Protection and control flags (PTE_*)
 * @return: 0 on success, error code on failure
 */
int pte_map(struct page_table *pt, vaddr_t vaddr, paddr_t paddr, uint32_t flags);

/**
 * Remove mapping for a virtual page.
 * Invalidates PTE and frees page table if empty.
 * 
 * @param pt: Page table to modify
 * @param vaddr: Virtual address to unmap (must be page-aligned)
 * @return: 0 on success, error code on failure
 */
int pte_unmap(struct page_table *pt, vaddr_t vaddr);


/**
 * Modify protection flags for an existing mapping.
 * 
 * @param pt: Page table to modify
 * @param vaddr: Virtual address to modify (must be page-aligned)
 * @param flags: New protection flags
 * @return: 0 on success, error code on failure
 */
int pte_set_flags(struct page_table *pt, vaddr_t vaddr, uint32_t flags);

/*
 * Address Translation Functions
 */

/**
 * Translate virtual to physical address.
 * Performs page table walk to find physical address.
 * 
 * @param pt: Page table to use
 * @param vaddr: Virtual address to translate
 * @param flags: If non-NULL, protection flags are stored here
 * @return: Physical address if found, 0 if not mapped
 */
paddr_t pagetable_translate(struct page_table *pt, vaddr_t vaddr, uint32_t *flags);

/**
 * Check access permissions for a virtual address.
 * 
 * @param pt: Page table to check
 * @param vaddr: Virtual address to check
 * @param prot: Desired access rights (PROT_*)
 * @return: 0 if access allowed, error code if not
 */
int pagetable_check_access(struct page_table *pt, vaddr_t vaddr, uint32_t prot);

/*
 * Memory Region Operations
 */

/**
 * Map a contiguous region of virtual addresses to physical addresses.
 * 
 * @param pt: Page table to modify
 * @param vaddr: Starting virtual address (must be page-aligned)
 * @param paddr: Starting physical address (must be page-aligned)
 * @param npages: Number of pages to map
 * @param flags: Protection and control flags
 * @return: 0 on success, error code on failure
 */
int pagetable_map_region(struct page_table *pt, vaddr_t vaddr, 
                        paddr_t paddr, size_t npages, uint32_t flags);

/**
 * Unmap a contiguous region of virtual addresses.
 * 
 * @param pt: Page table to modify
 * @param vaddr: Starting virtual address (must be page-aligned)
 * @param npages: Number of pages to unmap
 * @return: 0 on success, error code on failure
 */
int pagetable_unmap_region(struct page_table *pt, vaddr_t vaddr, size_t npages);

/**
 * Get PTE entry for a virtual address.
 * Obtains and returns PTE pointer for given virtual address.
 * 
 * @param pt: Page table to lookup
 * @param vaddr: Virtual address to get PTE for (must be page-aligned)
 * @return: Pointer to PTE if found and valid, NULL if not mapped
 * @note: Returns with pt_lock held if PTE found, caller must release
 */
struct pte *pte_get(struct page_table *pt, vaddr_t vaddr);
/*
 * pagetable_copy - Copy page table from source to destination
 * 
 * Arguments:
 *   src_pt  - Source page table to copy from
 *   dst_pt  - Destination page table to copy to
 *
 * Returns:
 *   PT_OK         - Copy successful
 *   PT_NOMEM      - Memory allocation failed
 *   PT_NOTPRESENT - Invalid source page table
 * 
 * This function creates a deep copy of the source page table (src_pt) into the 
 * destination page table (dst_pt). It follows this process:
 *
 * 1. For each valid page directory entry (PDE) in the source:
 *    - If needed, allocates a new page table in the destination
 *    - Copies page table entries (PTEs) from source to destination
 * 
 * 2. For each valid page table entry (PTE) in each page table:
 *    - Copies all attributes (valid, write, user, nocache, etc.)
 *    - Maps to the same physical pages as the source
 *
 * Note: The function acquires the spinlocks of both page tables during copy
 * to ensure consistency. The physical pages themselves are not copied, only
 * the page table structures and mappings.
 *
 * Error handling:
 * - If memory allocation fails during copying, all allocated memory is freed
 *   and PT_NOMEM is returned
 * - If source page table is invalid, returns PT_NOTPRESENT
 */
int pagetable_copy(struct page_table *src_pt, struct page_table *dst_pt);

/* Error codes */
#define PT_OK           0    /* Success */
#define PT_ERROR       -1    /* General error */
#define PT_NOMEM      -2    /* Out of memory */
#define PT_BADADDR    -3    /* Invalid address */
#define PT_READONLY   -4    /* Read-only page */
#define PT_PRESENT    -5    /* Page already present */
#define PT_NOTPRESENT -6    /* Page not present */
#define PT_PERM       -7    /* Permission denied */
#define PT_ALIGN      -8    /* Address not aligned */
#define PT_NOASID     -9    /* No ASID available */

#endif /* _PAGETABLE_H_ */