/*
 *
 *	Headerfile of quota interactions with system - filenames, fstab...
 *
 */

#ifndef _QUOTASYS_H
#define _QUOTASYS_H

#include <sys/types.h>
#include "mntopt.h"
#include "quota.h"

#define MAXNAMELEN 64		/* Maximal length of user/group name */
#define MAXTIMELEN 40		/* Maximal length of time string */
#define MAXNUMLEN 32		/* Maximal length of number */
#define MAXMNTPOINTS 128	/* Maximal number of mountpoints with quota */

/* Flags for formatting time */
#define TF_ROUND 0x1		/* Should be printed time rounded? */

/* Flags for IO initialization */
#define IOI_LOCALONLY	0x1	/* Operate only on local quota */
#define IOI_READONLY	0x2	/* Only readonly access */
#define IOI_OPENFILE	0x4	/* Open file even if kernel has quotas turned on */

/*
 *	Exported functions
 */
/* Convert quota type to written form */
char *type2name(int);

/* Convert username to uid */
uid_t user2uid(char *);

/* Convert groupname to gid */
gid_t group2gid(char *);

/* Convert user/groupname to id */
int name2id(char *name, int qtype);

/* Convert uid to username */
void uid2user(uid_t, char *);

/* Convert gid to groupname */
void gid2group(gid_t, char *);

/* Convert id to user/group name */
void id2name(int id, int qtype, char *buf);

/* Convert quota format name to number */
int name2fmt(char *str);

/* Convert quota format number to name */
char *fmt2name(int fmt);

/* Convert time difference between given time and current time to printable form */
void difftime2str(time_t, char *);

/* Convert time to printable form */
void time2str(time_t, char *, int);

/* Convert number in quota blocks to short printable form */
void space2str(qsize_t, char *, int);

/* Convert number to short printable form */
void number2str(unsigned long long, char *, int);

/* Check to see if particular quota is to be enabled */
int hasquota(struct mntent *mnt, int type);

/* Get quotafile name for given entry */
char *get_qf_name(struct mntent *mnt, int type, int fmt);

/* Create NULL-terminated list of handles for quotafiles for given mountpoints */
struct quota_handle **create_handle_list(int count, char **mntpoints, int type, int fmt,
					 int flags);
/* Dispose given list of handles */
int dispose_handle_list(struct quota_handle **hlist);

/* Check whether given device name matches quota handle device */
int devcmp_handle(const char *dev, struct quota_handle *h);

/* Check whether two quota handles have same device */
int devcmp_handles(struct quota_handle *a, struct quota_handle *b);

/* Warn about too new kernel */
void warn_new_kernel(int fmt);

/* Check kernel supported quotafile format */
int kern_quota_format(void);

/* Check whether is quota turned on on given device for given type */
int kern_quota_on(const char *dev, int type, int fmt);

/* Initialize mountpoints scan */
int init_mounts_scan(int dcnt, char **dirs);

/* Return next mountpoint for scan */
struct mntent *get_next_mount(void);

/* Free all structures associated with mountpoints scan */
void end_mounts_scan(void);

#endif /* _QUOTASYS_H */
