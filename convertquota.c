/*
 *
 *	Utility for converting quota file from old to new format
 *
 *	Sponsored by SuSE CR
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <asm/byteorder.h>

#include "pot.h"
#include "common.h"
#include "quotaio.h"
#include "quotasys.h"
#include "quota.h"
#include "bylabel.h"

char *mntpoint;
char *progname;
int ucv, gcv;
struct quota_handle *qn;	/* Handle of new file */

static void usage(void)
{
	errstr(_("Utility for converting quota files.\nUsage:\n\t%s [-u] [-g] mountpoint\n"), progname);
	errstr(_("Bugs to %s\n"), MY_EMAIL);
	exit(1);
}

void parse_options(int argcnt, char **argstr)
{
	int ret;
	char *slash = strrchr(argstr[0], '/'), cmdname[PATH_MAX];

	if (!slash)
		slash = argstr[0];
	else
		slash++;

	sstrncpy(cmdname, slash, sizeof(cmdname));
	while ((ret = getopt(argcnt, argstr, "Vugh:")) != -1) {
		switch (ret) {
			case '?':
			case 'h':
				usage();
			case 'V':
				version();
				exit(0);
			case 'u':
				ucv = 1;
				break;
			case 'g':
				gcv = 1;
				break;
		}
	}

	if (optind + 1 != argcnt) {
		puts(_("Bad number of arguments."));
		usage();
	}

	if (!(ucv | gcv))
		ucv = 1;

	mntpoint = argstr[optind];
}

int convert_dquot(struct dquot *dquot, char *name)
{
	struct dquot newdquot;

	memset(&newdquot, 0, sizeof(newdquot));
	newdquot.dq_id = dquot->dq_id;
	newdquot.dq_h = qn;
	newdquot.dq_dqb.dqb_ihardlimit = dquot->dq_dqb.dqb_ihardlimit;
	newdquot.dq_dqb.dqb_isoftlimit = dquot->dq_dqb.dqb_isoftlimit;
	newdquot.dq_dqb.dqb_curinodes = dquot->dq_dqb.dqb_curinodes;
	newdquot.dq_dqb.dqb_bhardlimit = dquot->dq_dqb.dqb_bhardlimit;
	newdquot.dq_dqb.dqb_bsoftlimit = dquot->dq_dqb.dqb_bsoftlimit;
	newdquot.dq_dqb.dqb_curspace = dquot->dq_dqb.dqb_curspace;
	newdquot.dq_dqb.dqb_btime = dquot->dq_dqb.dqb_btime;
	newdquot.dq_dqb.dqb_itime = dquot->dq_dqb.dqb_itime;
	if (qn->qh_ops->commit_dquot(&newdquot) < 0) {
		errstr(_("Can't commit dquot for id %u (%s): %s\n"),
			(uint)dquot->dq_id, name, strerror(errno));
		return -1;
	}
	return 0;
}

void convert_file(int type)
{
	struct quota_handle *qo;
	char *qfname, namebuf[PATH_MAX];
	FILE *mntf;
	struct mntent *mnt;
	const char *dev;

	if (!(mntf = setmntent(MOUNTED, "r")))
		die(2, _("Can't open %s: %s\n"), MOUNTED, strerror(errno));
	while ((mnt = getmntent(mntf))) {
		if (!(dev = get_device_name(mnt->mnt_fsname)))
			continue;
		if (devcmp(dev, mntpoint) || dircmp(mnt->mnt_dir, mntpoint)) {
			free((void *)dev);
			break;
		}
		free((void *)dev);
	}
	if (!mnt)
		die(1, _("Can't find given mountpoint %s\n"), mntpoint);
	if (!(qo = init_io(mnt, type, QF_VFSOLD, 1))) {
		errstr(_("Can't open old format file for %ss on %s\n"),
			type2name(type), mntpoint);
		return;
	}
	if (!(qn = new_io(mnt, type, QF_VFSV0))) {
		errstr(_("Can't create file for %ss for new format on %s: %s\n"),
			type2name(type), mntpoint, strerror(errno));
		end_io(qo);
		return;
	}
	if (qo->qh_ops->scan_dquots(qo, convert_dquot) >= 0) {	/* Conversion succeeded? */
		qfname = get_qf_name(mnt, type, QF_VFSV0);
		strcpy(namebuf, qfname);
		sstrncat(namebuf, ".new", sizeof(namebuf));
		if (rename(namebuf, qfname) < 0)
			errstr(_("Can't rename new quotafile %s to name %s: %s\n"),
				namebuf, qfname, strerror(errno));
		free(qfname);
	}
	endmntent(mntf);
	end_io(qo);
	end_io(qn);
}

int main(int argc, char **argv)
{
	gettexton();
	progname = basename(argv[0]);

	parse_options(argc, argv);

	if (ucv)
		convert_file(USRQUOTA);

	if (gcv)
		convert_file(GRPQUOTA);

	return 0;
}

