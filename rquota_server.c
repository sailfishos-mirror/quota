/*
 * QUOTA    An implementation of the diskquota system for the LINUX
 *          operating system. QUOTA is implemented using the BSD systemcall
 *          interface as the means of communication with the user level.
 *          Should work for all filesystems because of integration into the
 *          VFS layer of the operating system.
 *          This is based on the Melbourne quota system wich uses both user and
 *          group quota files.
 *
 *          This part does the lookup of the info.
 *
 * Version: $Id: rquota_server.c,v 1.1 2001/03/23 12:03:27 jkar8572 Exp $
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 *          This program is free software; you can redistribute it and/or
 *          modify it under the terms of the GNU General Public License
 *          as published by the Free Software Foundation; either version
 *          2 of the License, or (at your option) any later version.
 */
#include <rpc/rpc.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <arpa/inet.h>
#include <paths.h>
#include <stdio.h>
#include <syslog.h>
#include <netdb.h>
#ifdef HOST_ACCESS
#include <tcpd.h>
#endif

#include "mntopt.h"
#include "quotaops.h"
#include "bylabel.h"
#include "rquota.h"
#include "quotaio.h"
#include "quotasys.h"
#include "dqblk_rpc.h"

#define STDIN_FILENO	0

#define TYPE_EXTENDED	0x01
#define ACTIVE		0x02

#define FACILITY	LOG_LOCAL7

#ifndef MAXPATHNAMELEN
#define MAXPATHNAMELEN BUFSIZ
#endif

#define NETTYPE AF_INET

#ifdef HOSTS_ACCESS
#define good_client(a,b) hosts_ctl("rpc.rquotad", b, inet_ntoa(a->sin_addr), "")
#endif

int allow_severity = LOG_INFO;
int deny_severity = LOG_WARNING;

/*
 * Global unix authentication credentials.
 */
extern struct authunix_parms *unix_cred;

int in_group(gid_t * gids, u_int len, gid_t gid)
{
	gid_t *gidsp = gids + len;

	while (gidsp > gids)
		if (*(--gids) == gid)
			return 1;

	return 0;
}

static inline void servnet2utildqblk(struct util_dqblk *u, sq_dqblk * n)
{
	u->dqb_bhardlimit = n->rq_bhardlimit;
	u->dqb_bsoftlimit = n->rq_bsoftlimit;
	u->dqb_ihardlimit = n->rq_fhardlimit;
	u->dqb_isoftlimit = n->rq_fsoftlimit;
	u->dqb_curspace = n->rq_curblocks << RPC_DQBLK_SIZE_BITS;
	u->dqb_curinodes = n->rq_curfiles;
	u->dqb_btime = n->rq_btimeleft;
	u->dqb_itime = n->rq_ftimeleft;
}

static inline void servutil2netdqblk(struct rquota *n, struct util_dqblk *u)
{
	n->rq_bhardlimit = u->dqb_bhardlimit;
	n->rq_bsoftlimit = u->dqb_bsoftlimit;
	n->rq_fhardlimit = u->dqb_ihardlimit;
	n->rq_fsoftlimit = u->dqb_isoftlimit;
	n->rq_curblocks = (u->dqb_curspace + RPC_DQBLK_SIZE - 1) >> RPC_DQBLK_SIZE_BITS;
	n->rq_curfiles = u->dqb_curinodes;
	n->rq_btimeleft = u->dqb_btime;
	n->rq_ftimeleft = u->dqb_itime;
}

