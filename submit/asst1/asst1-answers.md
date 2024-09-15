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

