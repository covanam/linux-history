/*
 * linux/kernel/suspend.c
 *
 * This file is to realize architecture-independent
 * machine suspend feature using pretty near only high-level routines
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001,2002 Pavel Machek <pavel@suse.cz>
 *
 * I'd like to thank the following people for their work:
 * 
 * Pavel Machek <pavel@ucw.cz>:
 * Modifications, defectiveness pointing, being with me at the very beginning,
 * suspend to swap space, stop all tasks. Port to 2.4.18-ac and 2.5.17.
 *
 * Steve Doddi <dirk@loth.demon.co.uk>: 
 * Support the possibility of hardware state restoring.
 *
 * Raph <grey.havens@earthling.net>:
 * Support for preserving states of network devices and virtual console
 * (including X and svgatextmode)
 *
 * Kurt Garloff <garloff@suse.de>:
 * Straightened the critical function in order to prevent compilers from
 * playing tricks with local variables.
 *
 * Andreas Mohr <a.mohr@mailto.de>
 *
 * Alex Badea <vampire@go.ro>:
 * Fixed runaway init
 *
 * More state savers are welcome. Especially for the scsi layer...
 *
 * For TODOs,FIXMEs also look in Documentation/swsusp.txt
 */

#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/device.h>
#include <linux/buffer_head.h>
#include <linux/swapops.h>
#include <linux/bootmem.h>

#include <asm/mmu_context.h>

#include "power.h"


extern int swsusp_arch_suspend(int resume);

#define __ADDRESS(x)  ((unsigned long) phys_to_virt(x))
#define ADDRESS(x) __ADDRESS((x) << PAGE_SHIFT)
#define ADDRESS2(x) __ADDRESS(__pa(x))		/* Needed for x86-64 where some pages are in memory twice */

/* References to section boundaries */
extern char __nosave_begin, __nosave_end;

extern int is_head_of_free_region(struct page *);

/* Variables to be preserved over suspend */
static int pagedir_order_check;
static int nr_copy_pages_check;

static char resume_file[256];			/* For resume= kernel option */
static dev_t resume_device;
/* Local variables that should not be affected by save */
unsigned int nr_copy_pages __nosavedata = 0;

/* Suspend pagedir is allocated before final copy, therefore it
   must be freed after resume 

   Warning: this is evil. There are actually two pagedirs at time of
   resume. One is "pagedir_save", which is empty frame allocated at
   time of suspend, that must be freed. Second is "pagedir_nosave", 
   allocated at time of resume, that travels through memory not to
   collide with anything.
 */
suspend_pagedir_t *pagedir_nosave __nosavedata = NULL;
static suspend_pagedir_t *pagedir_save;
static int pagedir_order __nosavedata = 0;

struct link {
	char dummy[PAGE_SIZE - sizeof(swp_entry_t)];
	swp_entry_t next;
};

union diskpage {
	union swap_header swh;
	struct link link;
	struct suspend_header sh;
};

/*
 * XXX: We try to keep some more pages free so that I/O operations succeed
 * without paging. Might this be more?
 */
#define PAGES_FOR_IO	512

static const char name_suspend[] = "Suspend Machine: ";
static const char name_resume[] = "Resume Machine: ";

/*
 * Debug
 */
#define	DEBUG_DEFAULT
#undef	DEBUG_PROCESS
#undef	DEBUG_SLOW
#define TEST_SWSUSP 0		/* Set to 1 to reboot instead of halt machine after suspension */

#ifdef DEBUG_DEFAULT
# define PRINTK(f, a...)       printk(f, ## a)
#else
# define PRINTK(f, a...)
#endif

#ifdef DEBUG_SLOW
#define MDELAY(a) mdelay(a)
#else
#define MDELAY(a)
#endif

/*
 * Saving part...
 */

