/*
 *
 *	Generic IO operations on quotafiles
 *
 *	Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "pot.h"
#include "bylabel.h"
#include "common.h"
#include "quotasys.h"
#include "quotaio.h"

#include "dqblk_v1.h"
#include "dqblk_v2.h"
#include "dqblk_rpc.h"
#include "dqblk_xfs.h"

static int file_magics[] = INITQMAGICS;
static int known_versions[] = INITKNOWNVERSIONS;

/* Header in all newer quotafiles */
struct disk_dqheader {
	u_int32_t dqh_magic;
	u_int32_t dqh_version;
} __attribute__ ((packed));

/*
 *	Detect quotafile format
 */
int detect_qf_format(int fd, int type)
{
	struct disk_dqheader head;
	int ret;

	if ((ret = read(fd, &head, sizeof(head))) < 0)
		die(2, _("Error while reading from quotafile: %s\n"), strerror(errno));
	if (ret != sizeof(head) || head.dqh_magic != file_magics[type])	/* Short file? Probably old format */
		return QF_VFSOLD;
	if (head.dqh_version > known_versions[type])	/* Too new format? */
		return QF_TOONEW;
	return QF_VFSV0;
}

/*
 *	Detect quota format and initialize quota IO
 */
struct quota_handle *init_io(struct mntent *mnt, int type, int fmt, int flags)
{
	char *qfname = NULL;
	int fd = -1, kernfmt;
	struct quota_handle *h = smalloc(sizeof(struct quota_handle));
	const char *mnt_fsname = NULL;

	if (!hasquota(mnt, type))
		goto out_handle;
	if (!(mnt_fsname = get_device_name(mnt->mnt_fsname)))
		goto out_handle;
	if (stat(mnt_fsname, &h->qh_stat) < 0)
		memset(&h->qh_stat, 0, sizeof(struct stat));
	h->qh_io_flags = 0;
	if (flags & IOI_READONLY)
		h->qh_io_flags |= IOFL_RO;
	h->qh_type = type;
	sstrncpy(h->qh_quotadev, mnt_fsname, sizeof(h->qh_quotadev));
	free((char *)mnt_fsname);
	if (!strcmp(mnt->mnt_type, MNTTYPE_NFS)) {	/* NFS filesystem? */
		if (fmt != -1 && fmt != QF_RPC) {	/* User wanted some other format? */
			errstr(_("Only RPC quota format is allowed on NFS filesystem.\n"));
			goto out_handle;
		}
#ifdef RPC
		h->qh_fd = -1;
		h->qh_fmt = QF_RPC;
		h->qh_ops = &quotafile_ops_rpc;
		return h;
#else
		errstr(_("RPC quota format not compiled.\n"));
		goto out_handle;
#endif
	}

	if (!strcmp(mnt->mnt_type, MNTTYPE_XFS)) {	/* XFS filesystem? */
		if (fmt != -1 && fmt != QF_XFS) {	/* User wanted some other format? */
			errstr(_("Only XFS quota format is allowed on XFS filesystem.\n"));
			goto out_handle;
		}
		h->qh_fd = -1;
		h->qh_fmt = QF_XFS;
		h->qh_ops = &quotafile_ops_xfs;
		memset(&h->qh_info, 0, sizeof(h->qh_info));
		h->qh_ops->init_io(h);
		return h;
	}
	kernfmt = kern_quota_format();	/* Check kernel quota format */
	if (kernfmt > 0 && (fmt == -1 || (1 << fmt) & kernfmt) &&	/* Quota compiled and desired format available? */
	    /* Quota turned on? */
	    (kernfmt = kern_quota_on(h->qh_quotadev, type, fmt == -1 ? kernfmt : (1 << fmt))) != -1) {
		h->qh_io_flags |= IOFL_QUOTAON;
		fmt = kernfmt;	/* Default is kernel used format */
	}
	if (!(qfname = get_qf_name(mnt, type, fmt))) {
		errstr(_("Can't get quotafile name.\n"));
		goto out_handle;
	}
	if (qfname[0] && (!QIO_ENABLED(h) || flags & IOI_OPENFILE)) {	/* Need to open file? */
		/* We still need to open file for operations like 'repquota' */
		if ((fd = open(qfname, QIO_RO(h) ? O_RDONLY : O_RDWR)) < 0) {
			errstr(_("Can't open quotafile %s: %s\n"),
				qfname, strerror(errno));
			goto out_handle;
		}
		flock(fd, LOCK_EX);
		/* Init handle */
		h->qh_fd = fd;

		/* Check file format */
		h->qh_fmt = detect_qf_format(fd, type);
		if (h->qh_fmt == -2) {
			errstr(_("Quotafile format too new in %s\n"),
				qfname);
			goto out_lock;
		}
		if (fmt != -1 && h->qh_fmt != fmt) {
			errstr(_("Quotafile format detected differs from the specified one (or the one kernel uses on the file).\n"));
			goto out_handle;
		}
	}
	else {
		h->qh_fd = -1;
		h->qh_fmt = fmt;
	}

