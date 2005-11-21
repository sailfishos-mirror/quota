/*
 *
 *	Set disk quota from command line 
 *
 *	Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 */
#include <rpc/rpc.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#if defined(RPC)
#include "rquota.h"
#include "rquota_client.h"
#endif
#include "pot.h"
#include "quotaops.h"
#include "common.h"
#include "quotasys.h"

#define FL_USER 1
#define FL_GROUP 2
#define FL_RPC 4
#define FL_ALL 8
#define FL_PROTO 16
#define FL_GRACE 32
#define FL_INDIVIDUAL_GRACE 64
#define FL_BATCH 128

int flags, fmt = -1;
char **mnt;
char *progname;
int mntcnt;
qid_t protoid, id;
struct util_dqblk toset;

/* Print usage information */
static void usage(void)
{
#if defined(RPC_SETQUOTA)
	errstr(_("Usage:\n"
			  "  setquota [-u|-g] [-r] [-F quotaformat] <user|group>\n"
			  "\t<block-softlimit> <block-hardlimit> <inode-softlimit> <inode-hardlimit> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-r] [-F quotaformat] <-p protouser|protogroup> <user|group> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-r] [-F quotaformat] -b -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] -t <blockgrace> <inodegrace> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] <user|group> -T <blockgrace> <inodegrace> -a|<filesystem>...\n"));
#else
	errstr(_("Usage:\n"
			  "  setquota [-u|-g] [-F quotaformat] <user|group>\n"
			  "\t<block-softlimit> <block-hardlimit> <inode-softlimit> <inode-hardlimit> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] <-p protouser|protogroup> <user|group> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] -b -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] -t <blockgrace> <inodegrace> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] <user|group> -T <blockgrace> <inodegrace> -a|<filesystem>...\n"));
#endif
	fprintf(stderr, _("Bugs to: %s\n"), MY_EMAIL);
	exit(1);
}

/* Convert string to number - print errstr message in case of failure */
static long parse_num(char *str, char *msg)
{
	char *errch;
	long ret = strtol(str, &errch, 0);

	if (*errch) {
		errstr(_("Bad %s: %s\n"), msg, str);
		usage();
	}
	return ret;
}

/* Convert our flags to quota type */
static inline int flag2type(int flags)
{
	if (flags & FL_USER)
		return USRQUOTA;
	if (flags & FL_GROUP)
		return GRPQUOTA;
	return -1;
}

