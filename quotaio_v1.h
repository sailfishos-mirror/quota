/*
 *	Headerfile for old quotafile format
 */

#ifndef _QUOTAIO_V1_H
#define _QUOTAIO_V1_H

#include <sys/types.h>

#define V1_DQBLK_SIZE_BITS 10
#define V1_DQBLK_SIZE (1 << V1_DQBLK_SIZE_BITS)	/* Size of one quota block in bytes in old format */

#define V1_DQOFF(__id) ((loff_t) ((__id) * sizeof(struct v1_disk_dqblk)))

/* Structure of quota on disk */
struct v1_disk_dqblk {
	u_int32_t dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	u_int32_t dqb_bsoftlimit;	/* preferred limit on disk blks */
	u_int32_t dqb_curblocks;	/* current block count */
	u_int32_t dqb_ihardlimit;	/* maximum # allocated inodes */
	u_int32_t dqb_isoftlimit;	/* preferred inode limit */
	u_int32_t dqb_curinodes;	/* current # allocated inodes */
	time_t dqb_btime;	/* time limit for excessive disk use */
	time_t dqb_itime;	/* time limit for excessive files */
} __attribute__ ((packed));

/* Structure used for communication with kernel */
struct v1_kern_dqblk {
	u_int32_t dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	u_int32_t dqb_bsoftlimit;	/* preferred limit on disk blks */
	u_int32_t dqb_curblocks;	/* current block count */
	u_int32_t dqb_ihardlimit;	/* maximum # allocated inodes */
	u_int32_t dqb_isoftlimit;	/* preferred inode limit */
	u_int32_t dqb_curinodes;	/* current # allocated inodes */
	time_t dqb_btime;	/* time limit for excessive disk use */
	time_t dqb_itime;	/* time limit for excessive files */
};

#endif
