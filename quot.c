/*
 * Copyright (c) 1980, 1990 Regents of the University of California.
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc. [SGI]
 * All rights reserved.
 *
 * [Extensions to support XFS are copyright SGI]
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

#ident "$Copyright: (c) 1980, 1990 Regents of the University of California. $"
#ident "$Copyright: (c) 2000, 2001 Silicon Graphics, Inc. $"
#ident "$Copyright: All rights reserved. $"

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <utmp.h>
#include <pwd.h>

#include "pot.h"
#include "quot.h"
#include "common.h"
#include "mntopt.h"
#include "bylabel.h"

#define	TSIZE	500
__uint64_t sizes[TSIZE];
__uint64_t overflow;

static int fflag;
static int cflag;
static int vflag;
static int aflag;
static char *progname;
static time_t now;

static void mounttable(char *);
static char *username(uid_t);
static void report(void);

static void usage(void)
{
	fprintf(stderr, _("Usage: %s [-acfvV] [filesystem...]\n"), progname);
	exit(1);
}

int main(int argc, char **argv)
{
	int c;

	now = time(0);
	progname = basename(argv[0]);

	while ((c = getopt(argc, argv, "acfvV")) != EOF) {
		switch (c) {
		  case 'a':
			  aflag++;
			  break;
		  case 'c':
			  cflag++;
			  break;
		  case 'f':
			  fflag++;
			  break;
		  case 'v':
			  vflag++;
			  break;
		  case 'V':
			  version();
			  exit(0);
		  default:
			  usage();
		}
	}
	if ((aflag && optind != argc) || (!aflag && optind == argc))
		usage();
	if (aflag)
		mounttable(NULL);
	else {
		while (optind < argc)
			mounttable(argv[optind++]);
	}
	return 0;
}

static void mounttable(char *entry)
{
	struct mntent *mntp;
	const char *dev;
	FILE *mtab;
	int doit;

	if ((mtab = setmntent(MOUNTED, "r")) == NULL) {
		fprintf(stderr, _("%s: no " MOUNTED " file\n"), progname);
		exit(1);
	}
	while ((mntp = getmntent(mtab)) != NULL) {
		doit = 0;
		dev = get_device_name(mntp->mnt_fsname);
		if ((entry != NULL) &&
		    (strcmp(entry, mntp->mnt_dir) != 0) && (strcmp(entry, dev) != 0)) {
			free((char *)dev);
			continue;
		}

		/* Currently, only XFS is implemented... */
		if (strcmp(mntp->mnt_type, MNTTYPE_XFS) == 0) {
			checkXFS(dev, mntp->mnt_dir);
			doit = 1;
		}
		/* ...additional filesystems types here. */
		free((char *)dev);

		if (doit)
			report();
		if (entry != NULL) {
			entry = NULL;	/* found, bail out */
			break;
		}
	}
	if (entry != NULL)
		fprintf(stderr, _("%s: cannot locate block device for %s\n"), progname, entry);
	endmntent(mtab);
}

static int qcmp(du_t * p1, du_t * p2)
{
	if (p1->blocks > p2->blocks)
		return -1;
	if (p1->blocks < p2->blocks)
		return 1;
	if (p1->uid > p2->uid)
		return 1;
	else if (p1->uid < p2->uid)
		return -1;
	return 0;
}

static void report(void)
{
	int i;
	du_t *dp;

	if (cflag) {
		__uint64_t t = 0;

		for (i = 0; i < TSIZE - 1; i++)
			if (sizes[i] > 0) {
				t += sizes[i] * i;
				printf(_("%d\t%llu\t%llu\n"), i, sizes[i], t);
			}
		printf(_("%d\t%llu\t%llu\n"), TSIZE - 1, sizes[TSIZE - 1], overflow + t);
		return;
	}
	qsort(du, ndu, sizeof(du[0]), (int (*)(const void *, const void *))qcmp);
	for (dp = du; dp < &du[ndu]; dp++) {
		char *cp;

		if (dp->blocks == 0)
			return;
		printf(_("%8llu    "), dp->blocks);
		if (fflag)
			printf(_("%8llu    "), dp->nfiles);
		if ((cp = username(dp->uid)) != NULL)
			printf(_("%-8.8s"), cp);
		else
			printf(_("#%-7d"), dp->uid);
		if (vflag)
			printf(_("    %8llu    %8llu    %8llu"),
			       dp->blocks30, dp->blocks60, dp->blocks90);
		putchar('\n');
	}
}