/* Parse options of setquota */
static void parse_options(int argcnt, char **argstr)
{
	int ret, otherargs;
	char *protoname = NULL;

#ifdef RPC_SETQUOTA
	char *opts = "igp:urVF:taTb";
#else
	char *opts = "igp:uVF:taTb";
#endif

	while ((ret = getopt(argcnt, argstr, opts)) != -1) {
		switch (ret) {
		  case '?':
		  case 'h':
			  usage();
		  case 'g':
			  flags |= FL_GROUP;
			  break;
		  case 'u':
			  flags |= FL_USER;
			  break;
		  case 'p':
			  flags |= FL_PROTO;
			  protoname = optarg;
			  break;
		  case 'r':
			  flags |= FL_RPC;
			  break;
		  case 'a':
			  flags |= FL_ALL;
			  break;
		  case 't':
			  flags |= FL_GRACE;
			  break;
		  case 'b':
			  flags |= FL_BATCH;
			  break;
		  case 'T':
			  flags |= FL_INDIVIDUAL_GRACE;
			  break;
		  case 'F':
			  if ((fmt = name2fmt(optarg)) == QF_ERROR)
				  exit(1);
			  break;
		  case 'V':
			  version();
			  break;
		}
	}
	if (flags & FL_USER && flags & FL_GROUP) {
		errstr(_("Group and user quotas cannot be used together.\n"));
		usage();
	}
	if (flags & FL_PROTO && flags & FL_GRACE) {
		errstr(_("Prototype user has no sense when editting grace times.\n"));
		usage();
	}
	if (flags & FL_INDIVIDUAL_GRACE && flags & FL_GRACE) {
		errstr(_("Cannot set both individual and global grace time.\n"));
		usage();
	}
	if (flags & FL_BATCH && flags & (FL_GRACE | FL_INDIVIDUAL_GRACE)) {
		errstr(_("Batch mode cannot be used for setting grace times.\n"));
		usage();
	}
	if (flags & FL_BATCH && flags & FL_PROTO) {
		errstr(_("Batch mode and prototype user cannot be used together.\n"));
		usage();
	}
	if (flags & FL_GRACE)
		otherargs = 2;
	else if (flags & FL_INDIVIDUAL_GRACE)
		otherargs = 3;
	else if (flags & FL_BATCH)
		otherargs = 0;
	else {
		otherargs = 1;
		if (!(flags & FL_PROTO))
			otherargs += 4;
	}
	if (optind + otherargs > argcnt) {
		errstr(_("Bad number of arguments.\n"));
		usage();
	}
	if (!(flags & (FL_USER | FL_GROUP)))
		flags |= FL_USER;
	if (!(flags & (FL_GRACE | FL_BATCH))) {
		id = name2id(argstr[optind++], flag2type(flags), NULL);
		if (!(flags & (FL_GRACE | FL_INDIVIDUAL_GRACE | FL_PROTO))) {
			toset.dqb_bsoftlimit = parse_num(argstr[optind++], _("block softlimit"));
			toset.dqb_bhardlimit = parse_num(argstr[optind++], _("block hardlimit"));
			toset.dqb_isoftlimit = parse_num(argstr[optind++], _("inode softlimit"));
			toset.dqb_ihardlimit = parse_num(argstr[optind++], _("inode hardlimit"));
		}
		else if (flags & FL_PROTO)
			protoid = name2id(protoname, flag2type(flags), NULL);
	}
	if (flags & FL_GRACE) {
		toset.dqb_btime = parse_num(argstr[optind++], _("block grace time"));
		toset.dqb_itime = parse_num(argstr[optind++], _("inode grace time"));
	}
	else if (flags & FL_INDIVIDUAL_GRACE) {
		time_t now;

		time(&now);
		if (!strcmp(argstr[optind], _("unset"))) {
			toset.dqb_btime = 0;
			optind++;
		}
		else
			toset.dqb_btime = now + parse_num(argstr[optind++], _("block grace time"));
		if (!strcmp(argstr[optind], _("unset"))) {
			toset.dqb_itime = 0;
			optind++;
		}
		else
			toset.dqb_itime = now + parse_num(argstr[optind++], _("inode grace time"));
	}
	if (!(flags & FL_ALL)) {
		mntcnt = argcnt - optind;
		mnt = argstr + optind;
		if (!mntcnt) {
			errstr(_("Mountpoint not specified.\n"));
			usage();
		}
	}
}

/* Set user limits */
static int setlimits(struct quota_handle **handles)
{
	struct dquot *q, *protoq, *protoprivs = NULL, *curprivs;
	int ret = 0;

	curprivs = getprivs(id, handles, 0);
	if (flags & FL_PROTO) {
		protoprivs = getprivs(protoid, handles, 0);
		for (q = curprivs, protoq = protoprivs; q && protoq; q = q->dq_next, protoq = protoq->dq_next) {
			q->dq_dqb.dqb_bsoftlimit = protoq->dq_dqb.dqb_bsoftlimit;
			q->dq_dqb.dqb_bhardlimit = protoq->dq_dqb.dqb_bhardlimit;
			q->dq_dqb.dqb_isoftlimit = protoq->dq_dqb.dqb_isoftlimit;
			q->dq_dqb.dqb_ihardlimit = protoq->dq_dqb.dqb_ihardlimit;
			update_grace_times(q);
		}
		freeprivs(protoprivs);
	}
	else {
		for (q = curprivs; q; q = q->dq_next) {
			q->dq_dqb.dqb_bsoftlimit = toset.dqb_bsoftlimit;
			q->dq_dqb.dqb_bhardlimit = toset.dqb_bhardlimit;
			q->dq_dqb.dqb_isoftlimit = toset.dqb_isoftlimit;
			q->dq_dqb.dqb_ihardlimit = toset.dqb_ihardlimit;
			update_grace_times(q);
		}
	}
	if (putprivs(curprivs, COMMIT_LIMITS) == -1)
		ret = -1;
	freeprivs(curprivs);
	return ret;
}

