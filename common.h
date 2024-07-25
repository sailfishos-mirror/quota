/*
 *
 *	Various things common for all utilities
 *
 */

#ifndef GUARD_COMMON_H
#define GUARD_COMMON_H

#include <time.h>
#include <stdarg.h>

#ifndef __attribute__
# if !defined __GNUC__ || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8) || __STRICT_ANSI__
#  define __attribute__(x)
# endif
#endif

/* Name of current program for error reporting */
extern char *progname;

/* Finish programs being */
void __attribute ((noreturn)) die(int, char *, ...) __attribute__ ((__format__ (__printf__, 2, 3)));

/* Print error from va_list */
void errstrv(char *, va_list);

/* Print an error */
void errstr(char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

/* If use_syslog is called, all error reports using errstr() and die() are
 * written to syslog instead of stderr */
void use_syslog();

/* malloc() with error check */
void *smalloc(size_t);

/* realloc() with error check */
void *srealloc(void *, size_t);

/* Safe strncpy - always finishes string */
void sstrncpy(char *, const char *, size_t);

/* Safe strncat - always finishes string */
void sstrncat(char *, const char *, size_t);

/* Safe version of strdup() */
char *sstrdup(const char *s);

/* Print version string */
void version(void);

/* Desired output unit */
enum s2s_unit {
	S2S_NONE = 0,
	S2S_KB,
	S2S_MB,
	S2S_GB,
	S2S_TB,
	S2S_AUTO,
	S2S_INVALID
};

/* Compare two times */
int timespec_cmp(struct timespec *a, struct timespec *b);

/* Convert command line option to desired output unit */
int unitopt2unit(char *opt, enum s2s_unit *space_unit, enum s2s_unit *inode_unit);

#define MAXNAMELEN 64		/* Maximal length of user/group name */

struct fs_project {
	unsigned int pr_id;
	char *pr_name;
};

/* Rewind /etc/projid to the beginning */
void setprent(void);

/* Close /etc/projid file */
void endprent(void);

/* Get next entry in /etc/projid */
struct fs_project *getprent(void);

/* Convert username to uid */
uid_t user2uid(char *, int flag, int *err);

/* Convert groupname to gid */
gid_t group2gid(char *, int flag, int *err);

/* Convert project name to project id */
unsigned int project2pid(char *name, int flag, int *err);

/* Convert uid to username */
int uid2user(uid_t, char *);

/* Convert gid to groupname */
int gid2group(gid_t, char *);

/* Convert project id to name */
int pid2project(unsigned int, char *);

/* Possible default passwd handling */
#define PASSWD_FILES 0
#define PASSWD_DB 1
/* Parse /etc/nsswitch.conf and return type of default passwd handling */
int passwd_handling(void);

#endif /* GUARD_COMMON_H */
