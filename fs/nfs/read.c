/*
 * linux/fs/nfs/read.c
 *
 * Block I/O for NFS
 *
 * Partial copy of Linus' read cache modifications to fs/nfs/file.c
 * modified for async RPC by okir@monad.swb.de
 *
 * We do an ugly hack here in order to return proper error codes to the
 * user program when a read request failed: since generic_file_read
 * only checks the return value of inode->i_op->readpage() which is always 0
 * for async RPC, we set the error bit of the page to 1 when an error occurs,
 * and make nfs_readpage transmit requests synchronously when encountering this.
 * This is only a small problem, though, since we now retry all operations
 * within the RPC code when root squashing is suspected.
 */

#include <linux/config.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/nfs_flushd.h>
#include <linux/smp_lock.h>

#include <asm/system.h>

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static kmem_cache_t *nfs_rdata_cachep;

static __inline__ struct nfs_read_data *nfs_readdata_alloc(void)
{
	struct nfs_read_data   *p;
	p = kmem_cache_alloc(nfs_rdata_cachep, SLAB_NOFS);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

static __inline__ void nfs_readdata_free(struct nfs_read_data *p)
{
	kmem_cache_free(nfs_rdata_cachep, p);
}

void nfs_readdata_release(struct rpc_task *task)
{
        struct nfs_read_data   *data = (struct nfs_read_data *)task->tk_calldata;
        nfs_readdata_free(data);
}

/*
 * Read a page synchronously.
 */
static int
nfs_readpage_sync(struct file *file, struct inode *inode, struct page *page)
{
	struct rpc_cred	*cred = NULL;
	struct nfs_fattr fattr;
	unsigned int	offset = 0;
	unsigned int	rsize = NFS_SERVER(inode)->rsize;
	unsigned int	count = PAGE_CACHE_SIZE;
	int		result;
	int		flags = IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0;
	int		eof;

	dprintk("NFS: nfs_readpage_sync(%p)\n", page);

	if (file)
		cred = nfs_file_cred(file);

	/*
	 * This works now because the socket layer never tries to DMA
	 * into this buffer directly.
	 */
	do {
		if (count < rsize)
			rsize = count;

		dprintk("NFS: nfs_proc_read(%s, (%s/%Ld), %Lu, %u)\n",
			NFS_SERVER(inode)->hostname,
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			(unsigned long long)offset, rsize);

		lock_kernel();
		result = NFS_PROTO(inode)->read(inode, cred, &fattr, flags,
						offset, rsize, page, &eof);
		nfs_refresh_inode(inode, &fattr);
		unlock_kernel();

		/*
		 * Even if we had a partial success we can't mark the page
		 * cache valid.
		 */
		if (result < 0) {
			if (result == -EISDIR)
				result = -EINVAL;
			goto io_error;
		}
		count  -= result;
		offset += result;
		if (result < rsize)	/* NFSv2ism */
			break;
	} while (count);

	if (count) {
		char *kaddr = kmap(page);
		memset(kaddr + offset, 0, count);
		kunmap(page);
	}
	flush_dcache_page(page);
	SetPageUptodate(page);
	if (PageError(page))
		ClearPageError(page);
	result = 0;

io_error:
	unlock_page(page);
	return result;
}

/*
 * Add a request to the inode's asynchronous read list.
 */
static inline void
nfs_mark_request_read(struct nfs_page *req)
{
	struct inode *inode = req->wb_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&nfs_wreq_lock);
	nfs_list_add_request(req, &nfsi->read);
	nfsi->nread++;
	__nfs_add_lru(&NFS_SERVER(inode)->lru_read, req);
	spin_unlock(&nfs_wreq_lock);
}

static int
nfs_readpage_async(struct file *file, struct inode *inode, struct page *page)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page	*new;

	new = nfs_create_request(nfs_file_cred(file), inode, page, 0, PAGE_CACHE_SIZE);
	if (IS_ERR(new))
		return PTR_ERR(new);
	nfs_mark_request_read(new);

	if (nfsi->nread >= NFS_SERVER(inode)->rpages ||
	    page->index == (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)
		nfs_pagein_inode(inode, 0, 0);
	return 0;
}

