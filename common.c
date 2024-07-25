/*
 *
 *	Common things for all utilities
 *
 *	Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 *
 *      Jani Jaakkola <jjaakkol@cs.helsinki.fi> - syslog support
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pot.h"
#include "common.h"

static int enable_syslog=0;

void use_syslog(void)
{
	openlog(progname,0,LOG_DAEMON);
	enable_syslog=1;
}

static void do_syslog(int level, const char *format, va_list args)
{
	char buf[1024];
	int i, j;
	
	vsnprintf(buf,sizeof(buf),format,args);
	/* This while removes newlines from the log, so that
	 * syslog() will be called once for every line */
	for (i = 0; buf[i]; i = j) {
		for (j = i; buf[j] && buf[j] != '\n'; j++);
		if (buf[j] == '\n')
			buf[j++] = '\0';
		syslog(level, "%s", buf + i);
	}
}

void die(int ret, char *fmtstr, ...)
{
	va_list args;

	va_start(args, fmtstr);
	if (enable_syslog) {
		do_syslog(LOG_CRIT, fmtstr, args);
		syslog(LOG_CRIT, "Exiting with status %d", ret);
	} else {
		fprintf(stderr, "%s: ", progname);
		vfprintf(stderr, fmtstr, args);
	}
	va_end(args);
	exit(ret);
}

void errstrv(char *fmtstr, va_list args)
{
	if (enable_syslog)
		do_syslog(LOG_ERR, fmtstr, args);
	else {
		fprintf(stderr, "%s: ", progname);
		vfprintf(stderr, fmtstr, args);
	}
}

void errstr(char *fmtstr, ...)
{
	va_list args;

	va_start(args, fmtstr);
	errstrv(fmtstr, args);
	va_end(args);
}

void *smalloc(size_t size)
{
	void *ret = malloc(size);

	if (!ret) {
		fputs("Not enough memory.\n", stderr);
		exit(3);
	}
	return ret;
}

void *srealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);

	if (!ret) {
		fputs("Not enough memory.\n", stderr);
		exit(3);
	}
	return ret;
}

void sstrncpy(char *d, const char *s, size_t len)
{
	strncpy(d, s, len);
	d[len - 1] = 0;
}

void sstrncat(char *d, const char *s, size_t len)
{
	strncat(d, s, len - 1);
}

char *sstrdup(const char *s)
{
	char *r = strdup(s);

	if (!r) {
		puts("Not enough memory.");
		exit(3);
	}
	return r;
}

void version(void)
{
	printf(_("Quota utilities version %s.\n"), PACKAGE_VERSION);
	printf(_("Compiled with:%s\n"), COMPILE_OPTS);
	printf(_("Bugs to %s\n"), PACKAGE_BUGREPORT);
}

int timespec_cmp(struct timespec *a, struct timespec *b)
{
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_nsec < b->tv_nsec)
		return -1;
	if (a->tv_nsec > b->tv_nsec)
		return 1;
	return 0;
}

static enum s2s_unit unitstring2unit(char *opt)
{
	char unitchar;
	char *unitstring = "kmgt";
	int i, len;

	len = strlen(opt);
	if (!len)
		return S2S_NONE;
	if (len > 1)
		return S2S_INVALID;
	unitchar = tolower(*opt);
	for (i = 0; i < strlen(unitstring); i++)
		if (unitchar == unitstring[i])
			break;
	if (i >= strlen(unitstring))
		return S2S_INVALID;
	return S2S_KB + i;
}

int unitopt2unit(char *opt, enum s2s_unit *space_unit, enum s2s_unit *inode_unit)
{
	char *sep;

	sep = strchr(opt, ',');
	if (!sep)
		return -1;
	*sep = 0;
	*space_unit = unitstring2unit(opt);
	if (*space_unit == S2S_INVALID)
		return -1;
	*inode_unit = unitstring2unit(sep + 1);
	if (*inode_unit == S2S_INVALID)
		return -1;
	return 0;
}

/*
 *	Project quota handling
 */
#define PROJECT_FILE "/etc/projid"
#define MAX_LINE_LEN 1024

static FILE *project_file;

/* Rewind /etc/projid to the beginning */
void setprent(void)
{
	if (project_file)
		fclose(project_file);
	project_file = fopen(PROJECT_FILE, "r");
}

/* Close /etc/projid file */
void endprent(void)
{
	if (project_file) {
		fclose(project_file);
		project_file = NULL;
	}
}

/* Get next entry in /etc/projid */
struct fs_project *getprent(void)
{
	static struct fs_project p;
	static char linebuf[MAX_LINE_LEN];
	char *idstart, *idend;

