/*
 *  linux/fs/msdos/namei.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 *  Rewritten for constant inumbers 1999 by Al Viro
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/msdos_fs.h>
#include <linux/smp_lock.h>

/* MS-DOS "device special files" */
static const unsigned char *reserved_names[] = {
	"CON     ", "PRN     ", "NUL     ", "AUX     ",
	"LPT1    ", "LPT2    ", "LPT3    ", "LPT4    ",
	"COM1    ", "COM2    ", "COM3    ", "COM4    ",
	NULL
};

/* Characters that are undesirable in an MS-DOS file name */
static unsigned char bad_chars[] = "*?<>|\"";
static unsigned char bad_if_strict_pc[] = "+=,; ";
/* GEMDOS is less restrictive */
static unsigned char bad_if_strict_atari[] = " ";

#define bad_if_strict(opts) \
	((opts)->atari ? bad_if_strict_atari : bad_if_strict_pc)

/***** Formats an MS-DOS file name. Rejects invalid names. */
static int msdos_format_name(const unsigned char *name, int len,
			     unsigned char *res, struct fat_mount_options *opts)
	/*
	 * name is the proposed name, len is its length, res is
	 * the resulting name, opts->name_check is either (r)elaxed,
	 * (n)ormal or (s)trict, opts->dotsOK allows dots at the
	 * beginning of name (for hidden files)
	 */
{
	unsigned char *walk;
	const unsigned char **reserved;
	unsigned char c;
	int space;

	if (name[0] == '.') {	/* dotfile because . and .. already done */
		if (opts->dotsOK) {
			/* Get rid of dot - test for it elsewhere */
			name++;
			len--;
		} else if (!opts->atari)
			return -EINVAL;
	}
	/*
	 * disallow names that _really_ start with a dot for MS-DOS,
	 * GEMDOS does not care
	 */
	space = !opts->atari;
	c = 0;
	for (walk = res; len && walk - res < 8; walk++) {
		c = *name++;
		len--;
		if (opts->name_check != 'r' && strchr(bad_chars, c))
			return -EINVAL;
		if (opts->name_check == 's' && strchr(bad_if_strict(opts), c))
			return -EINVAL;
		if (c >= 'A' && c <= 'Z' && opts->name_check == 's')
			return -EINVAL;
		if (c < ' ' || c == ':' || c == '\\')
			return -EINVAL;
	/*
	 * 0xE5 is legal as a first character, but we must substitute
	 * 0x05 because 0xE5 marks deleted files.  Yes, DOS really
	 * does this.
	 * It seems that Microsoft hacked DOS to support non-US
	 * characters after the 0xE5 character was already in use to
	 * mark deleted files.
	 */
		if ((res == walk) && (c == 0xE5))
			c = 0x05;
		if (c == '.')
			break;
		space = (c == ' ');
		*walk = (!opts->nocase && c >= 'a' && c <= 'z') ? c - 32 : c;
	}
	if (space)
		return -EINVAL;
	if (opts->name_check == 's' && len && c != '.') {
		c = *name++;
		len--;
		if (c != '.')
			return -EINVAL;
	}
	while (c != '.' && len--)
		c = *name++;
	if (c == '.') {
		while (walk - res < 8)
			*walk++ = ' ';
		while (len > 0 && walk - res < MSDOS_NAME) {
			c = *name++;
			len--;
			if (opts->name_check != 'r' && strchr(bad_chars, c))
				return -EINVAL;
			if (opts->name_check == 's' &&
			    strchr(bad_if_strict(opts), c))
				return -EINVAL;
			if (c < ' ' || c == ':' || c == '\\')
				return -EINVAL;
			if (c == '.') {
				if (opts->name_check == 's')
					return -EINVAL;
				break;
			}
			if (c >= 'A' && c <= 'Z' && opts->name_check == 's')
				return -EINVAL;
			space = c == ' ';
			if (!opts->nocase && c >= 'a' && c <= 'z')
				*walk++ = c - 32;
			else
				*walk++ = c;
		}
		if (space)
			return -EINVAL;
		if (opts->name_check == 's' && len)
			return -EINVAL;
	}
	while (walk - res < MSDOS_NAME)
		*walk++ = ' ';
	if (!opts->atari)
		/* GEMDOS is less stupid and has no reserved names */
		for (reserved = reserved_names; *reserved; reserved++)
			if (!strncmp(res, *reserved, 8))
				return -EINVAL;
	return 0;
}

/***** Locates a directory entry.  Uses unformatted name. */
static int msdos_find(struct inode *dir, const unsigned char *name, int len,
		      struct fat_slot_info *sinfo)
{
	struct msdos_sb_info *sbi = MSDOS_SB(dir->i_sb);
	unsigned char msdos_name[MSDOS_NAME];
	int err;

