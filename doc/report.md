# AOSV Final Project Report
_A.Y. 2020/2021_

Author: Alberto Bombardelli (1903114)

# Introduction
This project aims at permitting to the user to build his own
simple user mode scheduler. It does it by adding a new extenal kernel module.
This project have been developed for  linux version: **5.13.0**. 
# General structure
The module is divided in 2 parts, one in user mode and the other in kernel mode.
The kernel module is in charge of switching context, keeping the data structures
and logically managing the state of the workers and schedulers. The 

In this project scheduling is done assuming that all the workers uses the same
memory map. 

Even if the majority of the work is done by the kernel module, the user
module is not a mere **ioctl** system call callers. It is in charge of various parts, in fact the whole work is built on the cohesion of these 2 parts.

## Kernel module

### General kernel module structure
The kernel mode is composed of 3 important actors:
- ioctl device module
- scheduler module
- completion list module

Each module is not aware of the internal `struct` of the other modules (that to prevent invalid accesses to concurrent data structures).
### Ioctl device
The ioctl gets requests from the user module. It acts as a bridge between the kernel and the user parts. Using *copy_from_user* and *copy_to_user* functions it comunicates with the user space. Since this module initializes the other kernel module, it can directly comunicates with both. 
After its registration triggered by `insmod` it can be found in **/dev/umsscheddev**.

A detailed documentation of the ioctl requests can be find in the documentation.

### Completion list and completion elements
This module is in charge of the completion lists and its elements. It is composed
mainly by 2 data structures: `struct ums_complist` and `struct ums_compelem`.
`
#### Completion list
A completion list is not a mere list of completion elements, it is a logical entity that manages its completion
elements. 
It ensures that worker thread are not reserved by multiples processes and guarantee 
that the completion elements shares lays in the same thread group. This
structure is also in charge of reserving the elements to the scheduler thread.

#### Completion elements

A completion element is the job to be executed. It internally has 3 states:
`running`, `idle`, `reserved`. A job is running when a **scheduler thread** executes
it. It is **idle** when it is ready for the reservation. A job is **reserved**
when a scheduler thread dequeues it from its completion list.
A completion element cannot pass from **idle** to **running** without being reserved
before. 

### User mode scheduler and user mode scheduler worker

A scheduler is a logical entity composed of *n_cpu* scheduler threads.
Each scheduler thread shares the same memory map with the completion list's
elements that are linked with the scheduler. Each scheduler thread can either
executer a completion element (with exec) or execute its own thread (yield).
When a scheduler thread terminates, his thread is silently removed while thread
scheduler thread entry remain until the scheduler is removed/

### Context switch
The physical context switch have been done using `pt_regs` and `fpu` that contain respectively the standard user registers and the floating point aritmetic registers. 
The idea is to set and reset the scheduler thread `pt_regs` and `fpu` using the one of the completion list `task_struct`.

### Handle concurrency

To handle async requests the module uses these techniques:
- To access queues data it uses a semaphore (to reserve the **ticket**) and a spinlock to call `kfifo_in_spinlocked` and `kfifo_out_spinlocked`
- To access to structures that might be concurrently: read, written or deleted it uses `rwlock_t`. 
- To generate unique identifiers the module uses `atomic_t`
- To safely use `hashtable` concurrently kernel module uses `_rcu` functions that works using `rcu` synchronization techniques.
- To concurrently access to linked list this module uses `spinlock_t`

### /proc fs
The informations of the schedulers and completion lists are exposed by proc file system. The proc hierarchy is composed with this structure:
- **/proc/ums** <- ums scheduling folder
- **/proc/ums/completion_lists** <- top-level directory for the completion lists
- **/proc/ums/completion_lists/complist_id** <- directory of the completion list with int id = complist_id
- **/proc/ums/completion_lists/complist_id/compelem_id** <- file with stats on the completion element compelem_id
- **/proc/ums/schedulers** <- top level schedulers folder
- **/proc/ums/schedulers/sched_id** <- directory of the scheduler with identifier sched_id
- **/proc/ums/schedulers/sched_id/{0, .. n_cpu}/info** <- info file for each cpu (scheduler thread) that contains stats about the sched thread
## User module
The user module does 2 things:
- setup the user request with `ioctl` calls
- creates scheduler and worker threads (aka completion elements).

### Top level functions
The main functions exposed by the user models are:
- `EnterUmsScheduling`
- `WaitUmsScheduler`
- `WaitUmsChildren`
- `CreateUmsCompletionList`
- `CreateUmsCompletionElement`
- `ExecuteUmsThread`
- `UmsThreadYield`
- `DequeueUmsCompletionListItems`

These functions do not necessary map 1 to 1 to an **ioctl** request call, for instance `EnterUmsScheduling` creates both the scheduler and the scheduler threads. Indeed, this functions simplify to the user the file by performing the complexes ioctl calls.

### Creating new threads
It is necessary to create new threads for both **scheduler threads** and **worker threads**. To do so, the user mode module is in charge of creating the threads via `clone` syscall and, in the end dealloc all the stacks of the generated threads.

# Results
The context switch have been tested in a benchmark model. The result is the following:
it takes from 0.000021 to 0.000041 seconds to pass from a worker, to the scheduler to the worker again.
Considering that this change involves also the reservation process it seems like a 
good result.

# Conclusions
This project produced a module that implements user mode scheduling, it provides APIs that works using `ioctl` syscalls on the kernel module. It has been tested using multiple `schedulers` and completion lists together and its context switch times were calculated.

# References
I used as reference the linux kernel source code and documentation, the lecture material of this course, some stackoverflow questions and some kernel articles. 
