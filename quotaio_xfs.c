/*
 *	Implementation of XFS quota manager.
 *      Copyright (c) 2001 Silicon Graphics, Inc.
 */

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pot.h"
#include "common.h"
#include "bylabel.h"
#include "quotaio.h"
#include "quotasys.h"
#include "dqblk_xfs.h"
#include "quotaio_generic.h"

#define XFS_USRQUOTA(h)	((h)->qh_type == USRQUOTA && \
			(h)->qh_info.u.xfs_mdqi.qs_flags & XFS_QUOTA_UDQ_ACCT)
#define XFS_GRPQUOTA(h)	((h)->qh_type == GRPQUOTA && \
			(h)->qh_info.u.xfs_mdqi.qs_flags & XFS_QUOTA_GDQ_ACCT)
#define XFS_PRJQUOTA(h)	((h)->qh_type == PRJQUOTA && \
			(h)->qh_info.u.xfs_mdqi.qs_flags & XFS_QUOTA_PDQ_ACCT)

static int xfs_init_io(struct quota_handle *h);
static int xfs_write_info(struct quota_handle *h);
static struct dquot *xfs_read_dquot(struct quota_handle *h, qid_t id);
static int xfs_commit_dquot(struct dquot *dquot, int flags);
static int xfs_scan_dquots(struct quota_handle *h, int (*process_dquot) (struct dquot *dquot, char *dqname));
static int xfs_report(struct quota_handle *h, int verbose);

struct quotafile_ops quotafile_ops_xfs = {
init_io:	xfs_init_io,
write_info:	xfs_write_info,
read_dquot:	xfs_read_dquot,
commit_dquot:	xfs_commit_dquot,
scan_dquots:	xfs_scan_dquots,
report:		xfs_report
};

static inline time_t xfs_kern2utildqblk_ts(const struct xfs_kern_dqblk *k,
		__s32 timer, __s8 timer_hi)
{
#if SIZEOF_TIME_T > 4
	if (k->d_fieldmask & FS_DQ_BIGTIME)
		return (__u32)timer | (__s64)timer_hi << 32;
#else
	if (k->d_fieldmask & FS_DQ_BIGTIME && timer_hi != 0)
		errstr(_("Truncating kernel returned time stamp."));
#endif
	return timer;
}

static inline void xfs_util2kerndqblk_ts(const struct xfs_kern_dqblk *k,
		__s32 *timer_lo, __s8 *timer_hi, time_t timer)
{
	*timer_lo = timer;
#if SIZEOF_TIME_T > 4
	if (k->d_fieldmask & FS_DQ_BIGTIME)
		*timer_hi = timer >> 32;
	else
		*timer_hi = 0;
#else
	*timer_hi = 0;
#endif
}

static inline int want_bigtime(time_t timer)
{
	return timer > INT32_MAX || timer < INT32_MIN;
}

static inline int timer_fits_xfs_dqblk(time_t timer)
{
	return timer >= FS_DQUOT_TIMER_MIN && timer <= FS_DQUOT_TIMER_MAX;
}

/*
 *	Convert XFS kernel quota format to utility format
 */
static inline void xfs_kern2utildqblk(struct util_dqblk *u, struct xfs_kern_dqblk * k)
{
	u->dqb_ihardlimit = k->d_ino_hardlimit;
	u->dqb_isoftlimit = k->d_ino_softlimit;
	u->dqb_bhardlimit = k->d_blk_hardlimit >> 1;
	u->dqb_bsoftlimit = k->d_blk_softlimit >> 1;
	u->dqb_curinodes = k->d_icount;
	u->dqb_curspace = ((qsize_t)k->d_bcount) << 9;
	u->dqb_itime = xfs_kern2utildqblk_ts(k, k->d_itimer, k->d_itimer_hi);
	u->dqb_btime = xfs_kern2utildqblk_ts(k, k->d_btimer, k->d_btimer_hi);
}

/*
 *	Convert utility quota format to XFS kernel format
 */