	err = msdos_format_name(name, len, msdos_name, &sbi->options);
	if (err)
		return -ENOENT;

	err = fat_scan(dir, msdos_name, sinfo);
	if (!err && sbi->options.dotsOK) {
		if (name[0] == '.') {
			if (!(sinfo->de->attr & ATTR_HIDDEN))
				err = -ENOENT;
		} else {
			if (sinfo->de->attr & ATTR_HIDDEN)
				err = -ENOENT;
		}
		if (err)
			brelse(sinfo->bh);
	}
	return err;
}

/*
 * Compute the hash for the msdos name corresponding to the dentry.
 * Note: if the name is invalid, we leave the hash code unchanged so
 * that the existing dentry can be used. The msdos fs routines will
 * return ENOENT or EINVAL as appropriate.
 */
static int msdos_hash(struct dentry *dentry, struct qstr *qstr)
{
	struct fat_mount_options *options = &MSDOS_SB(dentry->d_sb)->options;
	unsigned char msdos_name[MSDOS_NAME];
	int error;

	error = msdos_format_name(qstr->name, qstr->len, msdos_name, options);
	if (!error)
		qstr->hash = full_name_hash(msdos_name, MSDOS_NAME);
	return 0;
}

/*
 * Compare two msdos names. If either of the names are invalid,
 * we fall back to doing the standard name comparison.
 */
static int msdos_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	struct fat_mount_options *options = &MSDOS_SB(dentry->d_sb)->options;
	unsigned char a_msdos_name[MSDOS_NAME], b_msdos_name[MSDOS_NAME];
	int error;

	error = msdos_format_name(a->name, a->len, a_msdos_name, options);
	if (error)
		goto old_compare;
	error = msdos_format_name(b->name, b->len, b_msdos_name, options);
	if (error)
		goto old_compare;
	error = memcmp(a_msdos_name, b_msdos_name, MSDOS_NAME);
out:
	return error;

old_compare:
	error = 1;
	if (a->len == b->len)
		error = memcmp(a->name, b->name, a->len);
	goto out;
}

static struct dentry_operations msdos_dentry_operations = {
	.d_hash		= msdos_hash,
	.d_compare	= msdos_cmp,
};

/*
 * AV. Wrappers for FAT sb operations. Is it wise?
 */

/***** Get inode using directory and name */
static struct dentry *msdos_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct super_block *sb = dir->i_sb;
	struct fat_slot_info sinfo;
	struct inode *inode = NULL;
	int res;

	dentry->d_op = &msdos_dentry_operations;

	lock_kernel();
	res = msdos_find(dir, dentry->d_name.name, dentry->d_name.len, &sinfo);
	if (res == -ENOENT)
		goto add;
	if (res < 0)
		goto out;
	inode = fat_build_inode(sb, sinfo.de, sinfo.i_pos);
	brelse(sinfo.bh);
	if (IS_ERR(inode)) {
		res = PTR_ERR(inode);
		goto out;
	}
add:
	res = 0;
	dentry = d_splice_alias(inode, dentry);
	if (dentry)
		dentry->d_op = &msdos_dentry_operations;
out:
	unlock_kernel();
	if (!res)
		return dentry;
	return ERR_PTR(res);
}

/***** Creates a directory entry (name is already formatted). */
static int msdos_add_entry(struct inode *dir, const unsigned char *name,
			   struct buffer_head **bh,
			   struct msdos_dir_entry **de,
			   loff_t *i_pos, int is_dir, int is_hid)
{
	int res;

	res = fat_add_entries(dir, 1, bh, de, i_pos);
	if (res < 0)
		return res;

	/*
	 * XXX all times should be set by caller upon successful completion.
	 */
	dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);

	memcpy((*de)->name, name, MSDOS_NAME);
	(*de)->attr = is_dir ? ATTR_DIR : ATTR_ARCH;
	if (is_hid)
		(*de)->attr |= ATTR_HIDDEN;
	(*de)->start = 0;
	(*de)->starthi = 0;
	fat_date_unix2dos(dir->i_mtime.tv_sec, &(*de)->time, &(*de)->date);
	(*de)->size = 0;
	mark_buffer_dirty(*bh);
	return 0;
}

/***** Create a file */
static int msdos_create(struct inode *dir, struct dentry *dentry, int mode,
			struct nameidata *nd)
{
	struct super_block *sb = dir->i_sb;
	struct fat_slot_info sinfo;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode;
	loff_t i_pos;
	int res, is_hid;
	unsigned char msdos_name[MSDOS_NAME];