setquota_rslt *setquotainfo(int flags, caddr_t * argp, struct svc_req *rqstp)
{
	static setquota_rslt result;

#if defined(RPC_SETQUOTA)
	union {
		setquota_args *args;
		ext_setquota_args *ext_args;
	} arguments;
	struct stat st;
	dev_t device;
	FILE *mntf;
	struct util_dqblk dqblk;
	struct dquot *dquot;
	struct mntent *mnt;
	char *pathname;
	int id, qcmd, type;
	struct quota_handle *handles[2] = { NULL, NULL };

#ifdef HOSTS_ACCESS
	struct hostent *hp;
	struct sockaddr_in *addr;

	addr = (svc_getcaller(rqstp->rq_xprt));
	hp = gethostbyaddr((char *)&(addr->sin_addr), sizeof(addr->sin_addr), AF_INET);

	if (!good_client(svc_getcaller(rqstp->rq_xprt), hp->h_name)) {
		result.status = Q_EPERM;
		return (&result);
	}

#endif

	/*
	 * First check authentication.
	 */
	if (flags & TYPE_EXTENDED) {
		arguments.ext_args = (ext_setquota_args *) argp;

		id = arguments.ext_args->sqa_id;
		if (unix_cred->aup_uid != 0) {
			result.status = Q_EPERM;
			return (&result);
		}

		qcmd = arguments.ext_args->sqa_qcmd;
		type = arguments.ext_args->sqa_type;
		pathname = arguments.ext_args->sqa_pathp;
		servnet2utildqblk(&dqblk, &arguments.ext_args->sqa_dqblk);
	}
	else {
		arguments.args = (setquota_args *) argp;

		id = arguments.args->sqa_id;
		if (unix_cred->aup_uid != 0) {
			result.status = Q_EPERM;
			return (&result);
		}

		qcmd = arguments.args->sqa_qcmd;
		type = USRQUOTA;
		pathname = arguments.args->sqa_pathp;
		servnet2utildqblk(&dqblk, &arguments.args->sqa_dqblk);
	}

	result.status = Q_NOQUOTA;
	if (stat(pathname, &st) == -1)
		return (&result);

	device = st.st_dev;
	result.setquota_rslt_u.sqr_rquota.rq_bsize = RPC_DQBLK_SIZE;

	mntf = setmntent(_PATH_MOUNTED, "r");
	while ((mnt = getmntent(mntf))) {
		if (stat(mnt->mnt_dir, &st) == -1)
			continue;
		if (st.st_dev != device)
			continue;
		if (!(handles[0] = init_io(mnt, type, -1)))
			continue;
		break;
	}
	endmntent(mntf);
	if (!(dquot = handles[0]->qh_ops->read_dquot(handles[0], id)))
		goto out;
	if (qcmd == QCMD(Q_RPC_SETQLIM, type) || qcmd == QCMD(Q_RPC_SETQUOTA, type)) {
		dquot->dq_dqb.dqb_bsoftlimit = dqblk.dqb_bsoftlimit;
		dquot->dq_dqb.dqb_bhardlimit = dqblk.dqb_bhardlimit;
		dquot->dq_dqb.dqb_isoftlimit = dqblk.dqb_isoftlimit;
		dquot->dq_dqb.dqb_ihardlimit = dqblk.dqb_ihardlimit;
		dquot->dq_dqb.dqb_btime = dqblk.dqb_btime;
		dquot->dq_dqb.dqb_itime = dqblk.dqb_itime;
	}
	if (qcmd == QCMD(Q_RPC_SETUSE, type) || qcmd == QCMD(Q_RPC_SETQUOTA, type)) {
		dquot->dq_dqb.dqb_curspace = dqblk.dqb_curspace;
		dquot->dq_dqb.dqb_curinodes = dqblk.dqb_curinodes;
	}
	if (handles[0]->qh_ops->commit_dquot(dquot) == -1)
		goto out;
	result.status = Q_OK;
      out:
	dispose_handle_list(handles);
#else
	result.status = Q_EPERM;
#endif
	return (&result);
}

