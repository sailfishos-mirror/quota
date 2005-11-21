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
#ident "$Id: edquota.c,v 1.18 2005/11/21 22:30:23 jkar8572 Exp $"

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
#include <fcntl.h>

#include "pot.h"
#include "quotaops.h"
#include "quotasys.h"
#include "quotaio.h"
#include "common.h"

char *progname;
char *dirname;

void usage(void)
{
#if defined(RPC_SETQUOTA)
	errstr("%s%s%s%s",
		_("Usage:\n\tedquota [-r] [-u] [-F formatname] [-p username] [-f filesystem] username ...\n"),
		_("\tedquota [-r] -g [-F formatname] [-p groupname] [-f filesystem] groupname ...\n"),
		_("\tedquota [-r] [-u|g] [-F formatname] [-f filesystem] -t\n"),
		_("\tedquota [-r] [-u|g] [-F formatname] [-f filesystem] -T username|groupname ...\n"));
#else
	errstr("%s%s%s%s",
		_("Usage:\n\tedquota [-u] [-F formatname] [-p username] [-f filesystem] username ...\n"),
		_("\tedquota -g [-F formatname] [-p groupname] [-f filesystem] groupname ...\n"),
		_("\tedquota [-u|g] [-F formatname] [-f filesystem] -t\n"),
		_("\tedquota [-u|g] [-F formatname] [-f filesystem] -T username|groupname ...\n"));
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
	int tflag = 0, Tflag = 0, pflag = 0, rflag = 0, fmt = -1;
	struct quota_handle **handles;
	char *tmpfil, *tmpdir = NULL;

	gettexton();
	progname = basename(argv[0]);

	if (argc < 2)
		usage();

	dirname = NULL;
	quotatype = USRQUOTA;
#if defined(RPC_SETQUOTA)
	while ((ret = getopt(argc, argv, "ugrntTVp:F:f:")) != -1) {
#else
	while ((ret = getopt(argc, argv, "ugtTVp:F:f:")) != -1) {
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
		  case 'T':
			  Tflag++;
			  break;
		  case 'F':
			  if ((fmt = name2fmt(optarg)) == QF_ERROR)	/* Error? */
				  exit(1);
			  break;
		  case 'f':
			  dirname = optarg;
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

	if ((tflag && argc != 0) || (Tflag && argc < 1))
		usage();

	init_kernel_interface();
	handles = create_handle_list(dirname ? 1 : 0, dirname ? &dirname : NULL, quotatype, fmt, 0, rflag ? 0 : MS_LOCALONLY);
	if (!handles[0]) {
		dispose_handle_list(handles);
		fputs(_("No filesystems with quota detected.\n"), stderr);
		return 0;
	}
	if (pflag) {
		ret = 0;
		protoid = name2id(protoname, quotatype, NULL);
		protoprivs = getprivs(protoid, handles, 0);
		while (argc-- > 0) {
			id = name2id(*argv++, quotatype, NULL);
			curprivs = getprivs(id, handles, 0);

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
			if (putprivs(curprivs, COMMIT_LIMITS) == -1)
				ret = -1;
			freeprivs(curprivs);
		}
		if (dispose_handle_list(handles) == -1)
			ret = -1;
		freeprivs(protoprivs);
		exit(ret ? 1 : 0);
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
	ret = 0;
	if (tflag) {
		if (writetimes(handles, tmpfd) < 0) {
			errstr(_("Cannot write grace times to file.\n"));
			ret = -1;
		}
		if (editprivs(tmpfil) < 0) {
			errstr(_("Error while editting grace times.\n"));
			ret = -1;
		}
		if (readtimes(handles, tmpfd) < 0) {
			errstr(_("Failed to parse grace times file.\n"));
			ret = -1;
		}
	}
	else if (Tflag) {
		for (; argc > 0; argc--, argv++) {
			id = name2id(*argv, quotatype, NULL);
			curprivs = getprivs(id, handles, 0);
			if (writeindividualtimes(curprivs, tmpfd, *argv, quotatype) < 0) {
				errstr(_("Cannot write individual grace times to file.\n"));
				ret = -1;
				continue;
			}
			if (editprivs(tmpfil) < 0) {
				errstr(_("Error while editting individual grace times.\n"));
				ret = -1;
				continue;
			}
			if (readindividualtimes(curprivs, tmpfd) < 0) {
				errstr(_("Cannot read individual grace times from file.\n"));
				ret = -1;
				continue;
			}
			if (putprivs(curprivs, COMMIT_TIMES) == -1)
				ret = -1;
			freeprivs(curprivs);
		}
	}
	else {
		for (; argc > 0; argc--, argv++) {
			id = name2id(*argv, quotatype, NULL);
			curprivs = getprivs(id, handles, 0);
			if (writeprivs(curprivs, tmpfd, *argv, quotatype) < 0) {
				errstr(_("Cannot write quotas to file.\n"));
				ret = -1;
				continue;
			}
			if (editprivs(tmpfil) < 0) {
				errstr(_("Error while editting quotas.\n"));
				ret = -1;
				continue;
			}
			close(tmpfd);
			if ((tmpfd = open(tmpfil, O_RDONLY)) < 0)
				die(1, _("Cannot reopen!"));
			if (readprivs(curprivs, tmpfd) < 0) {
				errstr(_("Cannot read quotas from file.\n"));
				ret = -1;
				continue;
			}
			if (putprivs(curprivs, COMMIT_LIMITS) == -1)
				ret = -1;
			freeprivs(curprivs);
		}
	}
	if (dispose_handle_list(handles) == -1)
		ret = -1;

	close(tmpfd);
	unlink(tmpfil);
	free(tmpfil);
	return ret ? 1 : 0;
}