static inline int xfs_util2kerndqblk(struct xfs_kern_dqblk *k, struct util_dqblk *u)
{
	memset(k, 0, sizeof(struct xfs_kern_dqblk));
	k->d_ino_hardlimit = u->dqb_ihardlimit;
	k->d_ino_softlimit = u->dqb_isoftlimit;
	k->d_blk_hardlimit = u->dqb_bhardlimit << 1;
	k->d_blk_softlimit = u->dqb_bsoftlimit << 1;
	k->d_icount = u->dqb_curinodes;
	k->d_bcount = u->dqb_curspace >> 9;
	if (!timer_fits_xfs_dqblk(u->dqb_itime) ||
	    !timer_fits_xfs_dqblk(u->dqb_btime)) {
		errno = ERANGE;
		return -1;
	}
	if (want_bigtime(u->dqb_itime) || want_bigtime(u->dqb_btime))
		k->d_fieldmask |= FS_DQ_BIGTIME;
	xfs_util2kerndqblk_ts(k, &k->d_itimer, &k->d_itimer_hi, u->dqb_itime);
	xfs_util2kerndqblk_ts(k, &k->d_btimer, &k->d_btimer_hi, u->dqb_btime);
	return 0;
}

/*
 *	Initialize quota information
 */
static int xfs_init_io(struct quota_handle *h)
{
	struct xfs_mem_dqinfo info;

	memset(&info, 0, sizeof(struct xfs_mem_dqinfo));
	if (quotactl_handle(Q_XFS_GETQSTAT, h, 0, (void *)&info) < 0)
		return -1;
	h->qh_info.dqi_bgrace = info.qs_btimelimit;
	h->qh_info.dqi_igrace = info.qs_itimelimit;
	h->qh_info.u.xfs_mdqi = info;
	return 0;
}

/*
 *	Write information (grace times)
 */
static int xfs_write_info(struct quota_handle *h)
{
	struct xfs_kern_dqblk xdqblk;

	if (!XFS_USRQUOTA(h) && !XFS_GRPQUOTA(h) && !XFS_PRJQUOTA(h))
		return 0;

	memset(&xdqblk, 0, sizeof(struct xfs_kern_dqblk));

	xdqblk.d_btimer = h->qh_info.dqi_bgrace;
	xdqblk.d_itimer = h->qh_info.dqi_igrace;
	xdqblk.d_fieldmask |= FS_DQ_TIMER_MASK;
	if (quotactl_handle(Q_XFS_SETQLIM, h, 0, (void *)&xdqblk) < 0)
		return -1;
	return 0;
}

/*
 *	Read a dqblk struct from the quota manager
 */
static struct dquot *xfs_read_dquot(struct quota_handle *h, qid_t id)
{
	struct dquot *dquot = get_empty_dquot();
	struct xfs_kern_dqblk xdqblk;

	dquot->dq_id = id;
	dquot->dq_h = h;

	if (!XFS_USRQUOTA(h) && !XFS_GRPQUOTA(h) && !XFS_PRJQUOTA(h))
		return dquot;

	if (quotactl_handle(Q_XFS_GETQUOTA, h, id, (void *)&xdqblk) < 0) {
		/*
		 * ENOENT means the structure just does not exist - return all
		 * zeros. Otherwise return failure.
		 */
		if (errno != ENOENT) {
			free(dquot);
			return NULL;
		}
	}
	else {
		xfs_kern2utildqblk(&dquot->dq_dqb, &xdqblk);
	}
	return dquot;
}

/*
 *	Write a dqblk struct to the XFS quota manager
 */
