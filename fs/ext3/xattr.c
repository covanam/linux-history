/*
 * linux/fs/ext3/xattr.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 *
 * Fix by Harrison Xing <harrison@mountainviewdata.com>.
 * Ext3 code with a lot of help from Eric Jarman <ejarman@acm.org>.
 * Extended attributes for symlinks and special files added per
 *  suggestion of Luka Renko <luka.renko@hermes.si>.
 * xattr consolidation Copyright (c) 2004 James Morris <jmorris@redhat.com>,
 *  Red Hat Inc.
 */

/*
 * Extended attributes are stored on disk blocks allocated outside of
 * any inode. The i_file_acl field is then made to point to this allocated
 * block. If all extended attributes of an inode are identical, these
 * inodes may share the same extended attribute block. Such situations
 * are automatically detected by keeping a cache of recent attribute block
 * numbers and hashes over the block's contents in memory.
 *
 *
 * Extended attribute block layout:
 *
 *   +------------------+
 *   | header           |
 *   | entry 1          | |
 *   | entry 2          | | growing downwards
 *   | entry 3          | v
 *   | four null bytes  |
 *   | . . .            |
 *   | value 1          | ^
 *   | value 3          | | growing upwards
 *   | value 2          | |
 *   +------------------+
 *
 * The block header is followed by multiple entry descriptors. These entry
 * descriptors are variable in size, and alligned to EXT3_XATTR_PAD
 * byte boundaries. The entry descriptors are sorted by attribute name,
 * so that two extended attribute blocks can be compared efficiently.
 *
 * Attribute values are aligned to the end of the block, stored in
 * no specific order. They are also padded to EXT3_XATTR_PAD byte
 * boundaries. No additional gaps are left between them.
 *
 * Locking strategy
 * ----------------
 * EXT3_I(inode)->i_file_acl is protected by EXT3_I(inode)->xattr_sem.
 * EA blocks are only changed if they are exclusive to an inode, so
 * holding xattr_sem also means that nothing but the EA block's reference
 * count will change. Multiple writers to an EA block are synchronized
 * by the bh lock. No more than a single bh lock is held at any time
 * to avoid deadlocks.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include <linux/mbcache.h>
#include <linux/quotaops.h>
#include <linux/rwsem.h>
#include "xattr.h"
#include "acl.h"

#define BHDR(bh) ((struct ext3_xattr_header *)((bh)->b_data))
#define ENTRY(ptr) ((struct ext3_xattr_entry *)(ptr))
#define BFIRST(bh) ENTRY(BHDR(bh)+1)
#define IS_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)

#ifdef EXT3_XATTR_DEBUG
# define ea_idebug(inode, f...) do { \
		printk(KERN_DEBUG "inode %s:%ld: ", \
			inode->i_sb->s_id, inode->i_ino); \
		printk(f); \
		printk("\n"); \
	} while (0)
# define ea_bdebug(bh, f...) do { \
		char b[BDEVNAME_SIZE]; \
		printk(KERN_DEBUG "block %s:%lu: ", \
			bdevname(bh->b_bdev, b), \
			(unsigned long) bh->b_blocknr); \
		printk(f); \
		printk("\n"); \
	} while (0)
#else
# define ea_idebug(f...)
# define ea_bdebug(f...)
#endif

static void ext3_xattr_cache_insert(struct buffer_head *);
static struct buffer_head *ext3_xattr_cache_find(struct inode *,
						 struct ext3_xattr_header *,
						 struct mb_cache_entry **);
static void ext3_xattr_rehash(struct ext3_xattr_header *,
			      struct ext3_xattr_entry *);

static struct mb_cache *ext3_xattr_cache;

static struct xattr_handler *ext3_xattr_handler_map[] = {
	[EXT3_XATTR_INDEX_USER]		     = &ext3_xattr_user_handler,
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	[EXT3_XATTR_INDEX_POSIX_ACL_ACCESS]  = &ext3_xattr_acl_access_handler,
	[EXT3_XATTR_INDEX_POSIX_ACL_DEFAULT] = &ext3_xattr_acl_default_handler,
#endif
	[EXT3_XATTR_INDEX_TRUSTED]	     = &ext3_xattr_trusted_handler,
#ifdef CONFIG_EXT3_FS_SECURITY
	[EXT3_XATTR_INDEX_SECURITY]	     = &ext3_xattr_security_handler,
#endif
};

struct xattr_handler *ext3_xattr_handlers[] = {
	&ext3_xattr_user_handler,
	&ext3_xattr_trusted_handler,
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	&ext3_xattr_acl_access_handler,
	&ext3_xattr_acl_default_handler,
#endif
#ifdef CONFIG_EXT3_FS_SECURITY
	&ext3_xattr_security_handler,
#endif
	NULL
};

static inline struct xattr_handler *
ext3_xattr_handler(int name_index)
{
	struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index < ARRAY_SIZE(ext3_xattr_handler_map))
		handler = ext3_xattr_handler_map[name_index];
	return handler;
}

/*
 * Inode operation listxattr()
 *
 * dentry->d_inode->i_sem: don't care
 */