#define MAXLINELEN (MAXNUMLEN*4+MAXNAMELEN+16)

/* Read & parse one batch entry */
static int read_entry(qid_t *id, qsize_t *isoftlimit, qsize_t *ihardlimit, qsize_t *bsoftlimit, qsize_t *bhardlimit)
{
	static int line = 0;
	char name[MAXNAMELEN+1];
	unsigned long is, ih, bs, bh;
	int ret;

	do {
		line++;
		ret = scanf("%s %lu %lu %lu %lu", name, &bs, &bh, &is, &ih);
		if (ret == -1)
			return -1;
		if (ret != 5)
			die(1, _("Cannot parse input line %d.\n"), line);
		ret = 0;
		*id = name2id(name, flag2type(flags), &ret);
		if (ret)
			errstr(_("Unable to get name '%s'.\n"), name);
	} while (ret);
	*isoftlimit = is;
	*ihardlimit = ih;
	*bsoftlimit = bs;
	*bhardlimit = bh;
	return 0;
}

/* Set user limits in batch mode */
static int batch_setlimits(struct quota_handle **handles)
{
	struct dquot *curprivs, *q;
	qsize_t bhardlimit, bsoftlimit, ihardlimit, isoftlimit;
	qid_t id;
	int ret = 0;

	while (!read_entry(&id, &isoftlimit, &ihardlimit, &bsoftlimit, &bhardlimit)) {
		curprivs = getprivs(id, handles, 0);
		for (q = curprivs; q; q = q->dq_next) {
			q->dq_dqb.dqb_bsoftlimit = bsoftlimit;
			q->dq_dqb.dqb_bhardlimit = bhardlimit;
			q->dq_dqb.dqb_isoftlimit = isoftlimit;
			q->dq_dqb.dqb_ihardlimit = ihardlimit;
			update_grace_times(q);
		}
		if (putprivs(curprivs, COMMIT_LIMITS) == -1)
			ret = -1;
		freeprivs(curprivs);
	}
	return ret;
}

/* Set grace times */
static int setgraces(struct quota_handle **handles)
{
	int i;

	for (i = 0; handles[i]; i++) {
		handles[i]->qh_info.dqi_bgrace = toset.dqb_btime;
		handles[i]->qh_info.dqi_igrace = toset.dqb_itime;
		mark_quotafile_info_dirty(handles[i]);
	}
	return 0;
}

/* Set grace times for individual user */
static int setindivgraces(struct quota_handle **handles)
{
	int ret = 0;
	struct dquot *q, *curprivs;

	curprivs = getprivs(id, handles, 0);
	for (q = curprivs; q; q = q->dq_next) {
		q->dq_dqb.dqb_btime = toset.dqb_btime;
		q->dq_dqb.dqb_itime = toset.dqb_itime;
	}
	if (putprivs(curprivs, COMMIT_TIMES) == -1) {
		errstr(_("cannot write times for %s. Maybe kernel does not support such operation?\n"), type2name(flags & FL_USER ? USRQUOTA : GRPQUOTA));
		ret = -1;
	}
	freeprivs(curprivs);
	return ret;
}

int main(int argc, char **argv)
{
	struct quota_handle **handles;
	int ret;

	gettexton();
	progname = basename(argv[0]);

	parse_options(argc, argv);
	init_kernel_interface();

	if (flags & FL_ALL)
		handles = create_handle_list(0, NULL, flag2type(flags), fmt, 0, (flags & FL_RPC) ? 0 : MS_LOCALONLY);
	else
		handles = create_handle_list(mntcnt, mnt, flag2type(flags), fmt, 0, (flags & FL_RPC) ? 0 : MS_LOCALONLY);

	if (flags & FL_GRACE)
		ret = setgraces(handles);
	else if (flags & FL_INDIVIDUAL_GRACE)
		ret = setindivgraces(handles);
	else if (flags & FL_BATCH)
		ret = batch_setlimits(handles);
	else
		ret = setlimits(handles);

	if (dispose_handle_list(handles) == -1)
		ret = -1;

	return ret ? 1 : 0;
}
