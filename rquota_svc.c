/*
 * QUOTA    An implementation of the diskquota system for the LINUX operating
 *          system. QUOTA is implemented using the BSD systemcall interface
 *          as the means of communication with the user level. Should work for
 *          all filesystems because of integration into the VFS layer of the
 *          operating system. This is based on the Melbourne quota system wich
 *          uses both user and group quota files.
 *
 *          Rquota service handlers.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *          changes for new utilities by Jan Kara <jack@suse.cz>
 *          patches by Jani Jaakkola <jjaakkol@cs.helsinki.fi>
 *
 * Version: $Id: rquota_svc.c,v 1.12 2002/11/21 21:15:26 jkar8572 Exp $
 *
 *          This program is free software; you can redistribute it and/or
 *          modify it under the terms of the GNU General Public License as
 *          published by the Free Software Foundation; either version 2 of
 *          the License, or (at your option) any later version.
 */
                                                                                                          
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/pmap_clnt.h>	/* for pmap_unset */
#include <stdio.h>
#include <stdlib.h>		/* getenv, exit */
#include <string.h>		/* strcmp */
#include <memory.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#ifdef HOSTS_ACCESS
#include <tcpd.h>
#include <netdb.h>
#endif

#ifdef __STDC__
#define SIG_PF void(*)(int)
#endif

extern int svctcp_socket (u_long __number, int __reuse);
extern int svcudp_socket (u_long __number, int __reuse);

#include "pot.h"
#include "common.h"
#include "rquota.h"
#include "quotasys.h"

char *progname;

/*
 * Global authentication credentials.
 */
struct authunix_parms *unix_cred;

int disable_setquota=1;            /* Disables setquota rpc */
int disable_daemon=0;              /* Disable daemon() call */

static struct option options[]= {
	{ "version", 0, NULL, 'V' },
	{ "help", 0, NULL, 'h' },
	{ "foreground", 0 , NULL, 'F' },
#ifdef RPC_SETQUOTA
	{ "no-setquota", 0 , NULL, 's' },
	{ "setquota", 0, NULL, 'S' },
#endif
	{ NULL, 0, NULL , 0 }
};

static void show_help(void)
{
#ifdef RPC_SETQUOTA
	errstr(_("Usage: %s [options]\nOptions are:\n\
 -h --help         shows this text\n\
 -V --version      shows version information\n\
 -F --foreground   starts the quota service in foreground\n\
 -s --no-setquota  disables remote calls to setquota (default)\n\
 -S --setquota     enables remote calls to setquota\n"), progname);
#else
	errstr(_("Usage: %s [options]\nOptions are:\n\
 -h --help         shows this text\n\
 -V --version      shows version information\n\
 -F --foreground   starts the quota service in foreground\n"), progname);
#endif
}

static void parse_options(int argc, char **argv)
{
	char ostr[128]="";
	int i,opt;
	int j=0;

	for(i=0; options[i].name; i++) {
		ostr[j++] = options[i].val;
		if (options[i].has_arg) ostr[j++] = ':';
	}
	while ((opt=getopt_long(argc, argv, ostr, options, NULL))>=0) {
		switch(opt) {
			case 'V': version(); exit(0);
			case 'h': show_help(); exit(0);
			case 'F': disable_daemon = 1; break;
#ifdef RPC_SETQUOTA
			case 's': disable_setquota = 1; break;
			case 'S': disable_setquota = 0; break;
#endif
			default:
				errstr(_("Unknown option '%c'.\n"), opt);
				show_help();
				exit(1);
		}
	}
}


/*
 * good_client checks if an quota client should be allowed to
 * execute the requested rpc call.
 */