ssize_t
ext3_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	return ext3_xattr_list(dentry->d_inode, buffer, size);
}

static int
ext3_xattr_check_names(struct ext3_xattr_entry *entry, void *end)
{
	while (!IS_LAST_ENTRY(entry)) {
		struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(entry);
		if ((void *)next >= end)
			return -EIO;
		entry = next;
	}
	return 0;
}

static inline int
ext3_xattr_check_block(struct buffer_head *bh)
{
	int error;

	if (BHDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    BHDR(bh)->h_blocks != cpu_to_le32(1))
		return -EIO;
	error = ext3_xattr_check_names(BFIRST(bh), bh->b_data + bh->b_size);
	return error;
}

static inline int
ext3_xattr_check_entry(struct ext3_xattr_entry *entry, size_t size)
{
	size_t value_size = le32_to_cpu(entry->e_value_size);

	if (entry->e_value_block != 0 || value_size > size ||
	    le16_to_cpu(entry->e_value_offs) + value_size > size)
		return -EIO;
	return 0;
}

static int
ext3_xattr_find_entry(struct ext3_xattr_entry **pentry, int name_index,
		      const char *name, size_t size, int sorted)
{
	struct ext3_xattr_entry *entry;
	size_t name_len;
	int cmp = 1;

	if (name == NULL)
		return -EINVAL;
	name_len = strlen(name);
	entry = *pentry;
	for (; !IS_LAST_ENTRY(entry); entry = EXT3_XATTR_NEXT(entry)) {
		cmp = name_index - entry->e_name_index;
		if (!cmp)
			cmp = name_len - entry->e_name_len;
		if (!cmp)
			cmp = memcmp(name, entry->e_name, name_len);
		if (cmp <= 0 && (sorted || cmp == 0))
			break;
	}
	*pentry = entry;
	if (!cmp && ext3_xattr_check_entry(entry, size))
			return -EIO;
	return cmp ? -ENODATA : 0;
}

