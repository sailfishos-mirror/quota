/*
 *	State changes for the XFS Quota Manager.
 */

#ident "Copyright (c) 2001 Silicon Graphics, Inc."

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "quotaon.h"
#include "dqblk_xfs.h"

#define QOFF	1
#define ACCT	2
#define ENFD	3

/*
 *	Ensure we don't attempt to go into a dodgey state.
 */

static int xfs_state_check(int qcmd, int type, int flags, char *dev, int root, int *xopts)
{
	struct xfs_mem_dqinfo info;
	int state;

	/* we never want to operate via -a in XFS quota */
	if (flags & STATEFLAG_ALL)
		return 0;	/* noop */

	if (quotactl(QCMD(Q_XFS_GETQSTAT, 0), dev, 0, (void *)&info) < 0) {
		fprintf(stderr, flags & STATEFLAG_ON ? "quotaon: " : "quotaoff: ");
		perror(dev);
		return -1;
	}

	/* establish current state before any transition */
	state = QOFF;
	if (type == USRQUOTA) {
		if (info.qs_flags & XFS_QUOTA_UDQ_ACCT)
			state = ACCT;
		if (info.qs_flags & XFS_QUOTA_UDQ_ENFD)
			state = ENFD;
	}
	else {			/* GRPQUOTA */
		if (info.qs_flags & XFS_QUOTA_GDQ_ACCT)
			state = ACCT;
		if (info.qs_flags & XFS_QUOTA_GDQ_ENFD)
			state = ENFD;
	}

	switch (state) {
	  case QOFF:
		  switch (qcmd) {
		    case Q_XFS_QUOTARM:
			    return 1;
		    case Q_XFS_QUOTAON:
			    if (root) {
				    *xopts |= (type == USRQUOTA) ?
					    XFS_QUOTA_UDQ_ACCT : XFS_QUOTA_GDQ_ACCT;
				    printf(_("Enabling %s quota on root filesystem"
					     " (reboot to take effect)\n"), type2name(type));
				    return 1;
			    }
			    fprintf(stderr, _("Enable XFS %s quota during mount\n"),
				    type2name(type));
			    return -1;
		    case Q_XFS_QUOTAOFF:
			    return 0;	/* noop */
		  }
		  break;
	  case ACCT:
		  switch (qcmd) {
		    case Q_XFS_QUOTARM:
			    fprintf(stderr, _("Cannot delete %s quota on %s - "
					      "switch quota accounting off first\n"),
				    type2name(type), dev);
			    return -1;
		    case Q_XFS_QUOTAON:
			    if (root) {
				    *xopts |= (type == USRQUOTA) ?
					    XFS_QUOTA_UDQ_ACCT : XFS_QUOTA_GDQ_ACCT;
				    printf(_("Enabling %s quota on root filesystem"
					     " (reboot to take effect)\n"), type2name(type));
				    return 1;
			    }
			    printf(_("Enabling %s quota accounting on %s\n"), type2name(type), dev);
			    *xopts |= (type == USRQUOTA) ? XFS_QUOTA_UDQ_ACCT : XFS_QUOTA_GDQ_ACCT;
			    return 1;
		    case Q_XFS_QUOTAOFF:
			    printf(_("Disabling %s quota accounting on %s\n"),
				   type2name(type), dev);
			    *xopts |= (type == USRQUOTA) ? XFS_QUOTA_UDQ_ACCT : XFS_QUOTA_GDQ_ACCT;
			    return 1;
		  }
		  break;

	  case ENFD:
		  switch (qcmd) {
		    case Q_XFS_QUOTARM:
			    fprintf(stderr, _("Cannot delete %s quota on %s - "
					      "switch quota enforcement off first\n"),
				    type2name(type), dev);
			    return -1;
		    case Q_XFS_QUOTAON:
			    fprintf(stderr, _("Enforcing %s quota already on %s\n"),
				    type2name(type), dev);
			    return -1;
		    case Q_XFS_QUOTAOFF:
			    printf(_("Disabling %s quota enforcement on %s\n"),
				   type2name(type), dev);
			    *xopts |= (type == USRQUOTA) ? XFS_QUOTA_UDQ_ENFD : XFS_QUOTA_GDQ_ENFD;
			    return 1;
		  }
		  break;
	}
	fprintf(stderr, _("Unexpected XFS quota state sought on %s\n"), dev);
	return -1;
}

