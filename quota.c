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
#ident "$Id: quota.c,v 1.4 2001/04/26 09:36:08 jkar8572 Exp $"

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

int qflag, vflag, fmt = -1;

void usage(void);
void showquotas(int type, qid_t id);
void heading(int type, qid_t id, char *name, char *tag);

int main(int argc, char **argv)
{
	int ngroups;
	gid_t gidset[NGROUPS];
	int i, ret, gflag = 0, uflag = 0;

	gettexton();

	while ((ret = getopt(argc, argv, "guqvVF:")) != -1) {
		switch (ret) {
		  case 'g':
			  gflag++;
			  break;
		  case 'u':
			  uflag++;
			  break;
		  case 'q':
			  qflag++;
			  break;
		  case 'v':
			  vflag++;
			  break;
		  case 'F':
			  if ((fmt = name2fmt(optarg)) == QF_ERROR)	/* Error? */
				  exit(1);
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

	warn_new_kernel(fmt);
	if (!uflag && !gflag)
		uflag++;

	if (argc == 0) {
		if (uflag)
			showquotas(USRQUOTA, getuid());
		if (gflag) {
			ngroups = getgroups(NGROUPS, gidset);
			if (ngroups < 0)
				die(1, _("quota: getgroups(): %s\n"), strerror(errno));
			for (i = 0; i < ngroups; i++)
				showquotas(GRPQUOTA, gidset[i]);
		}
		exit(0);
	}

	if (uflag && gflag)
		usage();

	if (uflag)
		for (; argc > 0; argc--, argv++)
			showquotas(USRQUOTA, user2uid(*argv));
	else if (gflag)
		for (; argc > 0; argc--, argv++)
			showquotas(GRPQUOTA, group2gid(*argv));
	return 0;
}

void usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n",
		_("Usage: quota [-guqvV] [-F quotaformat]"),
		_("\tquota [-qv] [-F quotaformat] -u username ..."),
		_("\tquota [-qv] [-F quotaformat] -g groupname ..."));
	fprintf(stderr, _("Bugs to: %s\n"), MY_EMAIL);
	exit(1);
}

void showquotas(int type, qid_t id)
{
	struct dquot *qlist, *q;
	char *msgi, *msgb;
	char timebuf[MAXTIMELEN];
	char name[MAXNAMELEN];
	struct quota_handle **handles;
	int lines = 0;
	time_t now;

	time(&now);
	id2name(id, type, name);
	handles = create_handle_list(0, NULL, type, fmt, 0);
	qlist = getprivs(id, handles);
	for (q = qlist; q; q = q->dq_next) {
		if (!vflag && !q->dq_dqb.dqb_isoftlimit && !q->dq_dqb.dqb_ihardlimit
		    && !q->dq_dqb.dqb_bsoftlimit && !q->dq_dqb.dqb_bhardlimit)
			continue;
		msgi = NULL;
		if (q->dq_dqb.dqb_ihardlimit && q->dq_dqb.dqb_curinodes >= q->dq_dqb.dqb_ihardlimit)
			msgi = _("File limit reached on");
		else if (q->dq_dqb.dqb_isoftlimit
			 && q->dq_dqb.dqb_curinodes >= q->dq_dqb.dqb_isoftlimit) {
			if (q->dq_dqb.dqb_itime > now)
				msgi = _("In file grace period on");
			else
				msgi = _("Over file quota on");
		}
		msgb = NULL;
		if (q->dq_dqb.dqb_bhardlimit
		    && toqb(q->dq_dqb.dqb_curspace) >= q->dq_dqb.dqb_bhardlimit)
				msgb = _("Block limit reached on");
		else if (q->dq_dqb.dqb_bsoftlimit
			 && toqb(q->dq_dqb.dqb_curspace) >= q->dq_dqb.dqb_bsoftlimit) {
			if (q->dq_dqb.dqb_btime > now)
				msgb = _("In block grace period on");
			else
				msgb = _("Over block quota on");
		}
		if (qflag) {
			if ((msgi || msgb) && !lines++)
				heading(type, id, name, "");
			if (msgi)
				printf("\t%s %s\n", msgi, q->dq_h->qh_quotadev);
			if (msgb)
				printf("\t%s %s\n", msgb, q->dq_h->qh_quotadev);
			continue;
		}
		if (vflag || q->dq_dqb.dqb_curspace || q->dq_dqb.dqb_curinodes) {
			if (!lines++)
				heading(type, id, name, "");
			if (strlen(q->dq_h->qh_quotadev) > 15)
				printf("%s\n%15s", q->dq_h->qh_quotadev, "");
			else
				printf("%15s", q->dq_h->qh_quotadev);
			if (msgb)
				difftime2str(q->dq_dqb.dqb_btime, timebuf);
			printf("%8Lu%c%7Lu%8Lu%8s", (long long)toqb(q->dq_dqb.dqb_curspace),
			       msgb ? '*' : ' ', (long long)q->dq_dqb.dqb_bsoftlimit,
			       (long long)q->dq_dqb.dqb_bhardlimit, msgb ? timebuf : "");
			if (msgi)
				difftime2str(q->dq_dqb.dqb_itime, timebuf);
			printf("%8Lu%c%7Lu%8Lu%8s\n", (long long)q->dq_dqb.dqb_curinodes,
			       msgi ? '*' : ' ', (long long)q->dq_dqb.dqb_isoftlimit,
			       (long long)q->dq_dqb.dqb_ihardlimit, msgi ? timebuf : "");
			continue;
		}
	}
	if (!qflag && !lines)
		heading(type, id, name, _("none"));
	freeprivs(qlist);
	dispose_handle_list(handles);
}

void heading(int type, qid_t id, char *name, char *tag)
{
	printf(_("Disk quotas for %s %s (%cid %d): %s\n"), type2name(type),
	       name, *type2name(type), id, tag);
	if (!qflag && !tag[0]) {
		printf("%15s%8s %7s%8s%8s%8s %7s%8s%8s\n", _("Filesystem"),
		       _("blocks"), _("quota"), _("limit"), _("grace"),
		       _("files"), _("quota"), _("limit"), _("grace"));
	}
}