/*
 * ext3_xattr_get()
 *
 * Copy an extended attribute into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
ext3_xattr_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	struct buffer_head *bh = NULL;
	struct ext3_xattr_entry *entry;
	size_t size;
	int error;

	ea_idebug(inode, "name=%d.%s, buffer=%p, buffer_size=%ld",
		  name_index, name, buffer, (long)buffer_size);

	if (name == NULL)
		return -EINVAL;
	down_read(&EXT3_I(inode)->xattr_sem);
	error = -ENODATA;
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %d", EXT3_I(inode)->i_file_acl);
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	if (!bh)
		goto cleanup;
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	if (ext3_xattr_check_block(bh)) {
bad_block:	ext3_error(inode->i_sb, __FUNCTION__,
			   "inode %ld: bad block %d", inode->i_ino,
			   EXT3_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}
	ext3_xattr_cache_insert(bh);
	entry = BFIRST(bh);
	error = ext3_xattr_find_entry(&entry, name_index, name, bh->b_size, 1);
	if (error == -EIO)
		goto bad_block;
	if (error)
		goto cleanup;
	size = le32_to_cpu(entry->e_value_size);
	if (buffer) {
		error = -ERANGE;
		if (size > buffer_size)
			goto cleanup;
		memcpy(buffer, bh->b_data + le16_to_cpu(entry->e_value_offs),
		       size);
	}
	error = size;

cleanup:
	brelse(bh);
	up_read(&EXT3_I(inode)->xattr_sem);
	return error;
}

static int
ext3_xattr_list_entries(struct inode *inode, struct ext3_xattr_entry *entry,
			char *buffer, size_t buffer_size)
{
	size_t rest = buffer_size;

	for (; !IS_LAST_ENTRY(entry); entry = EXT3_XATTR_NEXT(entry)) {
		struct xattr_handler *handler =
			ext3_xattr_handler(entry->e_name_index);

		if (handler) {
			size_t size = handler->list(inode, buffer, rest,
						    entry->e_name,
						    entry->e_name_len);
			if (buffer) {
				if (size > rest)
					return -ERANGE;
				buffer += size;
			}
			rest -= size;
		}
	}
	return buffer_size - rest;
}

/*
 * ext3_xattr_list()
 *
 * Copy a list of attribute names into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
ext3_xattr_list(struct inode *inode, char *buffer, size_t buffer_size)
{
	struct buffer_head *bh = NULL;
	int error;

	ea_idebug(inode, "buffer=%p, buffer_size=%ld",
		  buffer, (long)buffer_size);

	down_read(&EXT3_I(inode)->xattr_sem);
	error = 0;
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %d", EXT3_I(inode)->i_file_acl);
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	error = -EIO;
	if (!bh)
		goto cleanup;
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	if (ext3_xattr_check_block(bh)) {
		ext3_error(inode->i_sb, __FUNCTION__,
			   "inode %ld: bad block %d", inode->i_ino,
			   EXT3_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}
	ext3_xattr_cache_insert(bh);
	error = ext3_xattr_list_entries(inode, BFIRST(bh), buffer, buffer_size);

cleanup:
	brelse(bh);
	up_read(&EXT3_I(inode)->xattr_sem);

	return error;
}

/*
 * If the EXT3_FEATURE_COMPAT_EXT_ATTR feature of this file system is
 * not set, set it.
 */
static void ext3_xattr_update_super_block(handle_t *handle,
					  struct super_block *sb)
{
	if (EXT3_HAS_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_EXT_ATTR))
		return;

	lock_super(sb);
	if (ext3_journal_get_write_access(handle, EXT3_SB(sb)->s_sbh) == 0) {
		EXT3_SB(sb)->s_es->s_feature_compat |=
			cpu_to_le32(EXT3_FEATURE_COMPAT_EXT_ATTR);
		sb->s_dirt = 1;
		ext3_journal_dirty_metadata(handle, EXT3_SB(sb)->s_sbh);
	}
	unlock_super(sb);
}

/*
 * Release the xattr block BH: If the reference count is > 1, decrement
 * it; otherwise free the block.
 */
static void
ext3_xattr_release_block(handle_t *handle, struct inode *inode,
			 struct buffer_head *bh)
{
	struct mb_cache_entry *ce = NULL;

	ce = mb_cache_entry_get(ext3_xattr_cache, bh->b_bdev, bh->b_blocknr);
	if (BHDR(bh)->h_refcount == cpu_to_le32(1)) {
		ea_bdebug(bh, "refcount now=0; freeing");
		if (ce)
			mb_cache_entry_free(ce);
		ext3_free_blocks(handle, inode, bh->b_blocknr, 1);
		get_bh(bh);
		ext3_forget(handle, 1, inode, bh, bh->b_blocknr);
	} else {
		if (ext3_journal_get_write_access(handle, bh) == 0) {
			lock_buffer(bh);
			BHDR(bh)->h_refcount = cpu_to_le32(
				le32_to_cpu(BHDR(bh)->h_refcount) - 1);
			ext3_journal_dirty_metadata(handle, bh);
			if (IS_SYNC(inode))
				handle->h_sync = 1;
			DQUOT_FREE_BLOCK(inode, 1);
			unlock_buffer(bh);
			ea_bdebug(bh, "refcount now=%d; releasing",
				  le32_to_cpu(BHDR(bh)->h_refcount));
		}
		if (ce)
			mb_cache_entry_release(ce);
	}
}

struct ext3_xattr_info {
	int name_index;
	const char *name;
	const void *value;
	size_t value_len;
};

struct ext3_xattr_search {
	struct ext3_xattr_entry *first;
	void *base;
	void *end;
	struct ext3_xattr_entry *here;
	int not_found;
};