static __inline__ int fill_suspend_header(struct suspend_header *sh)
{
	memset((char *)sh, 0, sizeof(*sh));

	sh->version_code = LINUX_VERSION_CODE;
	sh->num_physpages = num_physpages;
	strncpy(sh->machine, system_utsname.machine, 8);
	strncpy(sh->version, system_utsname.version, 20);
	/* FIXME: Is this bogus? --RR */
	sh->num_cpus = num_online_cpus();
	sh->page_size = PAGE_SIZE;
	sh->suspend_pagedir = pagedir_nosave;
	BUG_ON (pagedir_save != pagedir_nosave);
	sh->num_pbes = nr_copy_pages;
	/* TODO: needed? mounted fs' last mounted date comparison
	 * [so they haven't been mounted since last suspend.
	 * Maybe it isn't.] [we'd need to do this for _all_ fs-es]
	 */
	return 0;
}

/* We memorize in swapfile_used what swap devices are used for suspension */
#define SWAPFILE_UNUSED    0
#define SWAPFILE_SUSPEND   1	/* This is the suspending device */
#define SWAPFILE_IGNORED   2	/* Those are other swap devices ignored for suspension */

static unsigned short swapfile_used[MAX_SWAPFILES];
static unsigned short root_swap;
#define MARK_SWAP_SUSPEND 0
#define MARK_SWAP_RESUME 2

static void mark_swapfiles(swp_entry_t prev, int mode)
{
	swp_entry_t entry;
	union diskpage *cur;
	struct page *page;

	if (root_swap == 0xFFFF)  /* ignored */
		return;

	page = alloc_page(GFP_ATOMIC);
	if (!page)
		panic("Out of memory in mark_swapfiles");
	cur = page_address(page);
	/* XXX: this is dirty hack to get first page of swap file */
	entry = swp_entry(root_swap, 0);
	rw_swap_page_sync(READ, entry, page);

	if (mode == MARK_SWAP_RESUME) {
	  	if (!memcmp("S1",cur->swh.magic.magic,2))
		  	memcpy(cur->swh.magic.magic,"SWAP-SPACE",10);
		else if (!memcmp("S2",cur->swh.magic.magic,2))
			memcpy(cur->swh.magic.magic,"SWAPSPACE2",10);
		else printk("%sUnable to find suspended-data signature (%.10s - misspelled?\n", 
		      	name_resume, cur->swh.magic.magic);
	} else {
	  	if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)))
		  	memcpy(cur->swh.magic.magic,"S1SUSP....",10);
		else if ((!memcmp("SWAPSPACE2",cur->swh.magic.magic,10)))
			memcpy(cur->swh.magic.magic,"S2SUSP....",10);
		else panic("\nSwapspace is not swapspace (%.10s)\n", cur->swh.magic.magic);
		cur->link.next = prev; /* prev is the first/last swap page of the resume area */
		/* link.next lies *no more* in last 4/8 bytes of magic */
	}
	rw_swap_page_sync(WRITE, entry, page);
	__free_page(page);
}

static void read_swapfiles(void) /* This is called before saving image */
{
	int i, len;
	
	len=strlen(resume_file);
	root_swap = 0xFFFF;
	
	swap_list_lock();
	for(i=0; i<MAX_SWAPFILES; i++) {
		if (swap_info[i].flags == 0) {
			swapfile_used[i]=SWAPFILE_UNUSED;
		} else {
			if(!len) {
	    			printk(KERN_WARNING "resume= option should be used to set suspend device" );
				if(root_swap == 0xFFFF) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else
					swapfile_used[i] = SWAPFILE_IGNORED;				  
			} else {
	  			/* we ignore all swap devices that are not the resume_file */
				if (1) {
// FIXME				if(resume_device == swap_info[i].swap_device) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else {
#if 0
					printk( "Resume: device %s (%x != %x) ignored\n", swap_info[i].swap_file->d_name.name, swap_info[i].swap_device, resume_device );				  
#endif
				  	swapfile_used[i] = SWAPFILE_IGNORED;
				}
			}
		}
	}
	swap_list_unlock();
}

static void lock_swapdevices(void) /* This is called after saving image so modification
				      will be lost after resume... and that's what we want. */
{
	int i;

	swap_list_lock();
	for(i = 0; i< MAX_SWAPFILES; i++)
		if(swapfile_used[i] == SWAPFILE_IGNORED) {
			swap_info[i].flags ^= 0xFF; /* we make the device unusable. A new call to
						       lock_swapdevices can unlock the devices. */
		}
	swap_list_unlock();
}