static int xfs_commit_dquot(struct dquot *dquot, int flags)
{
	struct quota_handle *h = dquot->dq_h;
	struct xfs_kern_dqblk xdqblk;
	qid_t id = dquot->dq_id;

	if (!XFS_USRQUOTA(h) && !XFS_GRPQUOTA(h) && !XFS_PRJQUOTA(h))
		return 0;

	if (xfs_util2kerndqblk(&xdqblk, &dquot->dq_dqb) < 0)
		return -1;
	if (XFS_USRQUOTA(h))
		xdqblk.d_flags |= XFS_USER_QUOTA;
	else if (XFS_GRPQUOTA(h))
		xdqblk.d_flags |= XFS_GROUP_QUOTA;
	else if (XFS_PRJQUOTA(h))
		xdqblk.d_flags |= XFS_PROJ_QUOTA;
	xdqblk.d_id = id;
	if (strcmp(h->qh_fstype, MNTTYPE_GFS2) == 0) {
		if (flags & COMMIT_LIMITS) /* warn/limit */
			xdqblk.d_fieldmask |= FS_DQ_BSOFT | FS_DQ_BHARD;
		if (flags & COMMIT_USAGE) /* block usage */
			xdqblk.d_fieldmask |= FS_DQ_BCOUNT;
	} else {
		if (flags & COMMIT_LIMITS) /* warn/limit */
			xdqblk.d_fieldmask |= FS_DQ_BSOFT | FS_DQ_BHARD |
						FS_DQ_ISOFT | FS_DQ_IHARD;
		if (flags & COMMIT_TIMES) /* indiv grace period */
			xdqblk.d_fieldmask |= FS_DQ_TIMER_MASK;
	}

	if (quotactl_handle(Q_XFS_SETQLIM, h, id, (void *)&xdqblk) < 0)
		return -1;
	return 0;
}

/*
 *	xfs_scan_dquots helper - processes a single dquot
 */
static int xfs_get_dquot(struct dquot *dq)
{
	struct xfs_kern_dqblk d;
	int ret;

	memset(&d, 0, sizeof(d));
	ret = quotactl_handle(Q_XFS_GETQUOTA, dq->dq_h, dq->dq_id, (void *)&d);

	if (ret < 0) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	xfs_kern2utildqblk(&dq->dq_dqb, &d);
	return 0;
}

static int xfs_kernel_scan_dquots(struct quota_handle *h,
		int (*process_dquot)(struct dquot *dquot, char *dqname))
{
	struct dquot *dquot = get_empty_dquot();
	qid_t id = 0;
	struct xfs_kern_dqblk xdqblk;
	int ret;

	dquot->dq_h = h;
	while (1) {
		ret = quotactl_handle(Q_XGETNEXTQUOTA, h, id, (void *)&xdqblk);
		if (ret < 0)
			break;

		xfs_kern2utildqblk(&dquot->dq_dqb, &xdqblk);
		dquot->dq_id = xdqblk.d_id;
		ret = process_dquot(dquot, NULL);
		if (ret < 0)
			break;
		id = xdqblk.d_id + 1;
		/* id -1 is invalid and the last one... */
		if (id == -1) {
			errno = ENOENT;
			break;
		}
	}
	free(dquot);

	if (errno == ENOENT)
		return 0;
	return ret;
}

/*
 *	Scan all known dquots and call callback on each
 */
static int xfs_scan_dquots(struct quota_handle *h, int (*process_dquot) (struct dquot *dquot, char *dqname))
{
	int ret;
	struct xfs_kern_dqblk xdqblk;

	ret = quotactl_handle(Q_XGETNEXTQUOTA, h, 0, (void *)&xdqblk);
	if (ret < 0 && (errno == ENOSYS || errno == EINVAL)) {
		if (!XFS_USRQUOTA(h) && !XFS_GRPQUOTA(h) && !XFS_PRJQUOTA(h))
			return 0;
		return generic_scan_dquots(h, process_dquot, xfs_get_dquot);
	}
	return xfs_kernel_scan_dquots(h, process_dquot);
}

/*
 *	Report information about XFS quota on given filesystem
 */