static int
ext3_xattr_set_entry(struct ext3_xattr_info *i, struct ext3_xattr_search *s)
{
	struct ext3_xattr_entry *last;
	size_t free, min_offs = s->end - s->base, name_len = strlen(i->name);

	/* Compute min_offs and last. */
	last = s->first;
	for (; !IS_LAST_ENTRY(last); last = EXT3_XATTR_NEXT(last)) {
		if (!last->e_value_block && last->e_value_size) {
			size_t offs = le16_to_cpu(last->e_value_offs);
			if (offs < min_offs)
				min_offs = offs;
		}
	}
	free = min_offs - ((void *)last - s->base) - sizeof(__u32);
	if (!s->not_found) {
		if (!s->here->e_value_block && s->here->e_value_size) {
			size_t size = le32_to_cpu(s->here->e_value_size);
			free += EXT3_XATTR_SIZE(size);
		}
		free += EXT3_XATTR_LEN(name_len);
	}
	if (i->value) {
		if (free < EXT3_XATTR_SIZE(i->value_len) ||
		    free < EXT3_XATTR_LEN(name_len) +
			   EXT3_XATTR_SIZE(i->value_len))
			return -ENOSPC;
	}

	if (i->value && s->not_found) {
		/* Insert the new name. */
		size_t size = EXT3_XATTR_LEN(name_len);
		size_t rest = (void *)last - (void *)s->here + sizeof(__u32);
		memmove((void *)s->here + size, s->here, rest);
		memset(s->here, 0, size);
		s->here->e_name_index = i->name_index;
		s->here->e_name_len = name_len;
		memcpy(s->here->e_name, i->name, name_len);
	} else {
		if (!s->here->e_value_block && s->here->e_value_size) {
			void *first_val = s->base + min_offs;
			size_t offs = le16_to_cpu(s->here->e_value_offs);
			void *val = s->base + offs;
			size_t size = EXT3_XATTR_SIZE(
				le32_to_cpu(s->here->e_value_size));

			if (i->value && size == EXT3_XATTR_SIZE(i->value_len)) {
				/* The old and the new value have the same
				   size. Just replace. */
				s->here->e_value_size =
					cpu_to_le32(i->value_len);
				memset(val + size - EXT3_XATTR_PAD, 0,
				       EXT3_XATTR_PAD); /* Clear pad bytes. */
				memcpy(val, i->value, i->value_len);
				return 0;
			}

			/* Remove the old value. */
			memmove(first_val + size, first_val, val - first_val);
			memset(first_val, 0, size);
			s->here->e_value_size = 0;
			s->here->e_value_offs = 0;
			min_offs += size;

			/* Adjust all value offsets. */
			last = s->first;
			while (!IS_LAST_ENTRY(last)) {
				size_t o = le16_to_cpu(last->e_value_offs);
				if (!last->e_value_block &&
				    last->e_value_size && o < offs)
					last->e_value_offs =
						cpu_to_le16(o + size);
				last = EXT3_XATTR_NEXT(last);
			}
		}
		if (!i->value) {
			/* Remove the old name. */
			size_t size = EXT3_XATTR_LEN(name_len);
			last = ENTRY((void *)last - size);
			memmove(s->here, (void *)s->here + size,
				(void *)last - (void *)s->here + sizeof(__u32));
			memset(last, 0, size);
		}
	}

	if (i->value) {
		/* Insert the new value. */
		s->here->e_value_size = cpu_to_le32(i->value_len);
		if (i->value_len) {
			size_t size = EXT3_XATTR_SIZE(i->value_len);
			void *val = s->base + min_offs - size;
			s->here->e_value_offs = cpu_to_le16(min_offs - size);
			memset(val + size - EXT3_XATTR_PAD, 0,
			       EXT3_XATTR_PAD); /* Clear the pad bytes. */
			memcpy(val, i->value, i->value_len);
		}
	}
	return 0;
}