	lock_kernel();
	res = msdos_format_name(dentry->d_name.name, dentry->d_name.len,
				msdos_name, &MSDOS_SB(sb)->options);
	if (res < 0) {
		unlock_kernel();
		return res;
	}
	is_hid = (dentry->d_name.name[0] == '.') && (msdos_name[0] != '.');
	/* Have to do it due to foo vs. .foo conflicts */
	if (!fat_scan(dir, msdos_name, &sinfo)) {
		brelse(sinfo.bh);
		unlock_kernel();
		return -EINVAL;
	}

	res = msdos_add_entry(dir, msdos_name, &bh, &de, &i_pos, 0, is_hid);
	if (res) {
		unlock_kernel();
		return res;
	}
	inode = fat_build_inode(sb, de, i_pos);
	brelse(bh);
	if (IS_ERR(inode)) {
		unlock_kernel();
		return PTR_ERR(inode);
	}
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	d_instantiate(dentry, inode);
	unlock_kernel();
	return 0;
}

/***** Remove a directory */
static int msdos_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct fat_slot_info sinfo;
	int err;

	lock_kernel();
	/*
	 * Check whether the directory is not in use, then check
	 * whether it is empty.
	 */
	err = fat_dir_empty(inode);
	if (err)
		goto out;
	err = msdos_find(dir, dentry->d_name.name, dentry->d_name.len, &sinfo);
	if (err)
		goto out;

	sinfo.de->name[0] = DELETED_FLAG;
	mark_buffer_dirty(sinfo.bh);
	brelse(sinfo.bh);
	fat_detach(inode);
	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);

	dir->i_nlink--;
	mark_inode_dirty(dir);
out:
	unlock_kernel();

	return err;
}

/***** Make a directory */
static int msdos_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct super_block *sb = dir->i_sb;
	struct fat_slot_info sinfo;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode;
	int res, is_hid;
	unsigned char msdos_name[MSDOS_NAME];
	loff_t i_pos;

	lock_kernel();
	res = msdos_format_name(dentry->d_name.name, dentry->d_name.len,
				msdos_name, &MSDOS_SB(sb)->options);
	if (res < 0) {
		unlock_kernel();
		return res;
	}
	is_hid = (dentry->d_name.name[0] == '.') && (msdos_name[0] != '.');
	/* foo vs .foo situation */
	if (!fat_scan(dir, msdos_name, &sinfo)) {
		brelse(sinfo.bh);
		res = -EINVAL;
		goto out_unlock;
	}

	res = msdos_add_entry(dir, msdos_name, &bh, &de, &i_pos, 1, is_hid);
	if (res)
		goto out_unlock;
	inode = fat_build_inode(dir->i_sb, de, i_pos);
	if (IS_ERR(inode)) {
		brelse(bh);
		res = PTR_ERR(inode);
		goto out_unlock;
	}

	dir->i_nlink++;
	inode->i_nlink = 2;	/* no need to mark them dirty */

	res = fat_new_dir(inode, dir, 0);
	if (res)
		goto mkdir_error;
	brelse(bh);

	d_instantiate(dentry, inode);
	res = 0;

out_unlock:
	unlock_kernel();
	return res;

mkdir_error:
	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	dir->i_nlink--;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	de->name[0] = DELETED_FLAG;
	mark_buffer_dirty(bh);
	brelse(bh);
	fat_detach(inode);
	iput(inode);
	goto out_unlock;
}

/***** Unlink a file */
static int msdos_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct fat_slot_info sinfo;
	int err;

	lock_kernel();
	err = msdos_find(dir, dentry->d_name.name, dentry->d_name.len, &sinfo);
	if (err)
		goto unlink_done;

	sinfo.de->name[0] = DELETED_FLAG;
	mark_buffer_dirty(sinfo.bh);
	brelse(sinfo.bh);
	fat_detach(inode);
	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);

	mark_inode_dirty(dir);
unlink_done:
	unlock_kernel();

	return err;
}

static int do_msdos_rename(struct inode *old_dir, unsigned char *old_name,
			   struct dentry *old_dentry,
			   struct inode *new_dir, unsigned char *new_name,
			   struct dentry *new_dentry, int is_hid)
{
	struct buffer_head *dotdot_bh;
	struct msdos_dir_entry *dotdot_de;
	loff_t dotdot_i_pos;
	struct inode *old_inode, *new_inode;
	struct fat_slot_info old_sinfo, sinfo;
	int err, is_dir;

	old_sinfo.bh = dotdot_bh = NULL;
	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;

	err = fat_scan(old_dir, old_name, &old_sinfo);
	if (err) {
		err = -EIO;
		goto out;
	}

	is_dir = S_ISDIR(old_inode->i_mode);
	if (is_dir) {
		if (fat_get_dotdot_entry(old_inode, &dotdot_bh, &dotdot_de,
					 &dotdot_i_pos) < 0) {
			err = -EIO;
			goto out;
		}
	}

