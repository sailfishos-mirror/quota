/* Copyright (C) 2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <errno.h>

#include "pot.h"

static int svc_socket (u_long number, int type, int protocol, int reuse)
{
	struct sockaddr_in addr;
	socklen_t len = sizeof (struct sockaddr_in);
	char rpcdata [1024], servdata [1024];
	struct rpcent rpcbuf, *rpcp;
	struct servent servbuf, *servp;
	int sock, ret;
	const char *proto = protocol == IPPROTO_TCP ? "tcp" : "udp";

	if ((sock = socket (AF_INET, type, protocol)) < 0) {
		perror (_("svc_socket: socket creation problem"));
		return sock;
	}

	if (reuse) {
		ret = 1;
		ret = setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof(ret));
		if (ret < 0) {
			perror (_("svc_socket: socket reuse problem"));
			return ret;
		}
	}

	memset(&addr, sizeof (addr), 0);
	addr.sin_family = AF_INET;

	ret = getrpcbynumber_r (number, &rpcbuf, rpcdata, sizeof(rpcdata), &rpcp);
	if (ret == 0 && rpcp != NULL) {
		/* First try name.	*/
		ret = getservbyname_r(rpcp->r_name, proto, &servbuf, servdata,
		                       sizeof servdata, &servp);
		if ((ret != 0 || servp == NULL) && rpcp->r_aliases) {
			const char **a;

			/* Then we try aliases.	*/
			for (a = (const char **) rpcp->r_aliases; *a != NULL; a++) {
				ret = getservbyname_r (*a, proto, &servbuf, servdata,
						 sizeof servdata, &servp);
				if (ret == 0 && servp != NULL)
					break;
			}
		}
	}

	if (ret == 0 && servp != NULL) {
		addr.sin_port = servp->s_port;
		if (bind (sock, (struct sockaddr *) &addr, len) < 0) {
			perror (_("svc_socket: bind problem"));
			close (sock);
			sock = -1;
		}
	}
	else {
		if (bindresvport (sock, &addr)) {
			addr.sin_port = 0;
			if (bind (sock, (struct sockaddr *) &addr, len) < 0) {
				perror (_("svc_socket: bind problem"));
				close (sock);
				sock = -1;
			}
		}
	}

	return sock;
}

/*
 * Create and bind a TCP socket based on program number
 */
int svctcp_socket (u_long number, int reuse)
{
	return svc_socket (number, SOCK_STREAM, IPPROTO_TCP, reuse);
}

/*
 * Create and bind a UDP socket based on program number
 */
int svcudp_socket (u_long number, int reuse)
{
	return svc_socket (number, SOCK_DGRAM, IPPROTO_UDP, reuse);
}
