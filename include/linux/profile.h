#ifndef _LINUX_PROFILE_H
#define _LINUX_PROFILE_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <asm/errno.h>

#define CPU_PROFILING	1
#define SCHED_PROFILING	2

struct proc_dir_entry;
struct pt_regs;

/* parse command line */
int __init profile_setup(char * str);

/* init basic kernel profiler */
void __init profile_init(void);
void profile_tick(int, struct pt_regs *);
void profile_hit(int, void *);
#ifdef CONFIG_PROC_FS
void create_prof_cpu_mask(struct proc_dir_entry *);
#else
#define create_prof_cpu_mask(x)			do { (void)(x); } while (0)
#endif

extern unsigned int * prof_buffer;
extern unsigned long prof_len;
extern unsigned long prof_shift;
extern int prof_on;
extern cpumask_t prof_cpu_mask;


enum profile_type {
	EXIT_TASK,
	EXIT_MMAP,
	EXEC_UNMAP
};

#ifdef CONFIG_PROFILING

struct notifier_block;
struct task_struct;
struct mm_struct;

/* task is in do_exit() */
void profile_exit_task(struct task_struct * task);

/* change of vma mappings */
void profile_exec_unmap(struct mm_struct * mm);

/* exit of all vmas for a task */
void profile_exit_mmap(struct mm_struct * mm);

int profile_event_register(enum profile_type, struct notifier_block * n);

int profile_event_unregister(enum profile_type, struct notifier_block * n);

int register_profile_notifier(struct notifier_block * nb);
int unregister_profile_notifier(struct notifier_block * nb);

struct pt_regs;

/* profiling hook activated on each timer interrupt */
void profile_hook(struct pt_regs * regs);

#else

static inline int profile_event_register(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}

static inline int profile_event_unregister(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}

#define profile_exit_task(a) do { } while (0)
#define profile_exec_unmap(a) do { } while (0)
#define profile_exit_mmap(a) do { } while (0)

static inline int register_profile_notifier(struct notifier_block * nb)
{
	return -ENOSYS;
}

static inline int unregister_profile_notifier(struct notifier_block * nb)
{
	return -ENOSYS;
}

#define profile_hook(regs) do { } while (0)

#endif /* CONFIG_PROFILING */

#endif /* __KERNEL__ */

#endif /* _LINUX_PROFILE_H */