static int write_suspend_image(void)
{
	int i;
	swp_entry_t entry, prev = { 0 };
	int nr_pgdir_pages = SUSPEND_PD_PAGES(nr_copy_pages);
	union diskpage *cur,  *buffer = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	unsigned long address;
	struct page *page;

	printk( "Writing data to swap (%d pages): ", nr_copy_pages );
	for (i=0; i<nr_copy_pages; i++) {
		if (!(i%100))
			printk( "." );
		if (!(entry = get_swap_page()).val)
			panic("\nNot enough swapspace when writing data" );
		
		if (swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
			panic("\nPage %d: not enough swapspace on suspend device", i );
	    
		address = (pagedir_nosave+i)->address;
		page = virt_to_page(address);
		rw_swap_page_sync(WRITE, entry, page);
		(pagedir_nosave+i)->swap_address = entry;
	}
	printk( "|\n" );
	printk( "Writing pagedir (%d pages): ", nr_pgdir_pages);
	for (i=0; i<nr_pgdir_pages; i++) {
		cur = (union diskpage *)((char *) pagedir_nosave)+i;
		BUG_ON ((char *) cur != (((char *) pagedir_nosave) + i*PAGE_SIZE));
		printk( "." );
		if (!(entry = get_swap_page()).val) {
			printk(KERN_CRIT "Not enough swapspace when writing pgdir\n" );
			panic("Don't know how to recover");
			free_page((unsigned long) buffer);
			return -ENOSPC;
		}

		if(swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
			panic("\nNot enough swapspace for pagedir on suspend device" );

		BUG_ON (sizeof(swp_entry_t) != sizeof(long));
		BUG_ON (PAGE_SIZE % sizeof(struct pbe));

		cur->link.next = prev;				
		page = virt_to_page((unsigned long)cur);
		rw_swap_page_sync(WRITE, entry, page);
		prev = entry;
	}
	printk("H");
	BUG_ON (sizeof(struct suspend_header) > PAGE_SIZE-sizeof(swp_entry_t));
	BUG_ON (sizeof(union diskpage) != PAGE_SIZE);
	if (!(entry = get_swap_page()).val)
		panic( "\nNot enough swapspace when writing header" );
	if (swapfile_used[swp_type(entry)] != SWAPFILE_SUSPEND)
		panic("\nNot enough swapspace for header on suspend device" );

	cur = (void *) buffer;
	if (fill_suspend_header(&cur->sh))
		panic("\nOut of memory while writing header");
		
	cur->link.next = prev;

	page = virt_to_page((unsigned long)cur);
	rw_swap_page_sync(WRITE, entry, page);
	prev = entry;

	printk( "S" );
	mark_swapfiles(prev, MARK_SWAP_SUSPEND);
	printk( "|\n" );

	MDELAY(1000);
	free_page((unsigned long) buffer);
	return 0;
}

/* if pagedir_p != NULL it also copies the counted pages */
static int count_and_copy_data_pages(struct pbe *pagedir_p)
{
	int chunk_size;
	int nr_copy_pages = 0;
	int pfn;
	struct page *page;
	
	BUG_ON (max_pfn != num_physpages);

	for (pfn = 0; pfn < max_pfn; pfn++) {
		page = pfn_to_page(pfn);

		if (!PageReserved(page)) {
			if (PageNosave(page))
				continue;

			if ((chunk_size=is_head_of_free_region(page))!=0) {
				pfn += chunk_size - 1;
				continue;
			}
		} else if (PageReserved(page)) {
			BUG_ON (PageNosave(page));

			/*
			 * Just copy whole code segment. Hopefully it is not that big.
			 */
			if ((ADDRESS(pfn) >= (unsigned long) ADDRESS2(&__nosave_begin)) && 
			    (ADDRESS(pfn) <  (unsigned long) ADDRESS2(&__nosave_end))) {
				PRINTK("[nosave %lx]", ADDRESS(pfn));
				continue;
			}
			/* Hmm, perhaps copying all reserved pages is not too healthy as they may contain 
			   critical bios data? */
		} else	BUG();

		nr_copy_pages++;
		if (pagedir_p) {
			pagedir_p->orig_address = ADDRESS(pfn);
			copy_page((void *) pagedir_p->address, (void *) pagedir_p->orig_address);
			pagedir_p++;
		}
	}
	return nr_copy_pages;
}

static void free_suspend_pagedir(unsigned long this_pagedir)
{
	struct page *page;
	int pfn;
	unsigned long this_pagedir_end = this_pagedir +
		(PAGE_SIZE << pagedir_order);

	for(pfn = 0; pfn < num_physpages; pfn++) {
		page = pfn_to_page(pfn);
		if (!TestClearPageNosave(page))
			continue;

		if (ADDRESS(pfn) >= this_pagedir && ADDRESS(pfn) < this_pagedir_end)
			continue; /* old pagedir gets freed in one */
		
		free_page(ADDRESS(pfn));
	}
	free_pages(this_pagedir, pagedir_order);
}

static suspend_pagedir_t *create_suspend_pagedir(int nr_copy_pages)
{
	int i;
	suspend_pagedir_t *pagedir;
	struct pbe *p;
	struct page *page;

	pagedir_order = get_bitmask_order(SUSPEND_PD_PAGES(nr_copy_pages));

	p = pagedir = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC | __GFP_COLD, pagedir_order);
	if(!pagedir)
		return NULL;

	page = virt_to_page(pagedir);
	for(i=0; i < 1<<pagedir_order; i++)
		SetPageNosave(page++);
		
	while(nr_copy_pages--) {
		p->address = get_zeroed_page(GFP_ATOMIC | __GFP_COLD);
		if(!p->address) {
			free_suspend_pagedir((unsigned long) pagedir);
			return NULL;
		}
		SetPageNosave(virt_to_page(p->address));
		p->orig_address = 0;
		p++;
	}
	return pagedir;
}


static int suspend_prepare_image(void)
{
	struct sysinfo i;
	unsigned int nr_needed_pages = 0;

	drain_local_pages();

	pagedir_nosave = NULL;
	printk( "/critical section: Counting pages to copy" );
	nr_copy_pages = count_and_copy_data_pages(NULL);
	nr_needed_pages = nr_copy_pages + PAGES_FOR_IO;
	
	printk(" (pages needed: %d+%d=%d free: %d)\n",nr_copy_pages,PAGES_FOR_IO,nr_needed_pages,nr_free_pages());
	if(nr_free_pages() < nr_needed_pages) {
		printk(KERN_CRIT "%sCouldn't get enough free pages, on %d pages short\n",
		       name_suspend, nr_needed_pages-nr_free_pages());
		root_swap = 0xFFFF;
		return 1;
	}
	si_swapinfo(&i);	/* FIXME: si_swapinfo(&i) returns all swap devices information.
				   We should only consider resume_device. */
	if (i.freeswap < nr_needed_pages)  {
		printk(KERN_CRIT "%sThere's not enough swap space available, on %ld pages short\n",
		       name_suspend, nr_needed_pages-i.freeswap);
		return 1;
	}

	PRINTK( "Alloc pagedir\n" ); 
	pagedir_save = pagedir_nosave = create_suspend_pagedir(nr_copy_pages);
	if(!pagedir_nosave) {
		/* Shouldn't happen */
		printk(KERN_CRIT "%sCouldn't allocate enough pages\n",name_suspend);
		panic("Really should not happen");
		return 1;
	}
	nr_copy_pages_check = nr_copy_pages;
	pagedir_order_check = pagedir_order;

	drain_local_pages();	/* During allocating of suspend pagedir, new cold pages may appear. Kill them */
	if (nr_copy_pages != count_and_copy_data_pages(pagedir_nosave))	/* copy */
		BUG();

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	printk( "critical section/: done (%d pages copied)\n", nr_copy_pages );
	return 0;
}


/**
 *	suspend_save_image - Prepare and write saved image to swap.
 *
 *	IRQs are re-enabled here so we can resume devices and safely write 
 *	to the swap devices. We disable them again before we leave.
 *
 *	The second lock_swapdevices() will unlock ignored swap devices since
 *	writing is finished.
 *	It is important _NOT_ to umount filesystems at this point. We want
 *	them synced (in case something goes wrong) but we DO not want to mark
 *	filesystem clean: it is not. (And it does not matter, if we resume
 *	correctly, we'll mark system clean, anyway.)
 */

static int suspend_save_image(void)
{
	int error;
	local_irq_enable();
	device_resume();
	lock_swapdevices();
	error = write_suspend_image();
	lock_swapdevices();
	local_irq_disable();
	return error;
}

/*
 * Magic happens here
 */

int swsusp_resume(void)
{
	BUG_ON (nr_copy_pages_check != nr_copy_pages);
	BUG_ON (pagedir_order_check != pagedir_order);
	
	/* Even mappings of "global" things (vmalloc) need to be fixed */
	__flush_tlb_global();
	return 0;
}

/* swsusp_arch_suspend() is implemented in arch/?/power/swsusp.S, 
   and basically does:

	if (!resume) {
		save_processor_state();
		SAVE_REGISTERS
		swsusp_suspend();
		return;
	}
	GO_TO_SWAPPER_PAGE_TABLES
	COPY_PAGES_BACK
	RESTORE_REGISTERS
	restore_processor_state();
	swsusp_resume();

 */


int swsusp_suspend(void)
{
	int error;
	read_swapfiles();
	error = suspend_prepare_image();
	if (!error)
		error = suspend_save_image();
	if (error) {
		printk(KERN_EMERG "%sSuspend failed, trying to recover...\n", 
		       name_suspend);
		barrier();
		mb();
		mdelay(1000);
	}
	return error;
}

/* More restore stuff */

/* FIXME: Why not memcpy(to, from, 1<<pagedir_order*PAGE_SIZE)? */
static void __init copy_pagedir(suspend_pagedir_t *to, suspend_pagedir_t *from)
{
	int i;
	char *topointer=(char *)to, *frompointer=(char *)from;

	for(i=0; i < 1 << pagedir_order; i++) {
		copy_page(topointer, frompointer);
		topointer += PAGE_SIZE;
		frompointer += PAGE_SIZE;
	}
}

#define does_collide(addr) does_collide_order(pagedir_nosave, addr, 0)

/*
 * Returns true if given address/order collides with any orig_address 
 */
static int __init does_collide_order(suspend_pagedir_t *pagedir, 
				     unsigned long addr, int order)
{
	int i;
	unsigned long addre = addr + (PAGE_SIZE<<order);
	
	for(i=0; i < nr_copy_pages; i++)
		if((pagedir+i)->orig_address >= addr &&
			(pagedir+i)->orig_address < addre)
			return 1;

	return 0;
}

/*
 * We check here that pagedir & pages it points to won't collide with pages
 * where we're going to restore from the loaded pages later
 */
static int __init check_pagedir(void)
{
	int i;

	for(i=0; i < nr_copy_pages; i++) {
		unsigned long addr;

		do {
			addr = get_zeroed_page(GFP_ATOMIC);
			if(!addr)
				return -ENOMEM;
		} while (does_collide(addr));

		(pagedir_nosave+i)->address = addr;
	}
	return 0;
}

static int __init relocate_pagedir(void)
{
	/*
	 * We have to avoid recursion (not to overflow kernel stack),
	 * and that's why code looks pretty cryptic 
	 */
	suspend_pagedir_t *new_pagedir, *old_pagedir = pagedir_nosave;
	void **eaten_memory = NULL;
	void **c = eaten_memory, *m, *f;

	printk("Relocating pagedir");

	if(!does_collide_order(old_pagedir, (unsigned long)old_pagedir, pagedir_order)) {
		printk("not necessary\n");
		return 0;
	}

	while ((m = (void *) __get_free_pages(GFP_ATOMIC, pagedir_order))) {
		memset(m, 0, PAGE_SIZE);
		if (!does_collide_order(old_pagedir, (unsigned long)m, pagedir_order))
			break;
		eaten_memory = m;
		printk( "." ); 
		*eaten_memory = c;
		c = eaten_memory;
	}

	if (!m)
		return -ENOMEM;

	pagedir_nosave = new_pagedir = m;
	copy_pagedir(new_pagedir, old_pagedir);

	c = eaten_memory;
	while(c) {
		printk(":");
		f = *c;
		c = *c;
		if (f)
			free_pages((unsigned long)f, pagedir_order);
	}
	printk("|\n");
	return 0;
}

/*
 * Sanity check if this image makes sense with this kernel/swap context
 * I really don't think that it's foolproof but more than nothing..
 */

static int __init sanity_check_failed(char *reason)
{
	printk(KERN_ERR "%s%s\n",name_resume,reason);
	return -EPERM;
}

static int __init sanity_check(struct suspend_header *sh)
{
	if(sh->version_code != LINUX_VERSION_CODE)
		return sanity_check_failed("Incorrect kernel version");
	if(sh->num_physpages != num_physpages)
		return sanity_check_failed("Incorrect memory size");
	if(strncmp(sh->machine, system_utsname.machine, 8))
		return sanity_check_failed("Incorrect machine type");
	if(strncmp(sh->version, system_utsname.version, 20))
		return sanity_check_failed("Incorrect version");
	if(sh->num_cpus != num_online_cpus())
		return sanity_check_failed("Incorrect number of cpus");
	if(sh->page_size != PAGE_SIZE)
		return sanity_check_failed("Incorrect PAGE_SIZE");
	return 0;
}

static int __init bdev_read_page(struct block_device *bdev, 
				 long pos, void *buf)
{
	struct buffer_head *bh;
	BUG_ON (pos%PAGE_SIZE);
	bh = __bread(bdev, pos/PAGE_SIZE, PAGE_SIZE);
	if (!bh || (!bh->b_data)) {
		return -1;
	}
	memcpy(buf, bh->b_data, PAGE_SIZE);	/* FIXME: may need kmap() */
	BUG_ON(!buffer_uptodate(bh));
	brelse(bh);
	return 0;
} 

extern dev_t __init name_to_dev_t(const char *line);

static int __init read_suspend_image(struct block_device *bdev, 
				     union diskpage *cur)
{
	swp_entry_t next;
	int i, nr_pgdir_pages;

#define PREPARENEXT \
	{	next = cur->link.next; \
		next.val = swp_offset(next) * PAGE_SIZE; \
        }

	if (bdev_read_page(bdev, 0, cur)) return -EIO;

	if ((!memcmp("SWAP-SPACE",cur->swh.magic.magic,10)) ||
	    (!memcmp("SWAPSPACE2",cur->swh.magic.magic,10))) {
		printk(KERN_ERR "%sThis is normal swap space\n", name_resume );
		return -EINVAL;
	}

	PREPARENEXT; /* We have to read next position before we overwrite it */

	if (!memcmp("S1",cur->swh.magic.magic,2))
		memcpy(cur->swh.magic.magic,"SWAP-SPACE",10);
	else if (!memcmp("S2",cur->swh.magic.magic,2))
		memcpy(cur->swh.magic.magic,"SWAPSPACE2",10);
	else {
		printk("swsusp: %s: Unable to find suspended-data signature (%.10s - misspelled?\n", 
			name_resume, cur->swh.magic.magic);
		return -EFAULT;
	}

	printk( "%sSignature found, resuming\n", name_resume );
	MDELAY(1000);

	if (bdev_read_page(bdev, next.val, cur)) return -EIO;
	if (sanity_check(&cur->sh)) 	/* Is this same machine? */	
		return -EPERM;
	PREPARENEXT;

	pagedir_save = cur->sh.suspend_pagedir;
	nr_copy_pages = cur->sh.num_pbes;
	nr_pgdir_pages = SUSPEND_PD_PAGES(nr_copy_pages);
	pagedir_order = get_bitmask_order(nr_pgdir_pages);

	pagedir_nosave = (suspend_pagedir_t *)__get_free_pages(GFP_ATOMIC, pagedir_order);
	if (!pagedir_nosave)
		return -ENOMEM;

	PRINTK( "%sReading pagedir, ", name_resume );

	/* We get pages in reverse order of saving! */
	for (i=nr_pgdir_pages-1; i>=0; i--) {
		BUG_ON (!next.val);
		cur = (union diskpage *)((char *) pagedir_nosave)+i;
		if (bdev_read_page(bdev, next.val, cur)) return -EIO;
		PREPARENEXT;
	}
	BUG_ON (next.val);

	if (relocate_pagedir())
		return -ENOMEM;
	if (check_pagedir())
		return -ENOMEM;

	printk( "Reading image data (%d pages): ", nr_copy_pages );
	for(i=0; i < nr_copy_pages; i++) {
		swp_entry_t swap_address = (pagedir_nosave+i)->swap_address;
		if (!(i%100))
			printk( "." );
		/* You do not need to check for overlaps...
		   ... check_pagedir already did this work */
		if (bdev_read_page(bdev, swp_offset(swap_address) * PAGE_SIZE, (char *)((pagedir_nosave+i)->address)))
			return -EIO;
	}
	printk( "|\n" );
	return 0;
}

/**
 *	swsusp_save - Snapshot memory
 */

int swsusp_save(void) 
{
#if defined (CONFIG_HIGHMEM) || defined (COFNIG_DISCONTIGMEM)
	printk("swsusp is not supported with high- or discontig-mem.\n");
	return -EPERM;
#endif
	return arch_prepare_suspend();
}


/**
 *	swsusp_write - Write saved memory image to swap.
 *
 *	swsusp_arch_suspend(0) returns after system is resumed.
 *
 *	swsusp_arch_suspend() copies all "used" memory to "free" memory, 
 *	then unsuspends all device drivers, and writes memory to disk
 *	using normal kernel mechanism.
 */

int swsusp_write(void)
{
	return swsusp_arch_suspend(0);
}


/**
 *	swsusp_read - Read saved image from swap.
 */

int __init swsusp_read(void)
{
	union diskpage *cur;
	int error;
	char b[BDEVNAME_SIZE];

	if (!strlen(resume_file))
		return -ENOENT;

	resume_device = name_to_dev_t(resume_file);
	printk("swsusp: Resume From Partition: %s, Device: %s\n", 
	       resume_file, __bdevname(resume_device, b));

	cur = (union diskpage *)get_zeroed_page(GFP_ATOMIC);
	if (cur) {
		struct block_device *bdev;
		bdev = open_by_devnum(resume_device, FMODE_READ, BDEV_RAW);
		if (!IS_ERR(bdev)) {
			set_blocksize(bdev, PAGE_SIZE);
			error = read_suspend_image(bdev, cur);
			blkdev_put(bdev, BDEV_RAW);
		} else
			error = PTR_ERR(bdev);
		free_page((unsigned long)cur);
	} else
		error = -ENOMEM;

	if (!error)
		PRINTK("Reading resume file was successful\n");
	else
		printk( "%sError %d resuming\n", name_resume, error );
	MDELAY(1000);
	return error;
}


/**
 *	swsusp_restore - Replace running kernel with saved image.
 */

int __init swsusp_restore(void)
{
	return swsusp_arch_suspend(1);
}


/**
 *	swsusp_free - Free memory allocated to hold snapshot.
 */

int swsusp_free(void)
{
	PRINTK( "Freeing prev allocated pagedir\n" );
	free_suspend_pagedir((unsigned long) pagedir_save);

	PRINTK( "Fixing swap signatures... " );
	mark_swapfiles(((swp_entry_t) {0}), MARK_SWAP_RESUME);
	PRINTK( "ok\n" );
	return 0;
}

static int __init resume_setup(char *str)
{
	if (strlen(str))
		strncpy(resume_file, str, 255);
	return 1;
}

static int __init noresume_setup(char *str)
{
	resume_file[0] = '\0';
	return 1;
}

__setup("noresume", noresume_setup);
__setup("resume=", resume_setup);