	err = fat_scan(new_dir, new_name, &sinfo);
	if (!err) {
		brelse(sinfo.bh);
		if (!new_inode) {
			/* "foo" -> ".foo" case. just change the ATTR_HIDDEN */
			if (sinfo.de != old_sinfo.de) {
				err = -EINVAL;
				goto out;
			}
			if (is_hid)
				MSDOS_I(old_inode)->i_attrs |= ATTR_HIDDEN;
			else
				MSDOS_I(old_inode)->i_attrs &= ~ATTR_HIDDEN;
			mark_inode_dirty(old_inode);

			old_dir->i_version++;
			old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME_SEC;
			mark_inode_dirty(old_dir);
			goto out;
		}
	}
	if (new_inode) {
		if (err)
			goto out;
		if (MSDOS_I(new_inode)->i_pos != sinfo.i_pos) {
			/* WTF??? Cry and fail. */
			printk(KERN_WARNING "msdos_rename: fs corrupted\n");
			goto out;
		}

		if (is_dir) {
			err = fat_dir_empty(new_inode);
			if (err)
				goto out;
		}
		fat_detach(new_inode);
	} else {
		err = msdos_add_entry(new_dir, new_name, &sinfo.bh, &sinfo.de,
				      &sinfo.i_pos, is_dir, is_hid);
		if (err)
			goto out;
		brelse(sinfo.bh);
	}
	new_dir->i_version++;

	old_sinfo.de->name[0] = DELETED_FLAG;
	mark_buffer_dirty(old_sinfo.bh);
	brelse(old_sinfo.bh);
	old_sinfo.bh = NULL;
	if (is_dir)
		old_dir->i_nlink--;
	fat_detach(old_inode);
	fat_attach(old_inode, sinfo.i_pos);
	if (is_hid)
		MSDOS_I(old_inode)->i_attrs |= ATTR_HIDDEN;
	else
		MSDOS_I(old_inode)->i_attrs &= ~ATTR_HIDDEN;
	mark_inode_dirty(old_inode);

	old_dir->i_version++;
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(old_dir);

	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME_SEC;
		mark_inode_dirty(new_inode);
	}
	if (is_dir) {
		int start = MSDOS_I(new_dir)->i_logstart;
		dotdot_de->start = cpu_to_le16(start);
		dotdot_de->starthi = cpu_to_le16(start >> 16);
		mark_buffer_dirty(dotdot_bh);

		if (new_inode) {
			new_inode->i_nlink--;
		} else {
			new_dir->i_nlink++;
			mark_inode_dirty(new_dir);
		}
	}
out:
	brelse(dotdot_bh);
	brelse(old_sinfo.bh);
	return err;
}

/***** Rename, a wrapper for rename_same_dir & rename_diff_dir */
static int msdos_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	unsigned char old_msdos_name[MSDOS_NAME], new_msdos_name[MSDOS_NAME];
	int err, is_hid;

	lock_kernel();

	err = msdos_format_name(old_dentry->d_name.name,
				old_dentry->d_name.len, old_msdos_name,
				&MSDOS_SB(old_dir->i_sb)->options);
	if (err)
		goto out;
	err = msdos_format_name(new_dentry->d_name.name,
				new_dentry->d_name.len, new_msdos_name,
				&MSDOS_SB(new_dir->i_sb)->options);
	if (err)
		goto out;

	is_hid =
	     (new_dentry->d_name.name[0] == '.') && (new_msdos_name[0] != '.');

	err = do_msdos_rename(old_dir, old_msdos_name, old_dentry,
			      new_dir, new_msdos_name, new_dentry, is_hid);
out:
	unlock_kernel();
	return err;
}

static struct inode_operations msdos_dir_inode_operations = {
	.create		= msdos_create,
	.lookup		= msdos_lookup,
	.unlink		= msdos_unlink,
	.mkdir		= msdos_mkdir,
	.rmdir		= msdos_rmdir,
	.rename		= msdos_rename,
	.setattr	= fat_notify_change,
};

static int msdos_fill_super(struct super_block *sb, void *data, int silent)
{
	int res;

	res = fat_fill_super(sb, data, silent, &msdos_dir_inode_operations, 0);
	if (res)
		return res;

	sb->s_root->d_op = &msdos_dentry_operations;
	return 0;
}

static struct super_block *msdos_get_sb(struct file_system_type *fs_type,
					int flags, const char *dev_name,
					void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, msdos_fill_super);
}

static struct file_system_type msdos_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "msdos",
	.get_sb		= msdos_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_msdos_fs(void)
{
	return register_filesystem(&msdos_fs_type);
}

static void __exit exit_msdos_fs(void)
{
	unregister_filesystem(&msdos_fs_type);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Werner Almesberger");
MODULE_DESCRIPTION("MS-DOS filesystem support");

module_init(init_msdos_fs)
module_exit(exit_msdos_fs)
