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

#ident "$Copyright: (c) 1980, 1990 Regents of the University of California. $"
#ident "$Copyright: All rights reserved. $"
#ident "$Id: quota.c,v 1.14 2004/04/14 16:03:14 jkar8572 Exp $"

/*
 * Disk quota reporting program.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <getopt.h>
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#ifdef RPC
#include <rpc/rpc.h>
#include "rquota.h"
#endif

#include "quota.h"
#include "quotaops.h"
#include "quotasys.h"
#include "pot.h"
#include "common.h"

#define FL_QUIET 1
#define FL_VERBOSE 2
#define FL_USER 4
#define FL_GROUP 8
#define FL_SMARTSIZE 16
#define FL_LOCALONLY 32
#define FL_QUIETREFUSE 64
#define FL_NOAUTOFS 128

int flags, fmt = -1;
char *progname;

void usage(void);
int showquotas(int type, qid_t id);
void heading(int type, qid_t id, char *name, char *tag);

int main(int argc, char **argv)
{
	int ngroups;
	gid_t gidset[NGROUPS];
	int i, ret;

	gettexton();
	progname = basename(argv[0]);

	while ((ret = getopt(argc, argv, "guqvsVliQF:")) != -1) {
		switch (ret) {
		  case 'g':
			  flags |= FL_GROUP;
			  break;
		  case 'u':
			  flags |= FL_USER;
			  break;
		  case 'q':
			  flags |= FL_QUIET;
			  break;
		  case 'v':
			  flags |= FL_VERBOSE;
			  break;
		  case 'F':
			  if ((fmt = name2fmt(optarg)) == QF_ERROR)	/* Error? */
				  exit(1);
			  break;
		  case 's':
			  flags |= FL_SMARTSIZE;
			  break;
		  case 'l':
			  flags |= FL_LOCALONLY;
			  break;
		  case 'Q':
			  flags |= FL_QUIETREFUSE;
			  break;
		  case 'i':
			  flags |= FL_NOAUTOFS;
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

	if (!(flags & FL_USER) && !(flags & FL_GROUP))
		flags |= FL_USER;
	init_kernel_interface();

	ret = 0;
	if (argc == 0) {
		if (flags & FL_USER)
			ret |= showquotas(USRQUOTA, getuid());
		if (flags & FL_GROUP) {
			ngroups = getgroups(NGROUPS, gidset);
			if (ngroups < 0)
				die(1, _("quota: getgroups(): %s\n"), strerror(errno));
			for (i = 0; i < ngroups; i++)
				ret |= showquotas(GRPQUOTA, gidset[i]);
		}
		exit(ret);
	}

	if ((flags & FL_USER) && (flags & FL_GROUP))
		usage();

	if (flags & FL_USER)
		for (; argc > 0; argc--, argv++)
			ret |= showquotas(USRQUOTA, user2uid(*argv, NULL));
	else if (flags & FL_GROUP)
		for (; argc > 0; argc--, argv++)
			ret |= showquotas(GRPQUOTA, group2gid(*argv, NULL));
	return ret;
}

void usage(void)
{
	errstr( "%s%s%s",
		_("Usage: quota [-guqvs] [-l | -Q] [-i] [-F quotaformat]\n"),
		_("\tquota [-qvs] [-l | -Q] [-i] [-F quotaformat] -u username ...\n"),
		_("\tquota [-qvs] [-l | -Q] [-i] [-F quotaformat] -g groupname ...\n"));
	fprintf(stderr, _("Bugs to: %s\n"), MY_EMAIL);
	exit(1);
}

