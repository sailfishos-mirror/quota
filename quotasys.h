/*
 *
 *	Headerfile of quota interactions with system - filenames, fstab...
 *
 */

#ifndef _QUOTASYS_H
#define _QUOTASYS_H

#include <sys/types.h>
#include "mntopt.h"

#define MAXNAMELEN 64		/* Maximal length of user/group name */
#define MAXTIMELEN 40		/* Maximal length of time string */
#define MAXMNTPOINTS 128	/* Maximal number of processed mountpoints per one run */

/* Flags for formatting time */
#define TF_ROUND 0x1		/* Should be printed time rounded? */

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

/* Convert time difference between given time and current time to printable form */
void difftime2str(time_t, char *);

/* Convert time to printable form */
void time2str(time_t, char *, int);

/* Check to see if particular quota is to be enabled */
int hasquota(struct mntent *mnt, int type);

/* Get quotafile name for given entry */
char *get_qf_name(struct mntent *mnt, int type, int fmt);

/* Create NULL-terminated list of handles for quotafiles for given mountpoints */
struct quota_handle **create_handle_list(int count, char **mntpoints, int type, int fmt,

					 char local_only);
/* Dispose given list of handles */
int dispose_handle_list(struct quota_handle **hlist);

/* Warn about too new kernel */
void warn_new_kernel(int fmt);

/* Check kernel supported quotafile format */
int kern_quota_format(void);

/* Check whether is quota turned on on given device for given type */
int kern_quota_on(const char *dev, int type, int fmt);

#endif /* _QUOTASYS_H */