	if (h->qh_fmt == QF_VFSOLD)
		h->qh_ops = &quotafile_ops_1;
	else if (h->qh_fmt == QF_VFSV0)
		h->qh_ops = &quotafile_ops_2;
	memset(&h->qh_info, 0, sizeof(h->qh_info));

	if (h->qh_ops->init_io && h->qh_ops->init_io(h) < 0) {
		errstr(_("Can't initialize quota on %s: %s\n"), h->qh_quotadev, strerror(errno));
		goto out_lock;
	}
	return h;
      out_lock:
	if (fd != -1)
		flock(fd, LOCK_UN);
      out_handle:
	if (qfname)
		free(qfname);
	free(h);
	return NULL;
}

/*
 *	Create new quotafile of specified format on given filesystem
 */
struct quota_handle *new_io(struct mntent *mnt, int type, int fmt)
{
	char *qfname;
	int fd;
	struct quota_handle *h;
	const char *mnt_fsname;
	char namebuf[PATH_MAX];

	if (fmt == -1)
		fmt = QF_VFSV0;	/* Use the newest format */
	else if (fmt == QF_RPC || fmt == QF_XFS) {
		errstr(_("Creation of %s quota format is not supported.\n"),
			fmt == QF_RPC ? "RPC" : "XFS");
		return NULL;
	}
	if (!hasquota(mnt, type) || !(qfname = get_qf_name(mnt, type, fmt)))
		return NULL;
	sstrncpy(namebuf, qfname, PATH_MAX);
	sstrncat(namebuf, ".new", PATH_MAX);
	free(qfname);
	if ((fd = open(namebuf, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) < 0) {
		errstr(_("Can't create new quotafile %s: %s\n"),
			namebuf, strerror(errno));
		return NULL;
	}
	if (!(mnt_fsname = get_device_name(mnt->mnt_fsname)))
		goto out_fd;
	h = smalloc(sizeof(struct quota_handle));

	h->qh_fd = fd;
	h->qh_io_flags = 0;
	sstrncpy(h->qh_quotadev, mnt_fsname, sizeof(h->qh_quotadev));
	free((char *)mnt_fsname);
	h->qh_type = type;
	memset(&h->qh_info, 0, sizeof(h->qh_info));
	if (fmt == QF_VFSOLD)
		h->qh_ops = &quotafile_ops_1;
	else
		h->qh_ops = &quotafile_ops_2;

	flock(fd, LOCK_EX);
	if (h->qh_ops->new_io && h->qh_ops->new_io(h) < 0) {
		flock(fd, LOCK_UN);
		free(h);
		goto out_fd;
	}
	return h;
      out_fd:
	close(fd);
	return NULL;
}

/*
 *	Close quotafile and release handle
 */
int end_io(struct quota_handle *h)
{
	if (h->qh_io_flags & IOFL_INFODIRTY) {
		if (h->qh_ops->write_info && h->qh_ops->write_info(h) < 0)
			return -1;
		h->qh_io_flags &= ~IOFL_INFODIRTY;
	}
	if (h->qh_ops->end_io && h->qh_ops->end_io(h) < 0)
		return -1;
	flock(h->qh_fd, LOCK_UN);
	close(h->qh_fd);
	free(h);
	return 0;
}

/*
 *	Create empty quota structure
 */
struct dquot *get_empty_dquot(void)
{
	struct dquot *dquot = smalloc(sizeof(struct dquot));

	memset(dquot, 0, sizeof(*dquot));
	dquot->dq_id = -1;
	return dquot;
}