int good_client(struct sockaddr_in *addr, ulong rq_proc)
{
#ifdef HOSTS_ACCESS
	struct hostent *h;
	char *name, **ad;
#endif
	const char *remote=inet_ntoa(addr->sin_addr);

	if (rq_proc==RQUOTAPROC_SETQUOTA ||
	     rq_proc==RQUOTAPROC_SETACTIVEQUOTA) {
		/* If setquota is disabled, fail always */
		if (disable_setquota) {
			errstr(_("host %s attempted to call setquota when disabled\n"),
			       remote);

			return 0;
		}
		/* Require that SETQUOTA calls originate from port < 1024 */
		if (ntohs(addr->sin_port)>=1024) {
			errstr(_("host %s attempted to call setquota from port >= 1024\n"),
			       remote);
			return 0;
		}
		/* Setquota OK */
	}

#ifdef HOSTS_ACCESS
	/* NOTE: we could use different servicename for setquota calls to
	 * allow only some hosts to call setquota. */

	/* Check IP address */
	if (hosts_ctl("rquotad", "", remote, ""))
		return 1;
	/* Get address */
	if (!(h = gethostbyaddr((const char *)&(addr->sin_addr), sizeof(addr->sin_addr), AF_INET)))
		goto denied;
	if (!(name = alloca(strlen(h->h_name)+1)))
		goto denied;
	strcpy(name, h->h_name);
	/* Try to resolve it back */
	if (!(h = gethostbyname(name)))
		goto denied;
	for (ad = h->h_addr_list; *ad; ad++)
		if (!memcmp(*ad, &(addr->sin_addr), h->h_length))
			break;
	if (!*ad)	/* Our address not found? */
		goto denied;	
	/* Check host name */
	if (hosts_ctl("rquotad", "", h->h_name, ""))
		return 1;
	/* Check aliases */
	for (ad = h->h_aliases; *ad; ad++)
		if (hosts_ctl("rquotad", "", *ad, ""))
			return 1;
denied:
	errstr(_("Denied access to host %s\n"), remote);
	return 0;
#else
	/* If no access checking is available, OK always */
	return 1;
#endif
}

static void rquotaprog_1(struct svc_req *rqstp, register SVCXPRT * transp)
{
	union {
		getquota_args rquotaproc_getquota_1_arg;
		setquota_args rquotaproc_setquota_1_arg;
		getquota_args rquotaproc_getactivequota_1_arg;
		setquota_args rquotaproc_setactivequota_1_arg;
	} argument;
	char *result;
	xdrproc_t xdr_argument, xdr_result;
	char *(*local) (char *, struct svc_req *);

	/*
	 *  Authenticate host
	 */
	if (!good_client(svc_getcaller(rqstp->rq_xprt),rqstp->rq_proc)) {
		svcerr_auth (transp, AUTH_FAILED);
		return;
	}

	/*
	 * Don't bother authentication for NULLPROC.
	 */
	if (rqstp->rq_proc == NULLPROC) {
		(void)svc_sendreply(transp, (xdrproc_t) xdr_void, (char *)NULL);
		return;
	}

	/*
	 * Get authentication.
	 */
	switch (rqstp->rq_cred.oa_flavor) {
	  case AUTH_UNIX:
		  unix_cred = (struct authunix_parms *)rqstp->rq_clntcred;
		  break;
	  case AUTH_NULL:
	  default:
		  svcerr_weakauth(transp);
		  return;
	}

	switch (rqstp->rq_proc) {
	  case RQUOTAPROC_GETQUOTA:
		  xdr_argument = (xdrproc_t) xdr_getquota_args;
		  xdr_result = (xdrproc_t) xdr_getquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_getquota_1_svc;
		  break;

	  case RQUOTAPROC_SETQUOTA:
		  xdr_argument = (xdrproc_t) xdr_setquota_args;
		  xdr_result = (xdrproc_t) xdr_setquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_setquota_1_svc;
		  break;

	  case RQUOTAPROC_GETACTIVEQUOTA:
		  xdr_argument = (xdrproc_t) xdr_getquota_args;
		  xdr_result = (xdrproc_t) xdr_getquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_getactivequota_1_svc;
		  break;

	  case RQUOTAPROC_SETACTIVEQUOTA:
		  xdr_argument = (xdrproc_t) xdr_setquota_args;
		  xdr_result = (xdrproc_t) xdr_setquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_setactivequota_1_svc;
		  break;

	  default:
		  svcerr_noproc(transp);
		  return;
	}
	memset(&argument, 0, sizeof(argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t) & argument)) {
		svcerr_decode(transp);
		return;
	}
	result = (*local) ((char *)&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (caddr_t) & argument)) {
		errstr(_("unable to free arguments\n"));
		exit(1);
	}
	return;
}

