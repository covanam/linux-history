/*
 * drivers/power/process.c - Functions for saving/restoring console.
 *
 * Originally from swsusp.
 */

#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include "power.h"

static int new_loglevel = 7;
static int orig_loglevel;
static int orig_fgconsole, orig_kmsg;

int pm_prepare_console(void)
{
	orig_loglevel = console_loglevel;
	console_loglevel = new_loglevel;

#ifdef SUSPEND_CONSOLE
	orig_fgconsole = fg_console;

	if (vc_allocate(SUSPEND_CONSOLE))
	  /* we can't have a free VC for now. Too bad,
	   * we don't want to mess the screen for now. */
		return 1;

	set_console(SUSPEND_CONSOLE);
	if (vt_waitactive(SUSPEND_CONSOLE)) {
		pr_debug("Suspend: Can't switch VCs.");
		return 1;
	}
	orig_kmsg = kmsg_redirect;
	kmsg_redirect = SUSPEND_CONSOLE;
#endif
	return 0;
}

void pm_restore_console(void)
{
	console_loglevel = orig_loglevel;
#ifdef SUSPEND_CONSOLE
	set_console(orig_fgconsole);
#endif
	return;
}