/*
 * ext3_xattr_set_handle()
 *
 * Create, replace or remove an extended attribute for this inode. Buffer
 * is NULL to remove an existing extended attribute, and non-NULL to
 * either replace an existing extended attribute, or create a new extended
 * attribute. The flags XATTR_REPLACE and XATTR_CREATE
 * specify that an extended attribute must exist and must not exist
 * previous to the call, respectively.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext3_xattr_set_handle(handle_t *handle, struct inode *inode, int name_index,
		      const char *name, const void *value, size_t value_len,
		      int flags)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *old_bh = NULL, *new_bh = NULL;
	struct ext3_xattr_info i = {
		.name_index = name_index,
		.name = name,
		.value = value,
		.value_len = value_len,
	};
	struct ext3_xattr_search s = {
		.not_found = 1,
	};
	struct mb_cache_entry *ce = NULL;
	int error;

#define header ((struct ext3_xattr_header *)(s.base))

	/*
	 * header -- Points either into bh, or to a temporarily
	 *           allocated buffer.
	 */

	ea_idebug(inode, "name=%d.%s, value=%p, value_len=%ld",
		  name_index, name, value, (long)value_len);

	if (IS_RDONLY(inode))
		return -EROFS;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	if (i.value == NULL)
		i.value_len = 0;
	down_write(&EXT3_I(inode)->xattr_sem);
	if (EXT3_I(inode)->i_file_acl) {
		/* The inode already has an extended attribute block. */
		old_bh = sb_bread(sb, EXT3_I(inode)->i_file_acl);
		error = -EIO;
		if (!old_bh)
			goto cleanup;
		ea_bdebug(old_bh, "b_count=%d, refcount=%d",
			atomic_read(&(old_bh->b_count)),
			le32_to_cpu(BHDR(old_bh)->h_refcount));
		if (ext3_xattr_check_block(old_bh)) {
bad_block:		ext3_error(sb, __FUNCTION__,
				"inode %ld: bad block %d", inode->i_ino,
				EXT3_I(inode)->i_file_acl);
			error = -EIO;
			goto cleanup;
		}
		/* Find the named attribute. */
		s.base = BHDR(old_bh);
		s.first = BFIRST(old_bh);
		s.end = old_bh->b_data + old_bh->b_size;
		s.here = BFIRST(old_bh);
		error = ext3_xattr_find_entry(&s.here, name_index, name,
					      old_bh->b_size, 1);
		if (error && error != -ENODATA)
			goto cleanup;
		s.not_found = error;
	}

	if (s.not_found) {
		/* Request to remove a nonexistent attribute? */
		error = -ENODATA;
		if (flags & XATTR_REPLACE)
			goto cleanup;
		error = 0;
		if (value == NULL)
			goto cleanup;
	} else {
		/* Request to create an existing attribute? */
		error = -EEXIST;
		if (flags & XATTR_CREATE)
			goto cleanup;
	}

	if (header) {
		/* assert(header == BHDR(old_bh)); */
		ce = mb_cache_entry_get(ext3_xattr_cache, old_bh->b_bdev,
					old_bh->b_blocknr);
		if (header->h_refcount == cpu_to_le32(1)) {
			if (ce) {
				mb_cache_entry_free(ce);
				ce = NULL;
			}
			ea_bdebug(old_bh, "modifying in-place");
			error = ext3_journal_get_write_access(handle, old_bh);
			if (error)
				goto cleanup;
			lock_buffer(old_bh);
			error = ext3_xattr_set_entry(&i, &s);
			if (!error) {
				if (!IS_LAST_ENTRY(s.first))
					ext3_xattr_rehash(header, s.here);
				ext3_xattr_cache_insert(old_bh);
			}
			unlock_buffer(old_bh);
			if (error == -EIO)
				goto bad_block;
			if (!error && old_bh && header == BHDR(old_bh)) {
				error = ext3_journal_dirty_metadata(handle,
								    old_bh);
			}
			if (error)
				goto cleanup;
			goto inserted;
		} else {
			int offset = (char *)s.here - old_bh->b_data;

			if (ce) {
				mb_cache_entry_release(ce);
				ce = NULL;
			}
			ea_bdebug(old_bh, "cloning");
			s.base = kmalloc(old_bh->b_size, GFP_KERNEL);
			/*assert(header == s.base)*/
			error = -ENOMEM;
			if (header == NULL)
				goto cleanup;
			memcpy(header, BHDR(old_bh), old_bh->b_size);
			s.first = ENTRY(header+1);
			header->h_refcount = cpu_to_le32(1);
			s.here = ENTRY(s.base + offset);
			s.end = header + old_bh->b_size;
		}
	} else {
		/* Allocate a buffer where we construct the new block. */
		s.base = kmalloc(sb->s_blocksize, GFP_KERNEL);
		/*assert(header == s.base)*/
		error = -ENOMEM;
		if (header == NULL)
			goto cleanup;
		memset(header, 0, sb->s_blocksize);
		header->h_magic = cpu_to_le32(EXT3_XATTR_MAGIC);
		header->h_blocks = header->h_refcount = cpu_to_le32(1);
		s.first = ENTRY(header+1);
		s.here = ENTRY(header+1);
		s.end = (void *)header + sb->s_blocksize;
	}

	error = ext3_xattr_set_entry(&i, &s);
	if (error == -EIO)
		goto bad_block;
	if (error)
		goto cleanup;
	if (!IS_LAST_ENTRY(s.first))
		ext3_xattr_rehash(header, s.here);