static char *username(uid_t uid)
{
	register struct passwd *pw;
	register uidcache_t *ncp;
	static uidcache_t nc[NUID];
	static int entriesleft = NUID;

	/* check cache for name first */
	ncp = &nc[uid & UIDMASK];
	if (ncp->uid == uid && ncp->name[0])
		return ncp->name;
	if (entriesleft) {
		/*
		 * If we haven't gone through the passwd file then
		 * fill the cache while seaching for name.
		 * This lets us run through passwd serially.
		 */
		if (entriesleft == NUID)
			setpwent();
		while (((pw = getpwent()) != NULL) && entriesleft) {
			entriesleft--;
			ncp = &nc[pw->pw_uid & UIDMASK];
			if (ncp->name[0] == '\0' || pw->pw_uid == uid) {
				strncpy(ncp->name, pw->pw_name, UT_NAMESIZE);
				ncp->uid = uid;
			}
			if (pw->pw_uid == uid)
				return ncp->name;
		}
		endpwent();
		entriesleft = 0;
		ncp = &nc[uid & UIDMASK];
	}

	/* Not cached - do it the slow way & insert into cache */
	if ((pw = getpwuid(uid)) == NULL)
		return NULL;
	strncpy(ncp->name, pw->pw_name, UT_NAMESIZE);
	ncp->uid = uid;
	return ncp->name;
}

/*
 *	=== XFS specific code follows ===
 */

static void acctXFS(xfs_bstat_t * p)
{
	register du_t *dp;
	du_t **hp;
	__uint64_t size;

	if ((p->bs_mode & S_IFMT) == 0)
		return;
	size = howmany((p->bs_blocks * p->bs_blksize), 0x400ULL);

	if (cflag) {
		if (!(S_ISDIR(p->bs_mode) || S_ISREG(p->bs_mode)))
			return;
		if (size >= TSIZE) {
			overflow += size;
			size = TSIZE - 1;
		}
		sizes[(int)size]++;
		return;
	}
	hp = &duhash[p->bs_uid % DUHASH];
	for (dp = *hp; dp; dp = dp->next)
		if (dp->uid == p->bs_uid)
			break;
	if (dp == 0) {
		if (ndu >= NDU)
			return;
		dp = &du[ndu++];
		dp->next = *hp;
		*hp = dp;
		dp->uid = p->bs_uid;
		dp->nfiles = 0;
		dp->blocks = 0;
		dp->blocks30 = 0;
		dp->blocks60 = 0;
		dp->blocks90 = 0;
	}
	dp->blocks += size;

	if (now - p->bs_atime.tv_sec > 30 * SEC24HR)
		dp->blocks30 += size;
	if (now - p->bs_atime.tv_sec > 60 * SEC24HR)
		dp->blocks60 += size;
	if (now - p->bs_atime.tv_sec > 90 * SEC24HR)
		dp->blocks90 += size;
	dp->nfiles++;
}

static void checkXFS(const char *file, char *fsdir)
{
	xfs_fsop_bulkreq_t bulkreq;
	__s64 last = 0;
	__s32 count;
	int i;
	int sts;
	int fsfd;
	du_t **dp;
	xfs_bstat_t *buf;

	/*
	 * Initialize tables between checks; because of the qsort
	 * in report() the hash tables must be rebuilt each time.
	 */
	for (sts = 0; sts < TSIZE; sts++)
		sizes[sts] = 0;
	overflow = 0;
	for (dp = duhash; dp < &duhash[DUHASH]; dp++)
		*dp = 0;
	ndu = 0;

	fsfd = open(fsdir, O_RDONLY);
	if (fsfd < 0) {
		fprintf(stderr, _("%s: cannot open %s: %s\n"), progname, fsdir, strerror(errno));
		exit(1);
	}
	printf(_("%s (%s):\n"), file, fsdir);
	sync();

	buf = (xfs_bstat_t *) smalloc(NBSTAT * sizeof(xfs_bstat_t));
	memset(buf, 0, NBSTAT * sizeof(xfs_bstat_t));

	bulkreq.lastip = &last;
	bulkreq.icount = NBSTAT;
	bulkreq.ubuffer = buf;
	bulkreq.ocount = &count;

	while ((sts = ioctl(fsfd, XFS_IOC_FSBULKSTAT, &bulkreq)) == 0) {
		if (count == 0)
			break;
		for (i = 0; i < count; i++)
			acctXFS(&buf[i]);
	}
	if (sts < 0) {
		fprintf(stderr, _("%s: XFS_IOC_FSBULKSTAT ioctl failed: %s\n"),
			progname, strerror(errno));
		exit(1);
	}
	free(buf);
	close(fsfd);
}
