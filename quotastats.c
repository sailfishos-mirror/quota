/*
 * QUOTA    An implementation of the diskquota system for the LINUX operating
 *          system. QUOTA is implemented using the BSD systemcall interface
 *          as the means of communication with the user level. Should work for
 *          all filesystems because of integration into the VFS layer of the
 *          operating system. This is based on the Melbourne quota system wich
 *          uses both user and group quota files.
 * 
 *          Program to query for the internal statistics.
 * 
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: quotastats.c,v 1.3 2001/07/16 03:24:49 jkar8572 Exp $
 *
 *          This program is free software; you can redistribute it and/or
 *          modify it under the terms of the GNU General Public License as
 *          published by the Free Software Foundation; either version 2 of
 *          the License, or (at your option) any later version.
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "pot.h"
#include "quota.h"
#include "quotaio_v1.h"
#include "dqblk_v1.h"

static inline int get_stats(struct dqstats *dqstats)
{
	struct v1_dqstats old_dqstats;
	
	if (quotactl(QCMD(Q_GETSTATS, 0), NULL, 0, (caddr_t)dqstats) < 0) {
		if (errno != EINVAL)
			return -1;
		if (quotactl(QCMD(Q_V1_GETSTATS, 0), NULL, 0, (caddr_t)&old_dqstats) < 0)
			return -1;
		memcpy(dqstats, &old_dqstats, sizeof(old_dqstats));
		dqstats->version = 0;
	}
	return 0;
}

static inline int print_stats(struct dqstats *dqstats)
{
	fprintf(stdout, _("Number of dquot lookups: %ld\n"), (long)dqstats->lookups);
	fprintf(stdout, _("Number of dquot drops: %ld\n"), (long)dqstats->drops);
	fprintf(stdout, _("Number of still active inodes with quota : %ld\n"),
		(long)(dqstats->lookups - dqstats->drops));
	fprintf(stdout, _("Number of dquot reads: %ld\n"), (long)dqstats->reads);
	fprintf(stdout, _("Number of dquot writes: %ld\n"), (long)dqstats->writes);
	fprintf(stdout, _("Number of quotafile syncs: %ld\n"), (long)dqstats->syncs);
	fprintf(stdout, _("Number of dquot cache hits: %ld\n"), (long)dqstats->cache_hits);
	fprintf(stdout, _("Number of allocated dquots: %ld\n"), (long)dqstats->allocated_dquots);
	fprintf(stdout, _("Number of free dquots: %ld\n"), (long)dqstats->free_dquots);
	fprintf(stdout, _("Number of in use dquot entries (user/group): %ld\n"),
		(long)(dqstats->allocated_dquots - dqstats->free_dquots));
	return (0);
}

int main(int argc, char **argv)
{
	struct dqstats dqstats;

	gettexton();

	if (!get_stats(&dqstats))
		print_stats(&dqstats);
	return 0;
}
