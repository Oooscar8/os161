# The code reading exercises



> 1. What are the ELF magic numbers?

The ELF (Executable and Linkable Format) magic numbers are a sequence of bytes at the beginning of an ELF file that identify it as an ELF file and provide some basic information about its format. 

These magic numbers help operating systems and other tools quickly identify ELF files and determine their basic characteristics without having to parse the entire file structure.



> 2. What is the difference between UIO_USERISPACE and UIO_USERSPACE? When should one use UIO_SYSSPACE instead?

The `enum uio_seg` is typically used in Unix-like operating systems to specify the memory segment for I/O operations. 

```c
/* Source/destination. */
enum uio_seg {
        UIO_USERISPACE,			/* User process code. */
        UIO_USERSPACE,			/* User process data. */
        UIO_SYSSPACE,			/* Kernel. */
};
```

`UIO_USERISPACE`(User process instruction space) refers to the memory segment where the user process's executable code (instructions) resides.

`UIO_USERSPACE`(User process data space) refers to the memory segment where the user process's data resides.

`UIO_SYSSPACE` is used when the memory being accessed is in kernel space rather than user space. It is typically used within kernel code or drivers when performing I/O operations on kernel-owned memory.



> 3. Why can the struct `uio` that is used to read in a segment be allocated on the stack in load_segment() (i.e., where does the memory read actually go)?

The `uio` structure describes an I/O operation that will read data from the file (`vnode v`) into the user's address space at `vaddr`.

The struct `uio` is allocated on the stack in the `load_segment()` function. This is fine because the structure is just used to describe the I/O operation, not to hold the actual data being read. The actual read operation (`VOP_READ`) will use this information to copy the data directly into the user's address space.

The key is in this line:

```c
iov.iov_ubase = (userptr_t)vaddr;
```

Here, `vaddr` is the virtual address in the user's address space where the segment should be loaded. 



> 4. In `runprogram()`, why is it important to call `vfs_close()` before going to user mode?

Once the program is loaded, there's no need for the kernel to maintain access to the executable file. Closing the file before going to user mode ensures that the new user process doesn't have unnecessary open file handles.



> 5. What function forces the processor to switch into user mode? Is this function machine dependent?

 The function that forces the processor to switch into user mode is:

```c
/* Warp to user mode. */
enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
                  NULL /*userspace addr of environment*/,
                  stackptr, entrypoint);
```

This function is machine dependent. Specifically, it's designed for the MIPS architecture.



> 6. In what file are `copyin` and `copyout` defined? `memmove`? Why can't `copyin` and `copyout` be implemented as simply as `memmove`?

`copyin` and `copyout` are defined in `~/src/kern/vm/copyinout.c`

`memmove` is defined in `~/src/common/libc/string/memmove.c`

The `copyin` and `copyout` functions cannot be implemented as simply as `memmove` because they involve copying data between user space and kernel space, which introduces security and reliability concerns that `memmove` doesn't have to deal with.

There's a `copycheck` function call at the beginning of both `copyin` and `copyout` because the kernel must verify that the user-provided addresses are valid and within the user's accessible memory range. 

The complexity in `copyin` and `copyout` comes from setting up safeguards (like `tm_badfaultfunc` and `setjmp`) to catch and handle potential faults, performing necessary checks, and ensuring the operation is secure and doesn't compromise the system's integrity.

In contrast, `memmove` operates entirely within a single memory space (either all kernel or all user space) and can assume that all addresses it's working with are valid and accessible, which allows for a much simpler implementation.



> 7. What (briefly) is the purpose of `userptr_t`?

The purpose of `userptr_t` is to create a distinct pointer type for user-space addresses that cannot be accidentally confused or mixed with kernel-space pointers. It serves as a safeguard in the kernel code to clearly differentiate and safely handle pointers to user-space memory.



> 8. What is the numerical value of the exception code for a MIPS system call?

The exception code for a MIPS system call is 8.



> 9. How many bytes is an instruction in MIPS?

By examining the `syscall(struct trapframe *tf)` function, we can determine that each MIPS instruction is **4 bytes**. This is evident from the following code snippet:

```c
/*
 * Now, advance the program counter, to avoid restarting
 * the syscall over and over again.
 */
tf->tf_epc += 4;
```

