[toc]



# Assignment 1

> Reference: [Assignment 1](https://sites.google.com/view/cpen331fall2024/assignments/assignment-1)



## Step 4. Copy some output from git commands into your submit file

1. invoke sys161 with the newly built kernel

input:

```
cd $HOME/os161/root
sys161 kernel
```

output:

```
sys161: System/161 release 2.0.3, compiled Sep  5 2024 20:22:57

OS/161 base system version 1.99.08
Copyright (c) 2000, 2001-2005, 2008-2011, 2013, 2014
   President and Fellows of Harvard College.  All rights reserved.

Put-your-group-name-here's system version 0 (DUMBVM #2)

292k physical memory available
Device probe...
lamebus0 (system main bus)
emu0 at lamebus0
ltrace0 at lamebus0
ltimer0 at lamebus0
beep0 at ltimer0
rtclock0 at ltimer0
lrandom0 at lamebus0
random0 at lrandom0
lhd0 at lamebus0
lhd1 at lamebus0
lser0 at lamebus0
con0 at lser0

cpu0: MIPS/161 (System/161 2.x) features 0x0
OS/161 kernel [? for menu]: ?

OS/161 kernel menu
    [?o] Operations menu                [khgen] Next kernel heap generation
    [?t] Tests menu                     [khdump] Dump kernel heap
    [kh] Kernel heap stats              [q] Quit and shut down

Operation took 0.481954360 seconds
```

2. make sure that the git repository is working

input:

```
git log
```

output:

```
commit b2e7d28ff0d4113ea3b9e53e7aca73069feeb5d2 (HEAD -> master)
Author: root <root@localhost>
Date:   Fri Sep 6 20:24:16 2024 +0000

    Updated .gitignore

commit 5c4e39fa04a747a210a11fd9940768dda61981c0 (tag: asst1-start, origin/master, origin/HEAD)
Author: root <root@localhost>
Date:   Thu Sep 5 22:25:13 2024 +0000

    Ignore defs.mk file

commit 341e69822e0908f5c521e68e7c71f68e4c10119d
Author: Alex Sun <oscar8@sjtu.edu.cn>
Date:   Thu Sep 5 15:08:36 2024 -0700

    Initial commit
```

input:

```
git tag
```

output:
```
asst1-start
```



## Step 5. Complete the code reading exercises

> Reference: [Browse the source code](https://people.ece.ubc.ca/~os161/os161-site/browse-code.html)



> **Question 1:** In the book chapters and in class you were introduced to the mechanisms used to transfer control between user processes and the operating system. Tell us where we can find the first line of OS/161 code that is executed when a trap occurs. Then tell us where control gets transferred to from that point. What about an interrupt? How does that differ?

Look at the code in file `os161/src/kern/arch/mips/locore/exception-mips1.S`

When a trap occurs,

If it is an UTLB exception, the first line of `mips_utlb_handler` would be the first line of OS/161 code that is executed when the trap occurs.

If it is a general exception, the first line of `mips_general_handler` would be the first line of OS/161 code that is executed when the trap occurs.

After the `trapframe` is set up, it transfers control to `mips_trap`

`mips_trap` determines whether it is a system call or an interrupt or something else.

If it is an interrupt, it then transfers control to interrupt handler.



> **Question 2:** Making a system call, such as write, ultimately leads to a trap. Find the code in OS/161 that invokes system calls from user programs and causes traps. In which file and on which lines did you find this code?

In file `os161/src/kern/arch/mips/locore/trap.c` line **224**: 

```
syscall(tf);
```



> **Question 3:** Why do you suppose there are `libc` functions in the "common" part of the source tree (`common/libc`) as well as in `userland/lib/libc`?

The `common/libc` library provides implementations of C standard library functions to both the kernel and user programs, focusing mainly on core functionalities needed by the kernel, avoiding complexity of user-space features.

The `userland/lib/libc` library provides implementations of C standard library functions to the user space, dealing with more complex scenarios involving system calls…



> Below is a brief overview of the organization of the source tree, and a description of what goes where.

> * configure -- top-level configuration script; configures the OS/161 distribution and build system. It does not configure the operating system kernel, which is a slightly different kind of configuration.



> **Question 4:** Name two things that configure configures. What might invalidate that configuration and make you need/want to rerun it?

1. The configure file designates the target hardware platform and machine type.

2. The configure file designates the default location of the root of the installed system.

If I move my source tree to a different computer that is running a different OS, the configuration will be invalid and I need to rerun the configure file.



>- `Makefile` -- top-level `makefile`; builds the OS/161 distribution, including all the provided utilities, but does not build the operating system kernel.
>- `common/` -- code used both by the kernel and user-level programs, mostly standard C library functions.
>- `kern/` -- the kernel source code.
>- `kern/Makefile` -- Once again, there is a `Makefile`. This `Makefile` installs header files but does not build anything.
>- `kern/arch/` -- This is where architecture-specific code goes. By architecture-specific, we mean the code that differs depending on the hardware platform on which you're running. There are two directories here: mips which contains code specific to the MIPS processor and sys161 which contains code specific to the System/161 simulator.
>- `kern/arch/mips/conf/conf.arch` -- This tells the kernel config script where to find the machine-specific, low-level functions it needs (throughout `kern/arch/mips/*`).
>- `kern/arch/mips/include/` -- This folder and its subdirectories include files for the machine-specific constants and functions.



> **Question 5:** What are some of the details which would make a function "machine dependent"? Why might it be important to maintain this separation, instead of just putting all of the code in one function?

* Machine Dependent Details:

1. Functions that utilize specific processor instructions (assembly code) or rely on features unique to a particular CPU architecture

2. Functions accessing memory regions that are hard-wired to specific physical memory

3. Functions dealing with interrupts(traps) or low-level I/O operation

* It is important to separate machine-dependent functions from machine-independent functions for the following reasons:

1. Machine-independent functions can be reused across different architecture. It would be easier to port the software to different hardware architectures.
2. Keep the codebase organized and easy to understand, as well as easy to maintain and modify, without affecting other functions.



> * kern/arch/mips/* -- The other directories contain source files for the machine-dependent code that the kernel needs to run. A lot of this code is in assembler and will seem very low level, but understanding how it all fits together will help you immensely on Assignment 2.



> **Question 6:** How large is a `trapframe`? Why?

Look at the code in file `os161/src/kern/arch/mips/include/trapframe.h`

```
struct trapframe {
	uint32_t tf_vaddr;	/* coprocessor 0 vaddr register */
	uint32_t tf_status;	/* coprocessor 0 status register */
	uint32_t tf_cause;	/* coprocessor 0 cause register */
	uint32_t tf_lo;
	uint32_t tf_hi;
	uint32_t tf_ra;		/* Saved register 31 */
	uint32_t tf_at;		/* Saved register 1 (AT) */
	uint32_t tf_v0;		/* Saved register 2 (v0) */
	uint32_t tf_v1;		/* etc. */
	uint32_t tf_a0;
	uint32_t tf_a1;
	uint32_t tf_a2;
	uint32_t tf_a3;
	uint32_t tf_t0;
	uint32_t tf_t1;
	uint32_t tf_t2;
	uint32_t tf_t3;
	uint32_t tf_t4;
	uint32_t tf_t5;
	uint32_t tf_t6;
	uint32_t tf_t7;
	uint32_t tf_s0;
	uint32_t tf_s1;
	uint32_t tf_s2;
	uint32_t tf_s3;
	uint32_t tf_s4;
	uint32_t tf_s5;
	uint32_t tf_s6;
	uint32_t tf_s7;
	uint32_t tf_t8;
	uint32_t tf_t9;
	uint32_t tf_k0;		/* dummy (see exception-mips1.S comments) */
	uint32_t tf_k1;		/* dummy */
	uint32_t tf_gp;
	uint32_t tf_sp;
	uint32_t tf_s8;
	uint32_t tf_epc;	/* coprocessor 0 epc register */
};
```

A `trapframe` is 37 words large because the `trapframe` structure save 37 registers during entry to the exception handler and each register data is a word long.



> - `kern/arch/sys161/conf/conf.arch` -- Similar to `mips/conf/conf.arch`.
> - `kern/arch/sys161/include` -- These files are include files for the System161-specific hardware, constants, and functions. machine-specific constants and functions.
> - kern/compile -- This is where you build kernels. See below.



> **Question 7:** Under what circumstances should you re-run the kern/conf/config script?

I should rerun this step if I change the kernel config, add new source files to the build, or add new build options.



> **Question 8:** Under what circumstances should you run `bmake depend` in kern/compile/DUMBVM?

I should rerun `bmake depend` if I change header file inclusions, or after re-running `config`. 



> **Question 9:** Under what circumstances should you run `bmake` or `bmake install` in kern/compile/DUMBVM?

I should run `bmake` if I want to recompile the kernel.

I should also run `bmake install` when I want to copy the newly compiled kernel to `~/os161/root` where I can boot it in System/161



> - kern/dev -- This is where all the low level device management code is stored. Unless you are really interested, you can safely ignore most of this directory.
> - kern/fs -- This is where the actual file system implementations go. The subdirectory sfs contains a simple default file system. You will augment this file system as part of Assignment 4, so we'll ask you more questions about it then. The subdirectory semfs contains a special-purpose file system that provides semaphores to user-level programs. We may ask you more questions about this later on, after we discuss in class what semaphores are.
> - kern/include -- These are the include files that the kernel needs. The kern subdirectory contains include files that are visible not only to the operating system itself, but also to user-level programs. (Think about why it's named "kern" and where the files end up when installed.)
> - kern/lib -- These are library routines used throughout the kernel, e.g., arrays, kernel printf, etc. Note: You can use these data structures as you implement your assignments in CS161. We strongly encourage you to look around and see what we've provided for you.
> - kern/main -- This is where the kernel is initialized and where the kernel main function is implemented.



>**Question 10:** When you booted your kernel, you found that there were several commands that you could issue to experiment with it. Explain exactly where and what you would have to do to add a command that printed out, "Hello world!"

First, create a file named "hello.txt" under the path `~/os161/root`

```
cd ~/os161/root
vim hello.txt
```

Type "Hello world!" in the file hello.txt

Then run:

```
sys161 kernel pf hello.txt
```

output:

```
OS/161 kernel: pf hello.txt
Hello World!
Operation took 0.047573395 seconds
```

<img src="https://gitee.com/OooAlex/study_note/raw/master/img/202409151546221.png" alt="image-20240915154612936" style="zoom: 80%;" />



> - kern/proc -- This is where process support lives. You will write most of the code that goes here during Assignments 4 and 5.
> - kern/synchprobs -- This is the directory that contains/will contain the framework code that you will need to complete assignment 1. You can safely ignore it for now.
> - kern/syscall -- This is where you will add code to create and manage user level processes. As it stands now, OS/161 runs only kernel threads; there is no support for user level code. In Assignments 4 and 5, you'll implement this support.
> - kern/thread -- Threads are the fundamental abstraction on which the kernel is built (do not forget to look back at header files!)
> - kern/vfs -- The vfs is the "virtual file system." It is an abstraction for a file system and its existence in the kernel allows you to implement multiple file systems, while sharing as much code as possible. The VFS layer is the file-system independent layer. You will want to go look atvfs.h and vnode.h before looking at this directory.
> - kern/vm -- This directory is fairly vacant. In Assignments 6 and 7, you'll implement virtual memory and most of your code will go in here.
> - man/ -- the OS/161 manual ("man pages") appear here. The man pages document (or specify) every program, every function in the C library, and every system call. You will use the system call man pages for reference in the course of assignment 2. The man pages are HTML and can be read with any browser.
> - mk/ -- fragments of makefiles used to build the system.
> - userland/ -- user-level libraries and program code
> - userland/bin/ -- all the utilities that are typically found in /bin, e.g., cat, cp, ls, etc. The things in bin are considered "fundamental" utilities that the system needs to run.



> **Question 11:** Why do we need to include these in your OS/161 distribution? Why can't you just use the standard utilities that are present on the machine on which you're working?

os161 runs in its own virtualized environment (runs on hardware simulator) with a different kernel and system call interface compared to the host machine. Standard utilities from the host operating system rely on system calls and kernel features that are not implemented in OS/161. The userland utilities in OS/161 are specifically built to interact with the OS/161 kernel and its system calls.

Also, OS/161 programs are compiled for the MIPS architecture, which may be different from the architecture of the host machine(x86/ARM). In that case, the standard utilities on the host machine cannot run in the OS/161 environment. OS/161 needs a set of utilities that are compiled to run specifically within the MIPS architecture.



> - userland/include/ -- these are the include files that you would typically find in `/usr/include` (in our case, a subset of them). These are user level include files; not kernel include files.
> - userland/lib/ -- library code lives here. We have only two libraries: `libc`, the C standard library, and `hostcompat`, which is for recompiling OS/161 programs for the host UNIX system. There is also a crt0 directory, which contains the startup code for user programs.



> **Question 12:** When a user program exits, what is done with the program's return value?

When a user program exits, the kernel captures the return value and stores it in a data structure `struct proc` which contains information about the process. Later, the return value can be retrieved by the parent process using a system call (e.g. `waitpid()`). After the value is collected, the process is fully cleaned up.



> - `userland/sbin/` -- this is the source code for the utilities typically found in `/sbin` on a typical UNIX installation. In our case, there are some utilities that let you halt the machine, power it off and reboot it, among other things.
> - `userland/testbin/` -- this is the source code for the test programs found in `/testbin` in the installed OS/161 tree. You will want to examine this directory closely and be aware of what's available here, as many of these test programs will be useful during the course of the semester.



> **Question 13:** Imagine that you wanted to add a new system call. List all the places that you would need to modify/add code. Then review your answers to questions 7-9 and note which of those actions you need to take in order to test the new system call. 

* Add code

1. Define the system call number

In file`~/os161/src/kern/include/kern/syscall.h`, define the number of the newly-defined system call.

2. Declare the system call

In file `~/os161/src/kern/include/syscall.h`, declare the new system call.

3. implement the system call

In the path `~/os161/src/kern/syscall/`, create a source file and implement the system call. 

4. Update the system call dispatcher

In file `~/os161/src/kern/arch/mips/syscall/syscall.c`, add the new system call to the `switch` statement that dispatches the appropriate system call based on its number.

* Actions to take

In order to test the new system call, I need to run:

```
cd ~/os161/src/kern/compile/DUMBVM
bmake depend
bmake
bmake install
```



## Step 6. Learn how to use the debugger

> Reference: 
>
> [attaching to OS161 with GDB](https://people.ece.ubc.ca/~os161/os161-site/gdb-attach.html)
>
> [GDB tutorial](https://people.ece.ubc.ca/~os161/os161-site/debug-os161.html)



## Step 7. Trace the execution from start to menu()



> **Question 14:** What is the name of the very first function that executes when OS161 starts up? 

```
(gdb) db
__start () at ../../arch/sys161/main/start.S:54
54         addiu sp, sp, -24
```

`–start()` is the very first function that executes when OS161 starts.



> **Question 15:** What is the very first assembly instruction that executes? 

```
(gdb) db
__start () at ../../arch/sys161/main/start.S:54
54         addiu sp, sp, -24
```

`addiu sp, sp, -24` is the very first assembly instruction that executes.



> **Question 16:** Set the breakpoints in the kernel function that shows the menu and in the kernel main function. Now tell GDB to display all the breakpoints that were set and copy the output to your submit file. 

```
(gdb) info b
Num     Type           Disp Enb Address    What
1       breakpoint     keep y   0x800139e4 in kmain at ../../main/main.c:211
2       breakpoint     keep y   0x80014a0c in menu at ../../main/menu.c:697
```



>**Question 17:** Briefly describe what happens between the beginning of the execution and the invocation of the kernel main function. 

First, set up the stack frame for debugging purposes, saving the return address register (`ra`) to help GDB with disassembly.

Then, copy the boot argument string from the stack (in register `a0`) to the kernel's `_end` address.

Then, determine the memory layout and initialize the kernel stack.

```
We set up the memory map like this:
    *
    *         top of memory
    *                         free memory
    *         P + 0x1000
    *                         first thread's stack (1 page)
    *         P
    *                         wasted space (< 1 page)
    *                         copy of the boot string
    *         _end
    *                         kernel
    *         0x80000200
    *                         exception handlers
    *         0x80000000
```

Then, copy the exception handler code onto the first page of memory.

Then, flush the instruction cache to ensure recent changes (such as exception handler code) are correctly read by the instruction cache.

Then, initilize the TLB, set up the status register and the context register, and load the GP register.

Finally, call the kernel main function.



> **Question 18:** What is the assembly language instruction that calls the kernel main function? 

```
jal kmain
```



>**Question 19:** Step through the `boot()` code to find out what functions are called during early initialization. Paste the gdb output that shows you what these functions are.

```
(gdb) n
109             ram_bootstrap();
(gdb) n
110             proc_bootstrap();
(gdb) n
111             thread_bootstrap();
(gdb) n
112             hardclock_bootstrap();
(gdb) n
113             vfs_bootstrap();
(gdb) n
114             kheap_nextgeneration();
```



> **Question 20:** Set a breakpoint in thread_bootstrap(). Once you hit that breakpoint, at the very first line of that function, attempt to print the contents of the *bootcpu variable. Copy the output into the submit file. 

```
(gdb) p *bootcpu
Cannot access memory at address 0x80000
```



>**Question 21:** Now, step through that function until after the line that says 'bootcpu = cpu_create(0)'. Now print the content of *bootcpu and paste the output. 

```
(gdb) p *bootcpu
$2 = {c_self = 0x8003af00, c_number = 0, c_hardware_number = 0, c_curthread = 0x8003bf80, c_zombies = {tl_head = {
      tln_prev = 0x0, tln_next = 0x8003af1c, tln_self = 0x0}, tl_tail = {tln_prev = 0x8003af10, tln_next = 0x0, 
      tln_self = 0x0}, tl_count = 0}, c_hardclocks = 0, c_spinlocks = 0, c_isidle = false, c_runqueue = {tl_head = {
      tln_prev = 0x0, tln_next = 0x8003af44, tln_self = 0x0}, tl_tail = {tln_prev = 0x8003af38, tln_next = 0x0, 
      tln_self = 0x0}, tl_count = 0}, c_runqueue_lock = {splk_lock = 0, splk_holder = 0x0}, c_ipi_pending = 0, 
  c_shootdown = {{ts_placeholder = 0} <repeats 16 times>}, c_numshootdown = 0, c_ipi_lock = {splk_lock = 0, 
    splk_holder = 0x0}}
```



> Copy the contents of `kern/gdbscripts/array` into your .gdbinit file.



> **Question 22:** Print the allcpus array before the boot() function is executed. Paste the output. 

```
(gdb) array allcpus
0 items
```



> **Question 23:** Print again the same array after the boot() function is executed. Paste the output. 

```
(gdb) array allcpus
1 items
$1 = (void *) 0x8003af00
```

