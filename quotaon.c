/*
 * Copyright (c) 1980, 1990 Regents of the University of California. All
 * rights reserved.
 * 
 * This code is derived from software contributed to Berkeley by Robert Elz at
 * The University of Melbourne.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution. 3. All advertising
 * materials mentioning features or use of this software must display the
 * following acknowledgement: This product includes software developed by the
 * University of California, Berkeley and its contributors. 4. Neither the
 * name of the University nor the names of its contributors may be used to
 * endorse or promote products derived from this software without specific
 * prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ident "$Copyright: (c) 1980, 1990 Regents of the University of California $"
#ident "$Copyright: All rights reserved. $"
#ident "$Id: quotaon.c,v 1.8 2001/09/04 16:21:58 jkar8572 Exp $"

/*
 * Turn quota on/off for a filesystem.
 */
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>

#include "quotaon.h"

int aflag;			/* all file systems */
int gflag;			/* operate on group quotas */
int uflag;			/* operate on user quotas */
int vflag;			/* verbose */
int kqf;			/* kernel quota format */
char *progname;

static void usage(void)
{
	errstr(_("Usage:\n\t%s [-guv] [-x state] -a\n\t%s [-guv] [-x state] filesys ...\n"), progname, progname);
	exit(1);
}

/*
 *	For both VFS quota formats, need to pass in the quota file;
 *	for XFS quota manager, pass on the -x command line option.
 */
static int newstate(struct mntent *mnt, int offmode, int type, char *extra)
{
	int flags, ret = 0;
	newstate_t *statefunc;
	const char *mnt_fsname = get_device_name(mnt->mnt_fsname);

	if (!mnt_fsname)
		return 1;
	flags = offmode ? STATEFLAG_OFF : STATEFLAG_ON;
	if (vflag > 1)
		flags |= STATEFLAG_VERYVERBOSE;
	else if (vflag)
		flags |= STATEFLAG_VERBOSE;
	if (aflag)
		flags |= STATEFLAG_ALL;

	if (!strcmp(mnt->mnt_type, MNTTYPE_XFS)) {	/* XFS filesystem has special handling... */
		if (!(kqf & (1 << QF_XFS))) {
			errstr("Can't change state of XFS quota. It's not compiled in kernel.\n");
			return 1;
		}
		if (kqf & (1 << QF_XFS) &&
		    ((offmode && (kern_quota_on(mnt_fsname, USRQUOTA, 1 << QF_XFS)
		    || kern_quota_on(mnt_fsname, GRPQUOTA, 1 << QF_XFS)))
		    || (!offmode && kern_quota_on(mnt_fsname, type, 1 << QF_XFS))))
			ret = xfs_newstate(mnt, type, extra, flags);
	}
	else {
		extra = get_qf_name(mnt, type, (kqf & (1 << QF_VFSV0)) ? QF_VFSV0 : QF_VFSOLD);
		statefunc = (kqf & (1 << QF_VFSV0)) ? v2_newstate : v1_newstate;
		ret = statefunc(mnt, type, extra, flags);
		free(extra);
	}
	return ret;
}