static int xfs_report(struct quota_handle *h, int verbose)
{
	u_int16_t sbflags;
	struct xfs_mem_dqinfo *info = &h->qh_info.u.xfs_mdqi;

	if (!verbose)
		return 0;

	/* quotaon/off flags */
	printf(_("*** Status for %s quotas on device %s\n"), type2name(h->qh_type), h->qh_quotadev);

#define XQM_ON(flag) ((info->qs_flags & (flag)) ? _("ON") : _("OFF"))
	if (h->qh_type == USRQUOTA) {
		printf(_("Accounting: %s; Enforcement: %s\n"),
		       XQM_ON(XFS_QUOTA_UDQ_ACCT), XQM_ON(XFS_QUOTA_UDQ_ENFD));
	}
	else if (h->qh_type == GRPQUOTA) {
		printf(_("Accounting: %s; Enforcement: %s\n"),
		       XQM_ON(XFS_QUOTA_GDQ_ACCT), XQM_ON(XFS_QUOTA_GDQ_ENFD));
	}
	else if (h->qh_type == PRJQUOTA) {
		printf(_("Accounting: %s; Enforcement: %s\n"),
		       XQM_ON(XFS_QUOTA_PDQ_ACCT), XQM_ON(XFS_QUOTA_PDQ_ENFD));
	}
#undef XQM_ON

	/*
	 * If this is the root file system, it is possible that quotas are
	 * on ondisk, but not incore. Those flags will be in the HI 8 bits.
	 */
#define XQM_ONDISK(flag) ((sbflags & (flag)) ? _("ON") : _("OFF"))
	if ((sbflags = (info->qs_flags & 0xff00) >> 8) != 0) {
		if (h->qh_type == USRQUOTA) {
			printf(_("Accounting [ondisk]: %s; Enforcement [ondisk]: %s\n"),
			       XQM_ONDISK(XFS_QUOTA_UDQ_ACCT), XQM_ONDISK(XFS_QUOTA_UDQ_ENFD));
		}
		else if (h->qh_type == GRPQUOTA) {
			printf(_("Accounting [ondisk]: %s; Enforcement [ondisk]: %s\n"),
			       XQM_ONDISK(XFS_QUOTA_GDQ_ACCT), XQM_ONDISK(XFS_QUOTA_GDQ_ENFD));
		}
		else if (h->qh_type == PRJQUOTA) {
			printf(_("Accounting [ondisk]: %s; Enforcement [ondisk]: %s\n"),
			       XQM_ONDISK(XFS_QUOTA_PDQ_ACCT), XQM_ONDISK(XFS_QUOTA_PDQ_ENFD));
		}
#undef XQM_ONDISK
	}

	/* user and group quota file status information */
	if (h->qh_type == USRQUOTA) {
		if (info->qs_uquota.qfs_ino == -1 || info->qs_uquota.qfs_ino == 0)
			printf(_("Inode: none\n"));
		else
			printf(_("Inode: #%llu (%llu blocks, %u extents)\n"),
			       (unsigned long long)info->qs_uquota.qfs_ino,
			       (unsigned long long)info->qs_uquota.qfs_nblks,
			       info->qs_uquota.qfs_nextents);
	}
	else if (h->qh_type == GRPQUOTA) {
		if (info->qs_gquota.qfs_ino == -1)
			printf(_("Inode: none\n"));
		else
			printf(_("Inode: #%llu (%llu blocks, %u extents)\n"),
			       (unsigned long long)info->qs_gquota.qfs_ino,
			       (unsigned long long)info->qs_gquota.qfs_nblks,
			       info->qs_gquota.qfs_nextents);
	}
	else if (h->qh_type == PRJQUOTA) {
		/*
		 * FIXME: Older XFS use group files for project quotas, newer
		 * have dedicated file and we should detect that.
		 */
		if (info->qs_gquota.qfs_ino == -1)
			printf(_("Inode: none\n"));
		else
			printf(_("Inode: #%llu (%llu blocks, %u extents)\n"),
			       (unsigned long long)info->qs_gquota.qfs_ino,
			       (unsigned long long)info->qs_gquota.qfs_nblks,
			       info->qs_gquota.qfs_nextents);
	}
	return 0;
}