/*
 * Set up the NFS read request struct
 */
static void
nfs_read_rpcsetup(struct list_head *head, struct nfs_read_data *data)
{
	struct inode		*inode;
	struct nfs_page		*req;
	struct page		**pages;
	unsigned int		count;

	pages = data->pagevec;
	count = 0;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &data->pages);
		*pages++ = req->wb_page;
		count += req->wb_bytes;
	}
	req = nfs_list_entry(data->pages.next);
	data->inode	  = inode = req->wb_inode;
	data->cred	  = req->wb_cred;

	NFS_PROTO(inode)->read_setup(data, count);

	dprintk("NFS: %4d initiated read call (req %s/%Ld, %u bytes @ offset %Lu.\n",
			data->task.tk_pid,
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			count,
			(unsigned long long)req_offset(req) + req->wb_offset);
}

static void
nfs_async_read_error(struct list_head *head)
{
	struct nfs_page	*req;
	struct page	*page;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		page = req->wb_page;
		nfs_list_remove_request(req);
		SetPageError(page);
		unlock_page(page);
		nfs_clear_request(req);
		nfs_release_request(req);
		nfs_unlock_request(req);
	}
}

static int
nfs_pagein_one(struct list_head *head, struct inode *inode)
{
	struct rpc_clnt		*clnt = NFS_CLIENT(inode);
	struct nfs_read_data	*data;
	sigset_t		oldset;

	data = nfs_readdata_alloc();
	if (!data)
		goto out_bad;

	nfs_read_rpcsetup(head, data);

	/* Start the async call */
	rpc_clnt_sigmask(clnt, &oldset);
	lock_kernel();
	rpc_execute(&data->task);
	unlock_kernel();
	rpc_clnt_sigunmask(clnt, &oldset);
	return 0;
out_bad:
	nfs_async_read_error(head);
	return -ENOMEM;
}

int
nfs_pagein_list(struct list_head *head, int rpages)
{
	LIST_HEAD(one_request);
	struct nfs_page		*req;
	int			error = 0;
	unsigned int		pages = 0;

	while (!list_empty(head)) {
		pages += nfs_coalesce_requests(head, &one_request, rpages);
		req = nfs_list_entry(one_request.next);
		error = nfs_pagein_one(&one_request, req->wb_inode);
		if (error < 0)
			break;
	}
	if (error >= 0)
		return pages;

	nfs_async_read_error(head);
	return error;
}

/**
 * nfs_scan_lru_read_timeout - Scan LRU list for timed out read requests
 * @server: NFS superblock data
 * @dst: destination list
 *
 * Moves a maximum of 'rpages' timed out requests from the NFS read LRU list.
 * The elements are checked to ensure that they form a contiguous set
 * of pages, and that they originated from the same file.
 */
int
nfs_scan_lru_read_timeout(struct nfs_server *server, struct list_head *dst)
{
	struct nfs_inode *nfsi;
	int npages;

	npages = nfs_scan_lru_timeout(&server->lru_read, dst, server->rpages);
	if (npages) {
		nfsi = NFS_I(nfs_list_entry(dst->next)->wb_inode);
		nfsi->nread -= npages;
	}
	return npages;
}

/**
 * nfs_scan_lru_read - Scan LRU list for read requests
 * @server: NFS superblock data
 * @dst: destination list
 *
 * Moves a maximum of 'rpages' requests from the NFS read LRU list.
 * The elements are checked to ensure that they form a contiguous set
 * of pages, and that they originated from the same file.
 */
int
nfs_scan_lru_read(struct nfs_server *server, struct list_head *dst)
{
	struct nfs_inode *nfsi;
	int npages;

	npages = nfs_scan_lru(&server->lru_read, dst, server->rpages);
	if (npages) {
		nfsi = NFS_I(nfs_list_entry(dst->next)->wb_inode);
		nfsi->nread -= npages;
	}
	return npages;
}

/*
 * nfs_scan_read - Scan an inode for read requests
 * @inode: NFS inode to scan
 * @dst: destination list
 * @idx_start: lower bound of page->index to scan
 * @npages: idx_start + npages sets the upper bound to scan
 *
 * Moves requests from the inode's read list.
 * The requests are *not* checked to ensure that they form a contiguous set.
 */