inserted:
	if (!IS_LAST_ENTRY(s.first)) {
		new_bh = ext3_xattr_cache_find(inode, header, &ce);
		if (new_bh) {
			/* We found an identical block in the cache. */
			if (new_bh == old_bh)
				ea_bdebug(new_bh, "keeping");
			else {
				/* The old block is released after updating
				   the inode. */
				error = -EDQUOT;
				if (DQUOT_ALLOC_BLOCK(inode, 1))
					goto cleanup;
				error = ext3_journal_get_write_access(handle, new_bh);
				if (error)
					goto cleanup;
				lock_buffer(new_bh);
				BHDR(new_bh)->h_refcount = cpu_to_le32(1 +
					le32_to_cpu(BHDR(new_bh)->h_refcount));
				ea_bdebug(new_bh, "reusing; refcount now=%d",
					le32_to_cpu(BHDR(new_bh)->h_refcount));
				unlock_buffer(new_bh);
				error = ext3_journal_dirty_metadata(handle,
								    new_bh);
				if (error)
					goto cleanup;
			}
			mb_cache_entry_release(ce);
			ce = NULL;
		} else if (old_bh && header == BHDR(old_bh)) {
			/* We were modifying this block in-place. */
			ea_bdebug(old_bh, "keeping this block");
			new_bh = old_bh;
			get_bh(new_bh);
		} else {
			/* We need to allocate a new block */
			int goal = le32_to_cpu(
					EXT3_SB(sb)->s_es->s_first_data_block) +
				EXT3_I(inode)->i_block_group *
				EXT3_BLOCKS_PER_GROUP(sb);
			int block = ext3_new_block(handle, inode, goal, &error);
			if (error)
				goto cleanup;
			ea_idebug(inode, "creating block %d", block);

			new_bh = sb_getblk(sb, block);
			if (!new_bh) {
getblk_failed:
				ext3_free_blocks(handle, inode, block, 1);
				error = -EIO;
				goto cleanup;
			}
			lock_buffer(new_bh);
			error = ext3_journal_get_create_access(handle, new_bh);
			if (error) {
				unlock_buffer(new_bh);
				goto getblk_failed;
			}
			memcpy(new_bh->b_data, header, new_bh->b_size);
			set_buffer_uptodate(new_bh);
			unlock_buffer(new_bh);
			ext3_xattr_cache_insert(new_bh);
			error = ext3_journal_dirty_metadata(handle, new_bh);
			if (error)
				goto cleanup;
			ext3_xattr_update_super_block(handle, sb);
		}
	}

	/* Update the inode. */
	EXT3_I(inode)->i_file_acl = new_bh ? new_bh->b_blocknr : 0;
	inode->i_ctime = CURRENT_TIME_SEC;
	ext3_mark_inode_dirty(handle, inode);
	if (IS_SYNC(inode))
		handle->h_sync = 1;

	/* Drop the previous xattr block. */
	if (old_bh && old_bh != new_bh)
		ext3_xattr_release_block(handle, inode, old_bh);
	error = 0;

cleanup:
	if (ce)
		mb_cache_entry_release(ce);
	brelse(new_bh);
	brelse(old_bh);
	if (!(old_bh && header == BHDR(old_bh)))
		kfree(header);
	up_write(&EXT3_I(inode)->xattr_sem);

	return error;

#undef header
}

/*
 * ext3_xattr_set()
 *
 * Like ext3_xattr_set_handle, but start from an inode. This extended
 * attribute modification is a filesystem transaction by itself.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext3_xattr_set(struct inode *inode, int name_index, const char *name,
	       const void *value, size_t value_len, int flags)
{
	handle_t *handle;
	int error, retries = 0;

retry:
	handle = ext3_journal_start(inode, EXT3_DATA_TRANS_BLOCKS);
	if (IS_ERR(handle)) {
		error = PTR_ERR(handle);
	} else {
		int error2;

		error = ext3_xattr_set_handle(handle, inode, name_index, name,
					      value, value_len, flags);
		error2 = ext3_journal_stop(handle);
		if (error == -ENOSPC &&
		    ext3_should_retry_alloc(inode->i_sb, &retries))
			goto retry;
		if (error == 0)
			error = error2;
	}

	return error;
}

/*
 * ext3_xattr_delete_inode()
 *
 * Free extended attribute resources associated with this inode. This
 * is called immediately before an inode is freed. We have exclusive
 * access to the inode.
 */