int main(int argc, char **argv)
{
	struct mntent *mnt;
	char *xarg = NULL;
	int c, offmode = 0, errs = 0;

	gettexton();

	progname = basename(argv[0]);
	if (strcmp(progname, "quotaoff") == 0)
		offmode++;
	else if (strcmp(progname, "quotaon") != 0)
		die(1, _("Name must be quotaon or quotaoff not %s\n"), progname);

	while ((c = getopt(argc, argv, "afvugx:V")) != -1) {
		switch (c) {
		  case 'a':
			  aflag++;
			  break;
		  case 'f':
			  offmode++;
			  break;
		  case 'g':
			  gflag++;
			  break;
		  case 'u':
			  uflag++;
			  break;
		  case 'v':
			  vflag++;
			  break;
		  case 'x':
			  xarg = optarg;
			  break;
		  case 'V':
			  version();
			  exit(0);
		  default:
			  usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc <= 0 && !aflag)
		usage();
	if (!gflag && !uflag) {
		gflag++;
		uflag++;
	}

	kqf = kern_quota_format();

	if (init_mounts_scan(aflag ? 0 : argc, argv) < 0)
		return 1;
	while ((mnt = get_next_mount())) {
		if (!strcmp(mnt->mnt_type, MNTTYPE_NFS)) {
			if (!aflag)
				fprintf(stderr, "%s: Quota can't be turned on on NFS filesystem\n", mnt->mnt_fsname);
			continue;
		}

		if (gflag)
			errs += newstate(mnt, offmode, GRPQUOTA, xarg);
		if (uflag)
			errs += newstate(mnt, offmode, USRQUOTA, xarg);
	}
	end_mounts_scan();

	return errs;
}

/*
 *	Enable/disable VFS quota on given filesystem
 */
static int quotaonoff(char *quotadev, char *quotadir, char *quotafile, int type, int flags)
{
	int qcmd;

	if (flags & STATEFLAG_OFF) {
		qcmd = QCMD(Q_QUOTAOFF, type);
		if (quotactl(qcmd, quotadev, 0, NULL) < 0) {
			errstr(_("quotactl on %s [%s]: %s\n"), quotadev, quotadir, strerror(errno));
			return 1;
		}
		if (flags & STATEFLAG_VERBOSE)
			printf(_("%s [%s]: %s quotas turned off\n"), quotadev, quotadir, type2name(type));
		return 0;
	}
	qcmd = QCMD(Q_QUOTAON, type);
	if (quotactl(qcmd, quotadev, 0, (void *)quotafile) < 0) {
		errstr(_("using %s on %s [%s]: %s\n"), quotafile, quotadev, quotadir, strerror(errno));
		return 1;
	}
	if (flags & STATEFLAG_VERBOSE)
		printf(_("%s [%s]: %s quotas turned on\n"), quotadev, quotadir, type2name(type));
	return 0;
}

/*
 *	Enable/disable rsquash on given filesystem
 */
static int quotarsquashonoff(const char *quotadev, int type, int flags)
{
#if defined(MNTOPT_RSQUASH)
	int mode = (flags & STATEFLAG_OFF) ? 0 : 1;
	int qcmd = QCMD(Q_V1_RSQUASH, type);

	if (quotactl(qcmd, quotadev, 0, (void *)&mode) < 0) {
		errstr(_("set root_squash on %s: %s\n"), quotadev, strerror(errno));
		return 1;
	}
	if ((flags & STATEFLAG_VERBOSE) && (flags & STATEFLAG_OFF))
		printf(_("%s: %s root_squash turned off\n"), quotadev, type2name(type));
	else if ((flags & STATEFLAG_VERBOSE) && (flags & STATEFLAG_ON))
		printf(_("%s: %s root_squash turned on\n"), quotadev, type2name(type));
#endif
	return 0;
}

/*
 *	Enable/disable quota/rootsquash on given filesystem (version 1)
 */
int v1_newstate(struct mntent *mnt, int type, char *file, int flags)
{
	int errs = 0;
	const char *dev = get_device_name(mnt->mnt_fsname);

	if (!dev)
		return 1;
	if ((flags & STATEFLAG_OFF) && hasmntopt(mnt, MNTOPT_RSQUASH))
		errs += quotarsquashonoff(dev, type, flags);
	if (hasquota(mnt, type))
		errs += quotaonoff((char *)dev, mnt->mnt_dir, file, type, flags);
	if ((flags & STATEFLAG_ON) && hasmntopt(mnt, MNTOPT_RSQUASH))
		errs += quotarsquashonoff(dev, type, flags);
	free((char *)dev);
	return errs;
}

/*
 *	Enable/disable quota on given filesystem (version 2 quota)
 */
int v2_newstate(struct mntent *mnt, int type, char *file, int flags)
{
	const char *dev = get_device_name(mnt->mnt_fsname);
	int errs = 0;

	if (!dev)
		return 1;
	if (hasquota(mnt, type))
		errs = quotaonoff((char *)dev, mnt->mnt_dir, file, type, flags);
	free((char *)dev);
	return errs;
}