getquota_rslt *getquotainfo(int flags, caddr_t * argp, struct svc_req * rqstp)
{
	static getquota_rslt result;
	union {
		getquota_args *args;
		ext_getquota_args *ext_args;
	} arguments;
	struct stat st;
	dev_t device;
	FILE *mntf;
	struct dquot *dquot = NULL;
	struct mntent *mnt;
	char *pathname;
	int id, type;
	struct quota_handle *handles[2] = { NULL, NULL };

#ifdef HOSTS_ACCESS
	struct hostent *hp;
	struct sockaddr_in *addr;

	addr = (svc_getcaller(rqstp->rq_xprt));
	hp = gethostbyaddr((char *)&(addr->sin_addr), sizeof(addr->sin_addr), AF_INET);

	if (!good_client(svc_getcaller(rqstp->rq_xprt), hp->h_name)) {
		return (FALSE);
	}
#endif

	/*
	 * First check authentication.
	 */
	if (flags & TYPE_EXTENDED) {
		arguments.ext_args = (ext_getquota_args *) argp;
		id = arguments.ext_args->gqa_id;
		type = arguments.ext_args->gqa_type;
		pathname = arguments.ext_args->gqa_pathp;

		if (type == USRQUOTA && unix_cred->aup_uid && unix_cred->aup_uid != id) {
			result.status = Q_EPERM;
			return (&result);
		}

		if (type == GRPQUOTA && unix_cred->aup_uid && unix_cred->aup_gid != id &&
		    !in_group((gid_t *) unix_cred->aup_gids, unix_cred->aup_len, id)) {
			result.status = Q_EPERM;
			return (&result);
		}
	}
	else {
		arguments.args = (getquota_args *) argp;
		id = arguments.args->gqa_uid;
		type = USRQUOTA;
		pathname = arguments.args->gqa_pathp;

		if (unix_cred->aup_uid && unix_cred->aup_uid != id) {
			result.status = Q_EPERM;
			return (&result);
		}
	}

	result.status = Q_NOQUOTA;

	if (stat(pathname, &st) == -1)
		return (&result);

	device = st.st_dev;
	result.getquota_rslt_u.gqr_rquota.rq_bsize = RPC_DQBLK_SIZE;

	mntf = setmntent(_PATH_MOUNTED, "r");
	while ((mnt = getmntent(mntf))) {
		if (stat(mnt->mnt_dir, &st) == -1)
			continue;
		if (st.st_dev != device)
			continue;
		if (!(handles[0] = init_io(mnt, type, -1)))
			continue;
		break;
	}
	endmntent(mntf);
	if (!(flags & ACTIVE) || QIO_ENABLED(handles[0]))
		dquot = handles[0]->qh_ops->read_dquot(handles[0], id);
	if (dquot) {
		result.status = Q_OK;
		result.getquota_rslt_u.gqr_rquota.rq_active =
			QIO_ENABLED(handles[0]) ? TRUE : FALSE;
		servutil2netdqblk(&result.getquota_rslt_u.gqr_rquota, &dquot->dq_dqb);
	}
	dispose_handle_list(handles);
	return (&result);
}

/*
 * Map RPC-entrypoints to local function names.
 */
getquota_rslt *rquotaproc_getquota_1_svc(getquota_args * argp, struct svc_req * rqstp)
{
	return (getquotainfo(0, (caddr_t *) argp, rqstp));
}

getquota_rslt *rquotaproc_getactivequota_1_svc(getquota_args * argp, struct svc_req * rqstp)
{
	return (getquotainfo(ACTIVE, (caddr_t *) argp, rqstp));
}

getquota_rslt *rquotaproc_getquota_2_svc(ext_getquota_args * argp, struct svc_req * rqstp)
{
	return (getquotainfo(TYPE_EXTENDED, (caddr_t *) argp, rqstp));
}

getquota_rslt *rquotaproc_getactivequota_2_svc(ext_getquota_args * argp, struct svc_req * rqstp)
{
	return (getquotainfo(TYPE_EXTENDED | ACTIVE, (caddr_t *) argp, rqstp));
}

setquota_rslt *rquotaproc_setquota_1_svc(setquota_args * argp, struct svc_req * rqstp)
{
	return (setquotainfo(0, (caddr_t *) argp, rqstp));
}

setquota_rslt *rquotaproc_setactivequota_1_svc(setquota_args * argp, struct svc_req * rqstp)
{
	return (setquotainfo(ACTIVE, (caddr_t *) argp, rqstp));
}

setquota_rslt *rquotaproc_setquota_2_svc(ext_setquota_args * argp, struct svc_req * rqstp)
{
	return (setquotainfo(TYPE_EXTENDED, (caddr_t *) argp, rqstp));
}

setquota_rslt *rquotaproc_setactivequota_2_svc(ext_setquota_args * argp, struct svc_req * rqstp)
{
	return (setquotainfo(TYPE_EXTENDED | ACTIVE, (caddr_t *) argp, rqstp));
}
