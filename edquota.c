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
#ident "$Id: edquota.c,v 1.6 2001/08/15 20:13:42 jkar8572 Exp $"

/*
 * Disk quota editor.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <paths.h>
#include <stdlib.h>

#include "pot.h"
#include "quotaops.h"
#include "quotasys.h"
#include "quotaio.h"
#include "common.h"

char *progname;

void usage(void)
{
#if defined(RPC_SETQUOTA)
	errstr("%s%s%s%s",
		_("Usage:\tedquota [-r] [-u] [-F formatname] [-p username] username ...\n"),
		_("\tedquota [-r] -g [-F formatname] [-p groupname] groupname ...\n"),
		_("\tedquota [-r] [-u] [-F formatname] -t\n"), _("\tedquota [-r] -g [-F formatname] -t\n"));
#else
	errstr("%s%s%s%s",
		_("Usage:\tedquota [-u] [-F formatname] [-p username] username ...\n"),
		_("\tedquota -g [-F formatname] [-p groupname] groupname ...\n"),
		_("\tedquota [-u] [-F formatname] -t\n"), _("\tedquota -g [-F formatname] -t\n"));
#endif
	fprintf(stderr, _("Bugs to: %s\n"), MY_EMAIL);
	exit(1);
}

int main(int argc, char **argv)
{
	struct dquot *protoprivs, *curprivs, *pprivs, *cprivs;
	long id, protoid;
	int quotatype, tmpfd, ret;
	char *protoname = NULL;
	int tflag = 0, pflag = 0, rflag = 0, fmt = -1;
	struct quota_handle **handles;
	char *tmpfil, *tmpdir = NULL;

	gettexton();
	progname = basename(argv[0]);

	if (argc < 2)
		usage();

	quotatype = USRQUOTA;
#if defined(RPC_SETQUOTA)
	while ((ret = getopt(argc, argv, "ugrntVp:F:")) != -1) {
#else
	while ((ret = getopt(argc, argv, "ugtVp:F:")) != -1) {
#endif
		switch (ret) {
		  case 'p':
			  protoname = optarg;
			  pflag++;
			  break;
		  case 'g':
			  quotatype = GRPQUOTA;
			  break;
#if defined(RPC_SETQUOTA)
		  case 'n':
		  case 'r':
			  rflag++;
			  break;
#endif
		  case 'u':
			  quotatype = USRQUOTA;
			  break;
		  case 't':
			  tflag++;
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

	if (tflag && argc != 0)
		usage();

	handles = create_handle_list(0, NULL, quotatype, fmt, rflag ? 0 : IOI_LOCALONLY);
	if (!handles[0]) {
		dispose_handle_list(handles);
		fputs(_("No filesystems with quota detected.\n"), stderr);
		return 0;
	}
	if (pflag) {
		protoid = name2id(protoname, quotatype);
		protoprivs = getprivs(protoid, handles);
		while (argc-- > 0) {
			id = name2id(*argv++, quotatype);
			curprivs = getprivs(id, handles);

			for (pprivs = protoprivs, cprivs = curprivs; pprivs && cprivs;
			     pprivs = pprivs->dq_next, cprivs = cprivs->dq_next) {
				if (!devcmp_handles(pprivs->dq_h, cprivs->dq_h))
					errstr(_("fsname mismatch\n"));
				else {
					cprivs->dq_dqb.dqb_bsoftlimit =
						pprivs->dq_dqb.dqb_bsoftlimit;
					cprivs->dq_dqb.dqb_bhardlimit =
						pprivs->dq_dqb.dqb_bhardlimit;
					cprivs->dq_dqb.dqb_isoftlimit =
						pprivs->dq_dqb.dqb_isoftlimit;
					cprivs->dq_dqb.dqb_ihardlimit =
						pprivs->dq_dqb.dqb_ihardlimit;
					update_grace_times(cprivs);
				}
			}
			putprivs(curprivs);
			freeprivs(curprivs);
		}
		dispose_handle_list(handles);
		freeprivs(protoprivs);
		warn_new_kernel(fmt);
		exit(0);
	}

	umask(077);
	if (getuid() == geteuid() && getgid() == getegid())
		tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = _PATH_TMP;
	tmpfil = smalloc(strlen(tmpdir) + strlen("/EdP.aXXXXXX") + 1);
	strcpy(tmpfil, tmpdir);
	strcat(tmpfil, "/EdP.aXXXXXX");
	tmpfd = mkstemp(tmpfil);
	fchown(tmpfd, getuid(), getgid());
	if (tflag) {
		if (writetimes(handles, tmpfd) < 0) {
			unlink(tmpfil);
			die(1, _("Can't write grace times to file.\n"));
		}
		if (editprivs(tmpfil) < 0) {
			unlink(tmpfil);
			die(1, _("Error while editting grace times.\n"));
		}
		if (readtimes(handles, tmpfd) < 0) {
			unlink(tmpfil);
			die(1, _("Failed to parse grace times file.\n"));
		}
	}
	else {
		for (; argc > 0; argc--, argv++) {
			id = name2id(*argv, quotatype);
			curprivs = getprivs(id, handles);
			if (writeprivs(curprivs, tmpfd, *argv, quotatype) < 0) {
				errstr(_("Can't write quotas to file.\n"));
				continue;
			}
			if (editprivs(tmpfil) < 0) {
				errstr(_("Error while editting quotas.\n"));
				continue;
			}
			if (readprivs(curprivs, tmpfd) < 0) {
				errstr(_("Can't read quotas from file.\n"));
				continue;
			}
			putprivs(curprivs);
			freeprivs(curprivs);
		}
	}
	dispose_handle_list(handles);
	warn_new_kernel(fmt);

	close(tmpfd);
	unlink(tmpfil);
	free(tmpfil);
	return 0;
}