static int
nfs_scan_read(struct inode *inode, struct list_head *dst, unsigned long idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int	res;
	res = nfs_scan_list(&nfsi->read, dst, NULL, idx_start, npages);
	nfsi->nread -= res;
	if ((nfsi->nread == 0) != list_empty(&nfsi->read))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.nread.\n");
	return res;
}

int nfs_pagein_inode(struct inode *inode, unsigned long idx_start,
		     unsigned int npages)
{
	LIST_HEAD(head);
	int	res,
		error = 0;

	spin_lock(&nfs_wreq_lock);
	res = nfs_scan_read(inode, &head, idx_start, npages);
	spin_unlock(&nfs_wreq_lock);
	if (res)
		error = nfs_pagein_list(&head, NFS_SERVER(inode)->rpages);
	if (error < 0)
		return error;
	return res;
}

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
void
nfs_readpage_result(struct rpc_task *task, unsigned int count, int eof)
{
	struct nfs_read_data	*data = (struct nfs_read_data *) task->tk_calldata;
	struct inode		*inode = data->inode;

	dprintk("NFS: %4d nfs_readpage_result, (status %d)\n",
		task->tk_pid, task->tk_status);

	nfs_refresh_inode(inode, &data->fattr);
	while (!list_empty(&data->pages)) {
		struct nfs_page *req = nfs_list_entry(data->pages.next);
		struct page *page = req->wb_page;
		nfs_list_remove_request(req);

		if (task->tk_status >= 0) {
			if (count < PAGE_CACHE_SIZE) {
				char *p = kmap(page);
				memset(p + count, 0, PAGE_CACHE_SIZE - count);
				kunmap(page);
				count = 0;
				if (eof)
					SetPageUptodate(page);
				else
					SetPageError(page);
			} else {
				count -= PAGE_CACHE_SIZE;
				SetPageUptodate(page);
			}
		} else
			SetPageError(page);
		flush_dcache_page(page);
		unlock_page(page);

		dprintk("NFS: read (%s/%Ld %d@%Ld)\n",
                        req->wb_inode->i_sb->s_id,
                        (long long)NFS_FILEID(req->wb_inode),
                        req->wb_bytes,
                        (long long)(req_offset(req) + req->wb_offset));
		nfs_clear_request(req);
		nfs_release_request(req);
		nfs_unlock_request(req);
	}
}

/*
 * Read a page over NFS.
 * We read the page synchronously in the following cases:
 *  -	The NFS rsize is smaller than PAGE_CACHE_SIZE. We could kludge our way
 *	around this by creating several consecutive read requests, but
 *	that's hardly worth it.
 *  -	The error flag is set for this page. This happens only when a
 *	previous async read operation failed.
 */
int
nfs_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	int		error;

	dprintk("NFS: nfs_readpage (%p %ld@%lu)\n",
		page, PAGE_CACHE_SIZE, page->index);
	/*
	 * Try to flush any pending writes to the file..
	 *
	 * NOTE! Because we own the page lock, there cannot
	 * be any new pending writes generated at this point
	 * for this page (other pages can be written to).
	 */
	error = nfs_wb_page(inode, page);
	if (error)
		goto out_error;

	if (!PageError(page) && NFS_SERVER(inode)->rsize >= PAGE_CACHE_SIZE) {
		error = nfs_readpage_async(file, inode, page);
		goto out;
	}

	error = nfs_readpage_sync(file, inode, page);
	if (error < 0 && IS_SWAPFILE(inode))
		printk("Aiee.. nfs swap-in of page failed!\n");
out:
	return error;

out_error:
	unlock_page(page);
	goto out;
}

int nfs_init_readpagecache(void)
{
	nfs_rdata_cachep = kmem_cache_create("nfs_read_data",
					     sizeof(struct nfs_read_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_rdata_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_readpagecache(void)
{
	if (kmem_cache_destroy(nfs_rdata_cachep))
		printk(KERN_INFO "nfs_read_data: not all structures were freed\n");
}