static void rquotaprog_2(struct svc_req *rqstp, register SVCXPRT * transp)
{
	union {
		ext_getquota_args rquotaproc_getquota_2_arg;
		ext_setquota_args rquotaproc_setquota_2_arg;
		ext_getquota_args rquotaproc_getactivequota_2_arg;
		ext_setquota_args rquotaproc_setactivequota_2_arg;
	} argument;
	char *result;
	xdrproc_t xdr_argument, xdr_result;
	char *(*local) (char *, struct svc_req *);

	/*
	 *  Authenticate host
	 */
	if (!good_client(svc_getcaller(rqstp->rq_xprt),rqstp->rq_proc)) {
		svcerr_auth (transp, AUTH_FAILED);
		return;
	}

	/*
	 * Don't bother authentication for NULLPROC.
	 */
	if (rqstp->rq_proc == NULLPROC) {
		(void)svc_sendreply(transp, (xdrproc_t) xdr_void, (char *)NULL);
		return;
	}

	/*
	 * Get authentication.
	 */
	switch (rqstp->rq_cred.oa_flavor) {
	  case AUTH_UNIX:
		  unix_cred = (struct authunix_parms *)rqstp->rq_clntcred;
		  break;
	  case AUTH_NULL:
	  default:
		  svcerr_weakauth(transp);
		  return;
	}

	switch (rqstp->rq_proc) {
	  case RQUOTAPROC_GETQUOTA:
		  xdr_argument = (xdrproc_t) xdr_ext_getquota_args;
		  xdr_result = (xdrproc_t) xdr_getquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_getquota_2_svc;
		  break;

	  case RQUOTAPROC_SETQUOTA:
		  xdr_argument = (xdrproc_t) xdr_ext_setquota_args;
		  xdr_result = (xdrproc_t) xdr_setquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_setquota_2_svc;
		  break;

	  case RQUOTAPROC_GETACTIVEQUOTA:
		  xdr_argument = (xdrproc_t) xdr_ext_getquota_args;
		  xdr_result = (xdrproc_t) xdr_getquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_getactivequota_2_svc;
		  break;

	  case RQUOTAPROC_SETACTIVEQUOTA:
		  xdr_argument = (xdrproc_t) xdr_ext_setquota_args;
		  xdr_result = (xdrproc_t) xdr_setquota_rslt;
		  local = (char *(*)(char *, struct svc_req *))rquotaproc_setactivequota_2_svc;
		  break;

	  default:
		  svcerr_noproc(transp);
		  return;
	}
	memset(&argument, 0, sizeof(argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t) & argument)) {
		svcerr_decode(transp);
		return;
	}
	result = (*local) ((char *)&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (caddr_t) & argument)) {
		errstr(_("unable to free arguments\n"));
		exit(1);
	}
	return;
}

static void
unregister (int sig)
{
	(void)pmap_unset(RQUOTAPROG, RQUOTAVERS);
	(void)pmap_unset(RQUOTAPROG, EXT_RQUOTAVERS);
}

int main(int argc, char **argv)
{
	register SVCXPRT *transp;
	struct sigaction sa;

	gettexton();
	progname = basename(argv[0]);
	parse_options(argc, argv);

	init_kernel_interface();
	(void)pmap_unset(RQUOTAPROG, RQUOTAVERS);
	(void)pmap_unset(RQUOTAPROG, EXT_RQUOTAVERS);

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	sa.sa_handler = unregister;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	transp = svcudp_create(svcudp_socket (RQUOTAPROG, 1));
	if (transp == NULL) {
		errstr(_("cannot create udp service.\n"));
		exit(1);
	}
	if (!svc_register(transp, RQUOTAPROG, RQUOTAVERS, rquotaprog_1, IPPROTO_UDP)) {
		errstr(_("unable to register (RQUOTAPROG, RQUOTAVERS, udp).\n"));
		exit(1);
	}
	if (!svc_register(transp, RQUOTAPROG, EXT_RQUOTAVERS, rquotaprog_2, IPPROTO_UDP)) {
		errstr(_("unable to register (RQUOTAPROG, EXT_RQUOTAVERS, udp).\n"));
		exit(1);
	}

	transp = svctcp_create(svctcp_socket (RQUOTAPROG, 1), 0, 0);
	if (transp == NULL) {
		errstr(_("cannot create tcp service.\n"));
		exit(1);
	}
	if (!svc_register(transp, RQUOTAPROG, RQUOTAVERS, rquotaprog_1, IPPROTO_TCP)) {
		errstr(_("unable to register (RQUOTAPROG, RQUOTAVERS, tcp).\n"));
		exit(1);
	}
	if (!svc_register(transp, RQUOTAPROG, EXT_RQUOTAVERS, rquotaprog_2, IPPROTO_TCP)) {
		errstr(_("unable to register (RQUOTAPROG, EXT_RQUOTAVERS, tcp).\n"));
		exit(1);
	}

	if (!disable_daemon) {
		use_syslog();
		daemon(0, 0);
	}
	svc_run();
	errstr(_("svc_run returned\n"));
	exit(1);
	/* NOTREACHED */
}
