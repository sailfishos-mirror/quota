/*
 *
 *	Utility for reporting quotas
 *
 *	Based on old repquota.
 *	Jan Kara <jack@suse.cz> - Sponsored by SuSE CZ
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "pot.h"
#include "common.h"
#include "quotasys.h"
#include "quotaio.h"

#define PRINTNAMELEN 9	/* Number of characters to be reserved for name on screen */

#define FL_USER 1
#define FL_GROUP 2
#define FL_VERBOSE 4
#define FL_ALL 8
#define FL_TRUNCNAMES 16

int flags, fmt = -1;
char **mnt;
int mntcnt;
char *progname;

static void usage(void)
{
	errstr(_("Utility for reporting quotas.\nUsage:\n%s [-vugt] [-F quotaformat] (-a | mntpoint)\n"), progname);
	errstr(_("Bugs to %s\n"), MY_EMAIL);
	exit(1);
}

static void parse_options(int argcnt, char **argstr)
{
	int ret;
	char *slash = strrchr(argstr[0], '/');

	if (!slash)
		slash = argstr[0];
	else
		slash++;

	while ((ret = getopt(argcnt, argstr, "VavughtF:")) != -1) {
		switch (ret) {
			case '?':
			case 'h':
				usage();
			case 'V':
				version();
				exit(0);
			case 'u':
				flags |= FL_USER;
				break;
			case 'g':
				flags |= FL_GROUP;
				break;
			case 'v':
				flags |= FL_VERBOSE;
				break;
			case 'a':
				flags |= FL_ALL;
				break;
			case 't':
				flags |= FL_TRUNCNAMES;
				break;
			case 'F':
				if ((fmt = name2fmt(optarg)) == QF_ERROR)
					exit(1);
				break;

		}
	}

	if ((flags & FL_ALL && optind != argcnt) || (!(flags & FL_ALL) && optind == argcnt)) {
		fputs(_("Bad number of arguments.\n"), stderr);
		usage();
	}
	if (fmt == QF_RPC) {
		fputs(_("Repquota can't report through RPC calls.\n"), stderr);
		exit(1);
	}
	if (!(flags & (FL_USER | FL_GROUP)))
		flags |= FL_USER;
	if (!(flags & FL_ALL)) {
		mnt = argstr + optind;
		mntcnt = argcnt - optind;
	}
}

static char overlim(uint usage, uint softlim, uint hardlim)
{
	if ((usage > softlim && softlim) || (usage > hardlim && hardlim))
		return '+';
	return '-';
}

static int print(struct dquot *dquot, char *name)
{
	char pname[PRINTNAMELEN+1];
	char time[MAXTIMELEN];
	struct util_dqblk *entry = &dquot->dq_dqb;

	if (!entry->dqb_curspace && !entry->dqb_curinodes && !(flags & FL_VERBOSE))
		return 0;
	sstrncpy(pname, name, sizeof(pname));
	difftime2str(entry->dqb_btime, time);
	printf("%-*s %c%c%8Lu%8Lu%8Lu%7s", PRINTNAMELEN, pname,
	       overlim(qb2kb(toqb(entry->dqb_curspace)), qb2kb(entry->dqb_bsoftlimit),
		       qb2kb(entry->dqb_bhardlimit)), overlim(entry->dqb_curinodes,
							      entry->dqb_isoftlimit,
							      entry->dqb_ihardlimit),
	       (long long)qb2kb(toqb(entry->dqb_curspace)), (long long)qb2kb(entry->dqb_bsoftlimit),
	       (long long)qb2kb(entry->dqb_bhardlimit), time);
	difftime2str(entry->dqb_itime, time);
	printf("%8Lu%6Lu%6Lu%7s\n", (long long)entry->dqb_curinodes,
	       (long long)entry->dqb_isoftlimit, (long long)entry->dqb_ihardlimit, time);
	return 0;
}

static void report_it(struct quota_handle *h, int type)
{
	char bgbuf[MAXTIMELEN], igbuf[MAXTIMELEN];

	printf(_("*** Report for %s quotas on device %s\n"), type2name(type), h->qh_quotadev);
	time2str(h->qh_info.dqi_bgrace, bgbuf, TF_ROUND);
	time2str(h->qh_info.dqi_igrace, igbuf, TF_ROUND);
	printf(_("Block grace time: %s; Inode grace time: %s\n"), bgbuf, igbuf);
	printf(_("                        Block limits                File limits\n"));
	printf(_("%-9s       used    soft    hard  grace    used  soft  hard  grace\n"), (type == USRQUOTA)?_("User"):_("Group"));
	printf("----------------------------------------------------------------------\n");

	if (h->qh_ops->scan_dquots(h, print) < 0)
		return;
	if (h->qh_ops->report) {
		putchar('\n');
		h->qh_ops->report(h, flags & FL_VERBOSE);
		putchar('\n');
	}
}

static void report(int type)
{
	struct quota_handle **handles;
	int i;

	if (flags & FL_ALL)
		handles = create_handle_list(0, NULL, type, fmt, IOI_LOCALONLY | IOI_READONLY | IOI_OPENFILE);
	else
		handles = create_handle_list(mntcnt, mnt, type, fmt, IOI_LOCALONLY | IOI_READONLY | IOI_OPENFILE);
	for (i = 0; handles[i]; i++)
		report_it(handles[i], type);
	dispose_handle_list(handles);
}

int main(int argc, char **argv)
{
	gettexton();
	progname = basename(argv[0]);

	parse_options(argc, argv);
	warn_new_kernel(fmt);

	if (flags & FL_USER)
		report(USRQUOTA);

	if (flags & FL_GROUP)
		report(GRPQUOTA);

	return 0;
}
