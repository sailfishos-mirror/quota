/*
 *	quotaio_rpc.c - quota IO operations for RPC (just wrappers for RPC calls)
 */

#include <stdio.h>
#include <sys/types.h>

#include "quotaio.h"
#include "dqblk_rpc.h"
#include "rquota_client.h"

static struct dquot *rpc_read_dquot(struct quota_handle *h, qid_t id);
static int rpc_commit_dquot(struct dquot *dquot);

struct quotafile_ops quotafile_ops_rpc = {
	NULL,			/* init_io */
	NULL,			/* new_io */
	NULL,			/* end_io */
	NULL,			/* write_info */
	rpc_read_dquot,
	rpc_commit_dquot,
	NULL			/* scan_dquots */
};

/*
 *	Read a dqblk struct from RPC server - just wrapper function.
 */
static struct dquot *rpc_read_dquot(struct quota_handle *h, qid_t id)
{
#ifdef RPC
	struct dquot *dquot = get_empty_dquot();

	dquot->dq_id = id;
	dquot->dq_h = h;
	rpc_rquota_get(dquot);
	return dquot;
#else
	errno = ENOTSUP;
	return NULL;
#endif
}

/*
 *	Write a dqblk struct to RPC server - just wrapper function.
 */
static int rpc_commit_dquot(struct dquot *dquot)
{
#ifdef RPC
	rpc_rquota_set(QCMD(Q_RPC_SETQUOTA, dquot->dq_h->qh_type), dquot);
	return 0;
#else
	errno = ENOTSUP;
	return -1;
#endif
}