static int xfs_onoff(char *dev, int type, int flags, int rootfs, int *xopts)
{
	int qoff, qcmd;

	qoff = (flags & STATEFLAG_OFF);
	qcmd = qoff ? Q_XFS_QUOTAOFF : Q_XFS_QUOTAON;
	if (xfs_state_check(qcmd, type, flags, dev, rootfs, xopts) < 0)
		return 1;

	if (quotactl(QCMD(qcmd, type), dev, 0, (void *)xopts) < 0) {
		fprintf(stderr, qoff ? "quotaoff: " : "quotaon: ");
		perror(dev);
		return 1;
	}
	if ((flags & STATEFLAG_VERBOSE) && qoff)
		printf(_("%s: %s quotas turned off\n"), dev, type2name(type));
	else if ((flags & STATEFLAG_VERBOSE) && !qoff)
		printf(_("%s: %s quotas turned on\n"), dev, type2name(type));
	return 0;
}

static int xfs_delete(char *dev, int type, int flags, int rootfs, int *xopts)
{
	int qcmd, check;

	qcmd = Q_XFS_QUOTARM;
	check = xfs_state_check(qcmd, type, flags, dev, rootfs, xopts);
	if (check != 1)
		return (check < 0);

	if (quotactl(QCMD(qcmd, type), dev, 0, (void *)xopts) < 0) {
		fprintf(stderr, _("Failed to delete quota: "));
		perror(dev);
		return 1;
	}

	if (flags & STATEFLAG_VERBOSE)
		printf(_("%s: deleted %s quota blocks\n"), dev, type2name(type));
	return 0;
}

/*
 *	Change state for given filesystem - on/off, acct/enfd, & delete.
 *	Must consider existing state and also whether or not this is the
 *	root filesystem.
 *	We are passed in the new requested state through "type" & "xarg".
 */
int xfs_newstate(struct mntent *mnt, int type, char *xarg, int flags)
{
	int err = 1;
	int xopts = 0;
	int rootfs = !strcmp(mnt->mnt_dir, "/");
	const char *dev = get_device_name(mnt->mnt_fsname);

	if (!dev)
		return err;

	if (xarg == NULL) {	/* both acct & enfd on/off */
		xopts |= (type == USRQUOTA) ?
			(XFS_QUOTA_UDQ_ACCT | XFS_QUOTA_UDQ_ENFD) :
			(XFS_QUOTA_GDQ_ACCT | XFS_QUOTA_GDQ_ENFD);
		err = xfs_onoff((char *)dev, type, flags, rootfs, &xopts);
	}
	else if (strcmp(xarg, "account") == 0) {
		/* only useful if we want root accounting only */
		if (!rootfs || !(flags & STATEFLAG_ON))
			goto done;
		xopts |= (type == USRQUOTA) ? XFS_QUOTA_UDQ_ACCT : XFS_QUOTA_GDQ_ACCT;
		err = xfs_onoff((char *)dev, type, flags, rootfs, &xopts);
	}
	else if (strcmp(xarg, "enforce") == 0) {
		xopts |= (type == USRQUOTA) ? XFS_QUOTA_UDQ_ENFD : XFS_QUOTA_GDQ_ENFD;
		err = xfs_onoff((char *)dev, type, flags, rootfs, &xopts);
	}
	else if (strcmp(xarg, "delete") == 0) {
		xopts |= (type == USRQUOTA) ? XFS_USER_QUOTA : XFS_GROUP_QUOTA;
		err = xfs_delete((char *)dev, type, flags, rootfs, &xopts);
	}
	else
		die(1, _("Invalid argument \"%s\"\n"), xarg);
      done:
	free((char *)dev);
	return err;
}
