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
#include <pwd.h>
#include <grp.h>

#include "pot.h"
#include "common.h"
#include "quotasys.h"
#include "quotaio.h"

#define PRINTNAMELEN 9	/* Number of characters to be reserved for name on screen */
#define MAX_CACHE_DQUOTS 1024	/* Number of dquots in cache */

#define FL_USER 1
#define FL_GROUP 2
#define FL_VERBOSE 4
#define FL_ALL 8
#define FL_TRUNCNAMES 16
#define FL_SHORTNUMS 32
#define FL_NONAME 64

int flags, fmt = -1;
char **mnt;
int mntcnt;
int cached_dquots;
struct dquot dquot_cache[MAX_CACHE_DQUOTS];
char *progname;

static void usage(void)
{
	errstr(_("Utility for reporting quotas.\nUsage:\n%s [-vugs] [-t|n] [-F quotaformat] (-a | mntpoint)\n"), progname);
	fprintf(stderr, _("Bugs to %s\n"), MY_EMAIL);
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

	while ((ret = getopt(argcnt, argstr, "VavughtsnF:")) != -1) {
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
			case 's':
				flags |= FL_SHORTNUMS;
				break;
			case 'F':
				if ((fmt = name2fmt(optarg)) == QF_ERROR)
					exit(1);
				break;
			case 'n':
				flags |= FL_NONAME;
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
	if (flags & FL_NONAME && flags & FL_TRUNCNAMES) {
		fputs(_("Specified both -n and -t but only one of them can be used.\n"), stderr);
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

static void print(struct dquot *dquot, char *name)
{
	char pname[MAXNAMELEN];
	char time[MAXTIMELEN];
	char numbuf[3][MAXNUMLEN];
	
	struct util_dqblk *entry = &dquot->dq_dqb;

	if (!entry->dqb_curspace && !entry->dqb_curinodes && !(flags & FL_VERBOSE))
		return;
	sstrncpy(pname, name, sizeof(pname));
	if (flags & FL_TRUNCNAMES)
		pname[PRINTNAMELEN] = 0;
	difftime2str(entry->dqb_btime, time);
	space2str(toqb(entry->dqb_curspace), numbuf[0], flags & FL_SHORTNUMS);
	space2str(entry->dqb_bsoftlimit, numbuf[1], flags & FL_SHORTNUMS);
	space2str(entry->dqb_bhardlimit, numbuf[2], flags & FL_SHORTNUMS);
	printf("%-*s %c%c %7s %7s %7s %6s", PRINTNAMELEN, pname,
	       overlim(qb2kb(toqb(entry->dqb_curspace)), qb2kb(entry->dqb_bsoftlimit), qb2kb(entry->dqb_bhardlimit)),
	       overlim(entry->dqb_curinodes, entry->dqb_isoftlimit, entry->dqb_ihardlimit),
	       numbuf[0], numbuf[1], numbuf[2], time);
	difftime2str(entry->dqb_itime, time);
	number2str(entry->dqb_curinodes, numbuf[0], flags & FL_SHORTNUMS);
	number2str(entry->dqb_isoftlimit, numbuf[1], flags & FL_SHORTNUMS);
	number2str(entry->dqb_ihardlimit, numbuf[2], flags & FL_SHORTNUMS);
	printf(" %7s %5s %5s %6s\n", numbuf[0], numbuf[1], numbuf[2], time);
}

/* Print all dquots in the cache */
static void dump_cached_dquots(int type)
{
	int i;
	char namebuf[MAXNAMELEN];

	if (!cached_dquots)
		return;
	if (type == USRQUOTA) {
		struct passwd *pwent;

		setpwent();
		while ((pwent = getpwent())) {
			for (i = 0; i < cached_dquots && pwent->pw_uid != dquot_cache[i].dq_id; i++);
			if (i < cached_dquots) {
				print(dquot_cache+i, pwent->pw_name);
				dquot_cache[i].dq_flags |= DQ_PRINTED;
			}
		}
		endpwent();
	}
	else {
		struct group *grent;

		setgrent();
		while ((grent = getgrent())) {
			for (i = 0; i < cached_dquots && grent->gr_gid != dquot_cache[i].dq_id; i++);
			if (i < cached_dquots) {
				print(dquot_cache+i, grent->gr_name);
				dquot_cache[i].dq_flags |= DQ_PRINTED;
			}
		}
		endgrent();
	}
	for (i = 0; i < cached_dquots; i++)
		if (!(dquot_cache[i].dq_flags & DQ_PRINTED)) {
			sprintf(namebuf, "#%u", dquot_cache[i].dq_id);
			print(dquot_cache+i, namebuf);
		}
	cached_dquots = 0;
}

static int output(struct dquot *dquot, char *name)
{
	if (flags & FL_NONAME) {
		char namebuf[MAXNAMELEN];

		sprintf(namebuf, "#%u", dquot->dq_id);
		print(dquot, namebuf);
	}
	else if (name)
		print(dquot, name);
	else {
		memcpy(dquot_cache+cached_dquots++, dquot, sizeof(struct dquot));
		if (cached_dquots >= MAX_CACHE_DQUOTS)
			dump_cached_dquots(dquot->dq_h->qh_type);
	}
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

	if (h->qh_ops->scan_dquots(h, output) < 0)
		return;
	dump_cached_dquots(type);
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