	if (!project_file)
		return NULL;
	while (fgets(linebuf, MAX_LINE_LEN, project_file)) {
		/* Line too long? */
		if (linebuf[strlen(linebuf) - 1] != '\n')
			continue;
		/* Skip comments */
		if (linebuf[0] == '#')
			continue;
		idstart = strchr(linebuf, ':');
		/* Skip invalid lines... We follow what xfs_quota does */
		if (!idstart)
			continue;
		*idstart = 0;
		idstart++;
		/*
		 * Colon can separate name from something else - follow what
		 * xfs_quota does
		 */
		idend = strchr(idstart, ':');
		if (idend)
			*idend = 0;
		p.pr_name = linebuf;
		p.pr_id = strtoul(idstart, NULL, 10);
		return &p;
	}
	return NULL;
}

static struct fs_project *get_project_by_name(char *name)
{
	struct fs_project *p;

	setprent();
	while ((p = getprent())) {
		if (!strcmp(name, p->pr_name))
			break;
	}
	endprent();

	return p;
}

static struct fs_project *get_project_by_id(unsigned int id)
{
	struct fs_project *p;

	setprent();
	while ((p = getprent())) {
		if (p->pr_id == id)
			break;
	}
	endprent();

	return p;
}

/*
 *	Convert name to uid
 */
uid_t user2uid(char *name, int flag, int *err)
{
	struct passwd *entry;
	uid_t ret;
	char *errch;

	if (err)
		*err = 0;
	if (!flag) {
		ret = strtoul(name, &errch, 0);
		if (!*errch)		/* Is name number - we got directly uid? */
			return ret;
	}
	if (!(entry = getpwnam(name))) {
		if (!err) {
			errstr(_("user %s does not exist.\n"), name);
			exit(1);
		}
		else {
			*err = -1;
			return 0;
		}
	}
	return entry->pw_uid;
}

/*
 *	Convert group name to gid
 */
gid_t group2gid(char *name, int flag, int *err)
{
	struct group *entry;
	gid_t ret;
	char *errch;

	if (err)
		*err = 0;
	if (!flag) {
		ret = strtoul(name, &errch, 0);
		if (!*errch)		/* Is name number - we got directly gid? */
			return ret;
	}
	if (!(entry = getgrnam(name))) {
		if (!err) {
			errstr(_("group %s does not exist.\n"), name);
			exit(1);
		}
		else {
			*err = -1;
			return 0;
		}
	}
	return entry->gr_gid;
}

/*
 *	Convert project name to project id
 */
unsigned int project2pid(char *name, int flag, int *err)
{
	int ret;
	char *errch;
	struct fs_project *p;

	if (err)
		*err = 0;
	if (!flag) {
		ret = strtoul(name, &errch, 0);
		if (!*errch)		/* Is name number - we got directly pid? */
			return ret;
	}
	p = get_project_by_name(name);
	if (!p) {
		if (!err) {
			errstr(_("project %s does not exist.\n"), name);
			exit(1);
		}
		else {
			*err = -1;
			return 0;
		}
	}
	return p->pr_id;
}

/*
 *	Convert uid to name
 */
int uid2user(uid_t id, char *buf)
{
	struct passwd *entry;

	if (!(entry = getpwuid(id))) {
		snprintf(buf, MAXNAMELEN, "#%u", (uint) id);
		return 1;
	}
	else
		sstrncpy(buf, entry->pw_name, MAXNAMELEN);
	return 0;
}

/*
 *	Convert gid to name
 */
int gid2group(gid_t id, char *buf)
{
	struct group *entry;

	if (!(entry = getgrgid(id))) {
		snprintf(buf, MAXNAMELEN, "#%u", (uint) id);
		return 1;
	}
	else
		sstrncpy(buf, entry->gr_name, MAXNAMELEN);
	return 0;
}

/*
 *	Convert project id to name
 */
int pid2project(unsigned int id, char *buf)
{
	struct fs_project *p;

	if (!(p = get_project_by_id(id))) {
		snprintf(buf, MAXNAMELEN, "#%u", (uint) id);
		return 1;
	}
	else
		sstrncpy(buf, p->pr_name, MAXNAMELEN);
	return 0;
}

/*
 *	Parse /etc/nsswitch.conf and return type of default passwd handling
 */
int passwd_handling(void)
{
	FILE *f;
	char buf[1024], *colpos, *spcpos;
	int ret = PASSWD_FILES;

	if (!(f = fopen("/etc/nsswitch.conf", "r")))
		return PASSWD_FILES;	/* Can't open nsswitch.conf - fallback on compatible mode */
	while (fgets(buf, sizeof(buf), f)) {
		if (strncmp(buf, "passwd:", 7))	/* Not passwd entry? */
			continue;
		for (colpos = buf+7; isspace(*colpos); colpos++);
		if (!*colpos)	/* Not found any type of handling? */
			break;
		for (spcpos = colpos; !isspace(*spcpos) && *spcpos; spcpos++);
		*spcpos = 0;
		if (!strcmp(colpos, "db") || !strcmp(colpos, "nis") || !strcmp(colpos, "nis+"))
			ret = PASSWD_DB;
		break;
	}
	fclose(f);
	return ret;
}
