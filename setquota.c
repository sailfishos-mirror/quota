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

int flags, fmt = -1;
char **mnt;
int mntcnt;
qid_t protoid, id;
struct util_dqblk toset;

/* Print usage information */
static void usage(void)
{
#if defined(RPC_SETQUOTA)
	fprintf(stderr, _("Usage:\n"
			  "  setquota [-u|-g] [-r] [-F quotaformat] <user|group>\n"
			  "\t<block-softlimit> <block-hardlimit> <inode-softlimit> <inode-hardlimit> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-r] [-F quotaformat] <-p protouser|protogroup> <user|group> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] -t <blockgrace> <inodegrace> -a|<filesystem>...\n"));
#else
	fprintf(stderr, _("Usage:\n"
			  "  setquota [-u|-g] [-F quotaformat] <user|group>\n"
			  "\t<block-softlimit> <block-hardlimit> <inode-softlimit> <inode-hardlimit> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] <-p protouser|protogroup> <user|group> -a|<filesystem>...\n"
			  "  setquota [-u|-g] [-F quotaformat] -t <blockgrace> <inodegrace> -a|<filesystem>...\n"));
#endif
	fprintf(stderr, _("Bugs to: %s\n"), MY_EMAIL);
	exit(1);
}

/* Convert string to number - print error message in case of failure */
static long parse_num(char *str, char *msg)
{
	char *errch;
	long ret = strtol(str, &errch, 0);

	if (*errch) {
		fprintf(stderr, _("Bad %s: %s\n"), msg, str);
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
	char *opts = "igp:urVF:ta";
#else
	char *opts = "igp:uVF:ta";
#endif

#ifdef RPC_SETQUOTA
	flags |= FL_RPC;
#endif

	while ((ret = getopt(argcnt, argstr, opts)) != EOF) {
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
			  flags &= ~FL_RPC;
			  break;
		  case 'a':
			  flags |= FL_ALL;
			  break;
		  case 't':
			  flags |= FL_GRACE;
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
		fputs(_("Group and user quotas can't be used together.\n"), stderr);
		usage();
	}
	if (flags & FL_PROTO && flags & FL_GRACE) {
		fputs(_("Prototype user has no sense when editting grace times.\n"), stderr);
		usage();
	}
	if (flags & FL_GRACE)
		otherargs = 2;
	else {
		otherargs = 1;
		if (!(flags & FL_PROTO))
			otherargs += 4;
	}
	if (optind + otherargs > argcnt) {
		fputs(_("Bad number of arguments.\n"), stderr);
		usage();
	}
	if (!(flags & (FL_USER | FL_GROUP)))
		flags |= FL_USER;
	if (!(flags & FL_GRACE)) {
		id = name2id(argstr[optind++], flag2type(flags));
		if (!(flags & FL_PROTO)) {
			toset.dqb_bsoftlimit = parse_num(argstr[optind++], _("block softlimit"));
			toset.dqb_bhardlimit = parse_num(argstr[optind++], _("block hardlimit"));
			toset.dqb_isoftlimit = parse_num(argstr[optind++], _("inode softlimit"));
			toset.dqb_ihardlimit = parse_num(argstr[optind++], _("inode hardlimit"));
		}
		else
			protoid = name2id(protoname, flag2type(flags));
	}
	else {
		toset.dqb_btime = parse_num(argstr[optind++], _("block grace time"));
		toset.dqb_itime = parse_num(argstr[optind++], _("inode grace time"));
	}
	if (!(flags & FL_ALL)) {
		mntcnt = argcnt - optind;
		mnt = argstr + optind;
		if (!mntcnt) {
			fputs(_("Mountpoint not specified.\n"), stderr);
			usage();
		}
	}
}

/* Set user limits */
static void setlimits(struct quota_handle **handles)
{
	struct dquot *q, *protoq, *protoprivs = NULL, *curprivs;

	curprivs = getprivs(id, handles);
	if (flags & FL_PROTO) {
		protoprivs = getprivs(protoid, handles);
		for (q = curprivs, protoq = protoprivs; q; q = q->dq_next, protoq = protoq->dq_next) {
			q->dq_dqb.dqb_bsoftlimit = protoq->dq_dqb.dqb_bsoftlimit;
			q->dq_dqb.dqb_bhardlimit = protoq->dq_dqb.dqb_bhardlimit;
			q->dq_dqb.dqb_isoftlimit = protoq->dq_dqb.dqb_isoftlimit;
			q->dq_dqb.dqb_ihardlimit = protoq->dq_dqb.dqb_ihardlimit;
		}
		freeprivs(protoprivs);
	}
	else {
		for (q = curprivs; q; q = q->dq_next) {
			q->dq_dqb.dqb_bsoftlimit = toset.dqb_bsoftlimit;
			q->dq_dqb.dqb_bhardlimit = toset.dqb_bhardlimit;
			q->dq_dqb.dqb_isoftlimit = toset.dqb_isoftlimit;
			q->dq_dqb.dqb_ihardlimit = toset.dqb_ihardlimit;
		}
	}
	putprivs(curprivs);
	freeprivs(curprivs);
}

/* Set grace times */
static void setgraces(struct quota_handle **handles)
{
	int i;

	for (i = 0; handles[i]; i++) {
		handles[i]->qh_info.dqi_bgrace = toset.dqb_btime;
		handles[i]->qh_info.dqi_igrace = toset.dqb_itime;
		mark_quotafile_info_dirty(handles[i]);
	}
}

int main(int argc, char **argv)
{
	struct quota_handle **handles;

	gettexton();
	parse_options(argc, argv);
	warn_new_kernel(fmt);

	if (flags & FL_ALL)
		handles = create_handle_list(0, NULL, flag2type(flags), fmt, !(flags & FL_RPC));
	else
		handles = create_handle_list(mntcnt, mnt, flag2type(flags), fmt, !(flags & FL_RPC));

	if (flags & FL_GRACE)
		setgraces(handles);
	else
		setlimits(handles);
	dispose_handle_list(handles);
	return 0;
}