int showquotas(int type, qid_t id)
{
	struct dquot *qlist, *q;
	char *msgi, *msgb;
	char timebuf[MAXTIMELEN];
	char name[MAXNAMELEN];
	struct quota_handle **handles;
	int lines = 0, bover, iover, over;
	time_t now;

	time(&now);
	id2name(id, type, name);
	handles = create_handle_list(0, NULL, type, fmt, IOI_READONLY | ((flags & FL_LOCALONLY) ? IOI_LOCALONLY : 0), ((flags & FL_NOAUTOFS) ? MS_NO_AUTOFS : 0));
	qlist = getprivs(id, handles, !!(flags & FL_QUIETREFUSE));
	over = 0;
	for (q = qlist; q; q = q->dq_next) {
		bover = iover = 0;
		if (!(flags & FL_VERBOSE) && !q->dq_dqb.dqb_isoftlimit && !q->dq_dqb.dqb_ihardlimit
		    && !q->dq_dqb.dqb_bsoftlimit && !q->dq_dqb.dqb_bhardlimit)
			continue;
		msgi = NULL;
		if (q->dq_dqb.dqb_ihardlimit && q->dq_dqb.dqb_curinodes >= q->dq_dqb.dqb_ihardlimit) {
			msgi = _("File limit reached on");
			iover = 1;
		}
		else if (q->dq_dqb.dqb_isoftlimit
			 && q->dq_dqb.dqb_curinodes > q->dq_dqb.dqb_isoftlimit) {
			if (q->dq_dqb.dqb_itime > now) {
				msgi = _("In file grace period on");
				iover = 2;
			}
			else {
				msgi = _("Over file quota on");
				iover = 3;
			}
		}
		msgb = NULL;
		if (q->dq_dqb.dqb_bhardlimit && toqb(q->dq_dqb.dqb_curspace) >= q->dq_dqb.dqb_bhardlimit) {
				msgb = _("Block limit reached on");
				bover = 1;
		}
		else if (q->dq_dqb.dqb_bsoftlimit
			 && toqb(q->dq_dqb.dqb_curspace) > q->dq_dqb.dqb_bsoftlimit) {
			if (q->dq_dqb.dqb_btime > now) {
				msgb = _("In block grace period on");
				bover = 2;
			}
			else {
				msgb = _("Over block quota on");
				bover = 3;
			}
		}
		over |= bover | iover;
		if (flags & FL_QUIET) {
			if ((msgi || msgb) && !lines++)
				heading(type, id, name, "");
			if (msgi)
				printf("\t%s %s\n", msgi, q->dq_h->qh_quotadev);
			if (msgb)
				printf("\t%s %s\n", msgb, q->dq_h->qh_quotadev);
			continue;
		}
		if ((flags & FL_VERBOSE) || q->dq_dqb.dqb_curspace || q->dq_dqb.dqb_curinodes) {
			char numbuf[3][MAXNUMLEN];

			if (!lines++)
				heading(type, id, name, "");
			if (strlen(q->dq_h->qh_quotadev) > 15)
				printf("%s\n%15s", q->dq_h->qh_quotadev, "");
			else
				printf("%15s", q->dq_h->qh_quotadev);
			if (bover)
				difftime2str(q->dq_dqb.dqb_btime, timebuf);
			space2str(toqb(q->dq_dqb.dqb_curspace), numbuf[0], !!(flags & FL_SMARTSIZE));
			space2str(q->dq_dqb.dqb_bsoftlimit, numbuf[1], !!(flags & FL_SMARTSIZE));
			space2str(q->dq_dqb.dqb_bhardlimit, numbuf[2], !!(flags & FL_SMARTSIZE));
			printf(" %7s%c %6s %7s %7s", numbuf[0], bover ? '*' : ' ', numbuf[1],
			       numbuf[2], bover > 1 ? timebuf : "");
			if (iover)
				difftime2str(q->dq_dqb.dqb_itime, timebuf);
			number2str(q->dq_dqb.dqb_curinodes, numbuf[0], !!(flags & FL_SMARTSIZE));
			number2str(q->dq_dqb.dqb_isoftlimit, numbuf[1], !!(flags & FL_SMARTSIZE));
			number2str(q->dq_dqb.dqb_ihardlimit, numbuf[2], !!(flags & FL_SMARTSIZE));
			printf(" %7s%c %6s %7s %7s\n", numbuf[0], iover ? '*' : ' ', numbuf[1],
			       numbuf[2], iover > 1 ? timebuf : "");
			continue;
		}
	}
	if (!(flags & FL_QUIET) && !lines)
		heading(type, id, name, _("none"));
	freeprivs(qlist);
	dispose_handle_list(handles);
	return over > 0 ? 1 : 0;
}

void heading(int type, qid_t id, char *name, char *tag)
{
	printf(_("Disk quotas for %s %s (%cid %d): %s\n"), type2name(type),
	       name, *type2name(type), id, tag);
	if (!(flags & FL_QUIET) && !tag[0]) {
		printf("%15s%8s %7s%8s%8s%8s %7s%8s%8s\n", _("Filesystem"),
		       _("blocks"), _("quota"), _("limit"), _("grace"),
		       _("files"), _("quota"), _("limit"), _("grace"));
	}
}
