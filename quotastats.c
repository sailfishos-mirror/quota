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
 * Version: $Id: quotastats.c,v 1.5 2001/09/26 12:26:11 jkar8572 Exp $
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
#include "common.h"
#include "quota.h"
#include "quotasys.h"
#include "quotaio.h"
#include "quotaio_v1.h"
#include "dqblk_v1.h"
#include "quotaio_v2.h"
#include "dqblk_v2.h"

char *progname;

static inline int get_stats(struct util_dqstats *dqstats)
{
	struct v1_dqstats old_dqstats;
	struct v2_dqstats v0_dqstats;
	FILE *f;
	int ret = -1;

	if ((f = fopen(QSTAT_FILE, "r"))) {
		if (fscanf(f, "Version %u", &dqstats->version) != 1) {
			errstr(_("Can't parse quota version.\n"));
			goto out;
		}
		if (dqstats->version > KERN_KNOWN_QUOTA_VERSION) {
			errstr(_("Kernel quota version %u is too new.\n"), dqstats->version);
			goto out;
		}
		if (fscanf(f, "%u %u %u %u %u %u %u %u", &dqstats->lookups, &dqstats->drops,
			   &dqstats->reads, &dqstats->writes, &dqstats->cache_hits,
			   &dqstats->allocated_dquots, &dqstats->free_dquots, &dqstats->syncs) != 8) {
			errstr(_("Can't parse quota statistics.\n"));
			goto out;
		}
	}
	else if (quotactl(QCMD(Q_V2_GETSTATS, 0), NULL, 0, (caddr_t)&v0_dqstats) >= 0) {
		/* Structures are currently the same */
		memcpy(dqstats, &v0_dqstats, sizeof(v0_dqstats));
	}
	else {
		if (errno != EINVAL) {
			errstr(_("Error while getting quota statistics from kernel: %s\n"), strerror(errno));
			goto out;
		}
		if (quotactl(QCMD(Q_V1_GETSTATS, 0), NULL, 0, (caddr_t)&old_dqstats) < 0) {
			errstr(_("Error while getting old quota statistics from kernel: %s\n"), strerror(errno));
			goto out;
		}
		memcpy(dqstats, &old_dqstats, sizeof(old_dqstats));
		dqstats->version = 0;
	}
	ret = 0;
out:
	if (f)
		fclose(f);
	return ret;
}

static inline int print_stats(struct util_dqstats *dqstats)
{
	if (!dqstats->version)
		printf(_("Kernel quota version: old\n"));
	else
		printf(_("Kernel quota version: %u.%u.%u\n"), dqstats->version/10000, dqstats->version/100%100, dqstats->version%100);
	printf(_("Number of dquot lookups: %ld\n"), (long)dqstats->lookups);
	printf(_("Number of dquot drops: %ld\n"), (long)dqstats->drops);
	printf(_("Number of dquot reads: %ld\n"), (long)dqstats->reads);
	printf(_("Number of dquot writes: %ld\n"), (long)dqstats->writes);
	printf(_("Number of quotafile syncs: %ld\n"), (long)dqstats->syncs);
	printf(_("Number of dquot cache hits: %ld\n"), (long)dqstats->cache_hits);
	printf(_("Number of allocated dquots: %ld\n"), (long)dqstats->allocated_dquots);
	printf(_("Number of free dquots: %ld\n"), (long)dqstats->free_dquots);
	printf(_("Number of in use dquot entries (user/group): %ld\n"),
		(long)(dqstats->allocated_dquots - dqstats->free_dquots));
	return (0);
}

int main(int argc, char **argv)
{
	struct util_dqstats dqstats;

	gettexton();
	progname = basename(argv[0]);

	if (!get_stats(&dqstats))
		print_stats(&dqstats);
	return 0;
}
