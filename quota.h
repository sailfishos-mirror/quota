#ifndef _QUOTA_H
#define _QUOTA_H

#include <sys/types.h>

typedef u_int32_t qid_t;	/* Type in which we store ids in memory */
typedef u_int64_t qsize_t;	/* Type in which we store size limitations */

#define MAXQUOTAS 2
#define USRQUOTA  0		/* element used for user quotas */
#define GRPQUOTA  1		/* element used for group quotas */

/*
 * Definitions for the default names of the quotas files.
 */
#define INITQFNAMES { \
	"user",    /* USRQUOTA */ \
	"group",   /* GRPQUOTA */ \
	"undefined", \
}

/*
 * Definitions of magics and versions of current quota files
 */
#define INITQMAGICS {\
	0xd9c01f11,	/* USRQUOTA */\
	0xd9c01927	/* GRPQUOTA */\
}

/* Size of blocks in which are counted size limits in generic utility parts */
#define QUOTABLOCK_BITS 10
#define QUOTABLOCK_SIZE (1 << QUOTABLOCK_BITS)

/* Conversion routines from and to quota blocks */
#define qb2kb(x) ((x) << (QUOTABLOCK_BITS-10))
#define kb2qb(x) ((x) >> (QUOTABLOCK_BITS-10))
#define toqb(x) (((x) + QUOTABLOCK_SIZE - 1) >> QUOTABLOCK_BITS)

/*
 * Command definitions for the 'quotactl' system call.
 * The commands are broken into a main command defined below
 * and a subcommand that is used to convey the type of
 * quota that is being manipulated (see above).
 */
#define SUBCMDMASK  0x00ff
#define SUBCMDSHIFT 8
#define QCMD(cmd, type)  (((cmd) << SUBCMDSHIFT) | ((type) & SUBCMDMASK))

#define Q_QUOTAON  0x0100	/* enable quotas */
#define Q_QUOTAOFF 0x0200	/* disable quotas */
#define Q_SYNC     0x0600	/* sync disk copy of a filesystems quotas */
#define Q_GETSTATS 0x1100	/* get collected stats */

struct dqstats {
	u_int32_t lookups;
	u_int32_t drops;
	u_int32_t reads;
	u_int32_t writes;
	u_int32_t cache_hits;
	u_int32_t allocated_dquots;
	u_int32_t free_dquots;
	u_int32_t syncs;
	u_int32_t version;
};

/* Ioctl for getting quota size */
#include <sys/ioctl.h>
#ifndef FIOQSIZE
	#if defined(__alpha__) || defined(__powerpc__) || defined(__sh__) || defined(__sparc__) || defined(__sparc64__)
		#define FIOQSIZE _IOR('f', 128, loff_t)
	#elif defined(__arm__) || defined(__mc68000__) || defined(__s390__)
		#define FIOQSIZE 0x545E
        #elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__ia64__)
		#define FIOQSIZE 0x5460
	#elif defined(__mips__) || defined(__mips64__)
		#define FIOQSIZE 0x6667
	#endif
#endif


long quotactl __P((int, const char *, qid_t, caddr_t));

#endif /* _QUOTA_ */
