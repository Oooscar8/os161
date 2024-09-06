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