void
ext3_xattr_delete_inode(handle_t *handle, struct inode *inode)
{
	struct buffer_head *bh = NULL;

	down_write(&EXT3_I(inode)->xattr_sem);
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	if (!bh) {
		ext3_error(inode->i_sb, __FUNCTION__,
			"inode %ld: block %d read error", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		goto cleanup;
	}
	if (BHDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    BHDR(bh)->h_blocks != cpu_to_le32(1)) {
		ext3_error(inode->i_sb, __FUNCTION__,
			"inode %ld: bad block %d", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		goto cleanup;
	}
	ext3_xattr_release_block(handle, inode, bh);
	EXT3_I(inode)->i_file_acl = 0;

cleanup:
	brelse(bh);
	up_write(&EXT3_I(inode)->xattr_sem);
}

/*
 * ext3_xattr_put_super()
 *
 * This is called when a file system is unmounted.
 */
void
ext3_xattr_put_super(struct super_block *sb)
{
	mb_cache_shrink(ext3_xattr_cache, sb->s_bdev);
}

/*
 * ext3_xattr_cache_insert()
 *
 * Create a new entry in the extended attribute cache, and insert
 * it unless such an entry is already in the cache.
 *
 * Returns 0, or a negative error number on failure.
 */
static void
ext3_xattr_cache_insert(struct buffer_head *bh)
{
	__u32 hash = le32_to_cpu(BHDR(bh)->h_hash);
	struct mb_cache_entry *ce;
	int error;

	ce = mb_cache_entry_alloc(ext3_xattr_cache);
	if (!ce) {
		ea_bdebug(bh, "out of memory");
		return;
	}
	error = mb_cache_entry_insert(ce, bh->b_bdev, bh->b_blocknr, &hash);
	if (error) {
		mb_cache_entry_free(ce);
		if (error == -EBUSY) {
			ea_bdebug(bh, "already in cache");
			error = 0;
		}
	} else {
		ea_bdebug(bh, "inserting [%x]", (int)hash);
		mb_cache_entry_release(ce);
	}
}

/*
 * ext3_xattr_cmp()
 *
 * Compare two extended attribute blocks for equality.
 *
 * Returns 0 if the blocks are equal, 1 if they differ, and
 * a negative error number on errors.
 */
static int
ext3_xattr_cmp(struct ext3_xattr_header *header1,
	       struct ext3_xattr_header *header2)
{
	struct ext3_xattr_entry *entry1, *entry2;

	entry1 = ENTRY(header1+1);
	entry2 = ENTRY(header2+1);
	while (!IS_LAST_ENTRY(entry1)) {
		if (IS_LAST_ENTRY(entry2))
			return 1;
		if (entry1->e_hash != entry2->e_hash ||
		    entry1->e_name_len != entry2->e_name_len ||
		    entry1->e_value_size != entry2->e_value_size ||
		    memcmp(entry1->e_name, entry2->e_name, entry1->e_name_len))
			return 1;
		if (entry1->e_value_block != 0 || entry2->e_value_block != 0)
			return -EIO;
		if (memcmp((char *)header1 + le16_to_cpu(entry1->e_value_offs),
			   (char *)header2 + le16_to_cpu(entry2->e_value_offs),
			   le32_to_cpu(entry1->e_value_size)))
			return 1;

		entry1 = EXT3_XATTR_NEXT(entry1);
		entry2 = EXT3_XATTR_NEXT(entry2);
	}
	if (!IS_LAST_ENTRY(entry2))
		return 1;
	return 0;
}

/*
 * ext3_xattr_cache_find()
 *
 * Find an identical extended attribute block.
 *
 * Returns a pointer to the block found, or NULL if such a block was
 * not found or an error occurred.
 */
static struct buffer_head *
ext3_xattr_cache_find(struct inode *inode, struct ext3_xattr_header *header,
		      struct mb_cache_entry **pce)
{
	__u32 hash = le32_to_cpu(header->h_hash);
	struct mb_cache_entry *ce;

	if (!header->h_hash)
		return NULL;  /* never share */
	ea_idebug(inode, "looking for cached blocks [%x]", (int)hash);
again:
	ce = mb_cache_entry_find_first(ext3_xattr_cache, 0,
				       inode->i_sb->s_bdev, hash);
	while (ce) {
		struct buffer_head *bh;

		if (IS_ERR(ce)) {
			if (PTR_ERR(ce) == -EAGAIN)
				goto again;
			break;
		}
		bh = sb_bread(inode->i_sb, ce->e_block);
		if (!bh) {
			ext3_error(inode->i_sb, __FUNCTION__,
				"inode %ld: block %ld read error",
				inode->i_ino, (unsigned long) ce->e_block);
		} else if (le32_to_cpu(BHDR(bh)->h_refcount) >=
				EXT3_XATTR_REFCOUNT_MAX) {
			ea_idebug(inode, "block %ld refcount %d>=%d",
				  (unsigned long) ce->e_block,
				  le32_to_cpu(BHDR(bh)->h_refcount),
					  EXT3_XATTR_REFCOUNT_MAX);
		} else if (ext3_xattr_cmp(header, BHDR(bh)) == 0) {
			*pce = ce;
			return bh;
		}
		brelse(bh);
		ce = mb_cache_entry_find_next(ce, 0, inode->i_sb->s_bdev, hash);
	}
	return NULL;
}

#define NAME_HASH_SHIFT 5
#define VALUE_HASH_SHIFT 16

/*
 * ext3_xattr_hash_entry()
 *
 * Compute the hash of an extended attribute.
 */
static inline void ext3_xattr_hash_entry(struct ext3_xattr_header *header,
					 struct ext3_xattr_entry *entry)
{
	__u32 hash = 0;
	char *name = entry->e_name;
	int n;

	for (n=0; n < entry->e_name_len; n++) {
		hash = (hash << NAME_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - NAME_HASH_SHIFT)) ^
		       *name++;
	}

	if (entry->e_value_block == 0 && entry->e_value_size != 0) {
		__le32 *value = (__le32 *)((char *)header +
			le16_to_cpu(entry->e_value_offs));
		for (n = (le32_to_cpu(entry->e_value_size) +
		     EXT3_XATTR_ROUND) >> EXT3_XATTR_PAD_BITS; n; n--) {
			hash = (hash << VALUE_HASH_SHIFT) ^
			       (hash >> (8*sizeof(hash) - VALUE_HASH_SHIFT)) ^
			       le32_to_cpu(*value++);
		}
	}
	entry->e_hash = cpu_to_le32(hash);
}

#undef NAME_HASH_SHIFT
#undef VALUE_HASH_SHIFT

#define BLOCK_HASH_SHIFT 16

/*
 * ext3_xattr_rehash()
 *
 * Re-compute the extended attribute hash value after an entry has changed.
 */
static void ext3_xattr_rehash(struct ext3_xattr_header *header,
			      struct ext3_xattr_entry *entry)
{
	struct ext3_xattr_entry *here;
	__u32 hash = 0;

	ext3_xattr_hash_entry(header, entry);
	here = ENTRY(header+1);
	while (!IS_LAST_ENTRY(here)) {
		if (!here->e_hash) {
			/* Block is not shared if an entry's hash value == 0 */
			hash = 0;
			break;
		}
		hash = (hash << BLOCK_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - BLOCK_HASH_SHIFT)) ^
		       le32_to_cpu(here->e_hash);
		here = EXT3_XATTR_NEXT(here);
	}
	header->h_hash = cpu_to_le32(hash);
}

#undef BLOCK_HASH_SHIFT

int __init
init_ext3_xattr(void)
{
	ext3_xattr_cache = mb_cache_create("ext3_xattr", NULL,
		sizeof(struct mb_cache_entry) +
		sizeof(((struct mb_cache_entry *) 0)->e_indexes[0]), 1, 6);
	if (!ext3_xattr_cache)
		return -ENOMEM;
	return 0;
}

void
exit_ext3_xattr(void)
{
	if (ext3_xattr_cache)
		mb_cache_destroy(ext3_xattr_cache);
	ext3_xattr_cache = NULL;
}