The line `tf->tf_epc += 4;` increments the program counter by 4, indicating that each instruction in MIPS occupies 4 bytes.



> 10. Why do you "probably want to change" the implementation of `kill_curthread()`?

Since this function handles fatal faults in user-level code by calling `panic()`, which will crash the entire system. For a single user-level fault, we don't need to crash the entire kernel or system. 



> 11. What would be required to implement a system call that took more than 4 arguments?

To implement a system call with more than 4 arguments, the following steps would be necessary:

1. **Pass the first 4 arguments in registers `a0-a3`**.

2. **Fetch the remaining arguments from the user stack**: For arguments beyond the 4th, we need to get them from the user stack, in detail, fetching from `sp+16` to skip over the space reserved for the first 4 registerized arguments. 
   (We need use the `copyin()` function to safely retrieve these arguments from the user space into the kernel space. )



> 12. What is the purpose of the SYSCALL macro?

The main purpose of the `SYSCALL` macro is to simplify the definition and handling of system calls, ensuring that each system call is properly numbered and recognized by the kernel. 

1. **Generate Assembly Code for System Calls**: 
   It generates the necessary assembly code to load the system call number into the appropriate register and call the generic `__syscall` handler.

2. **Simplify the Registration Process**: 
   By using the `SYSCALL` macro, developers don't have to manually write the assembly code for each system call. The script ( `gensyscalls.sh`) parses this file and generates the required assembly stubs, making the system calls automatically recognizable and executable by the kernel.



> 13. What is the MIPS instruction that actually triggers a system call?

The instruction that actually triggers a system call is:
`syscall`



> 14. After reading syscalls-mips.S and syscall.c, you should be prepared to answer the following question:
>
>     OS/161 supports 64-bit values; lseek() takes and returns a 64-bit offset value. Thus, lseek() takes a 32-bit file handle (arg0), a 64-bit offset (arg1), a 32-bit whence (arg2), and needs to return a 64-bit offset value. In `void syscall(struct trapframe *tf)` where will you find each of the three arguments (in which registers) and how will you return the 64-bit offset?

- **arg0** (file handle, 32-bit) is found in the **`a0`** register.
- **arg1** (offset, 64-bit):
  - Lower 32 bits are stored in **`a2`**.
  - Upper 32 bits are stored in **`a3`**.
  - If registers are insufficient, additional arguments can be fetched from the user stack, starting at **`sp+16`** for the lower 32 bits and **`sp+20`** for the upper 32 bits.
- **arg2** (whence, 32-bit) is found on the user stack at **`sp+16`**.

To return the 64-bit offset value:

- **`v0`** holds the lower 32 bits of the return value.
- **`v1`** holds the upper 32 bits of the return value.



> 15. As you were reading the code in `runprogram.c` and `loadelf.c`, you probably noticed how the kernel manipulates the files. Which kernel function is called to open a file? Which macro is called to read the file? What about to write a file? Which data structure is used in the kernel to represent an open file?

**Kernel function to open a file**: 
The kernel function to open a file is `vfs_open()`. In `runprogram()`, the file is opened with the call `vfs_open(progname, O_RDONLY, 0, &v);`, where `O_RDONLY` specifies read-only mode.

**Macro to read a file**: 
The macro to read a file is `VOP_READ()`. In `loadelf.c`, it is used in `load_segment()` with the call `VOP_READ(v, &u);` to read data from the file.

**Macro to write a file**: 
Although not explicitly used in the provided code, the macro to write a file is `VOP_WRITE()`, which functions similarly to `VOP_READ()` but for writing.

**Data structure representing an open file in the kernel**: 
The kernel uses the `struct vnode` data structure to represent an open file. In `runprogram.c`, a `vnode` represents the opened program file, stored in the variable `v`.



> 16. What is the purpose of VOP_INCREF and VOP_DECREF?

**VOP_INCREF**: Increases the reference count of a file (or `vnode`) to indicate that it is in use. This prevents the file from being released while it is still being accessed.

**VOP_DECREF**: Decreases the reference count of a file. When the count reaches zero, it indicates that the file is no longer in use, allowing the kernel to safely release its resources.









