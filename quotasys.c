/*
 *
 *	Interactions of quota with system - filenames, fstab and so on...
 *
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pot.h"
#include "bylabel.h"
#include "common.h"
#include "quotasys.h"
#include "quotaio.h"
#include "dqblk_v1.h"
#include "dqblk_v2.h"
#include "dqblk_xfs.h"

#define min(x,y) (((x) < (y)) ? (x) : (y))
#define CORRECT_FSTYPE(type) \
((!strcmp(type, MNTTYPE_EXT2)) || \
(!strcmp(type, MNTTYPE_EXT3)) || \
(!strcmp(type, MNTTYPE_MINIX)) || \
(!strcmp(type, MNTTYPE_UFS)) || \
(!strcmp(type, MNTTYPE_UDF)) || \
(!strcmp(type, MNTTYPE_REISER)) || \
(!strcmp(type, MNTTYPE_XFS)))

static char extensions[MAXQUOTAS + 2][20] = INITQFNAMES;
static char *basenames[] = INITQFBASENAMES;

/*
 *	Convert type of quota to written representation
 */
char *type2name(int type)
{
	return extensions[type];
}

/*
 *	Convert name to uid
 */
uid_t user2uid(char *name)
{
	struct passwd *entry;
	uid_t ret;
	char *errch;

	ret = strtol(name, &errch, 0);
	if (!*errch)		/* Is name number - we got directly uid? */
		return ret;
	if (!(entry = getpwnam(name))) {
		fprintf(stderr, _("User %s doesn't exist.\n"), name);
		exit(1);
	}
	return entry->pw_uid;
}

/*
 *	Convert group name to gid
 */
gid_t group2gid(char *name)
{
	struct group *entry;
	gid_t ret;
	char *errch;

	ret = strtol(name, &errch, 0);
	if (!*errch)		/* Is name number - we got directly gid? */
		return ret;
	if (!(entry = getgrnam(name))) {
		fprintf(stderr, _("Group %s doesn't exist.\n"), name);
		exit(1);
	}
	return entry->gr_gid;
}

/*
 *	Convert name to id
 */
int name2id(char *name, int qtype)
{
	if (qtype == USRQUOTA)
		return user2uid(name);
	else
		return group2gid(name);
}

/*
 *	Convert uid to name
 */
void uid2user(uid_t id, char *buf)
{
	struct passwd *entry;

	if (!(entry = getpwuid(id)))
		sprintf(buf, "#%u", (uint) id);
	else
		sstrncpy(buf, entry->pw_name, MAXNAMELEN);
}

/*
 *	Convert gid to name
 */
void gid2group(gid_t id, char *buf)
{
	struct group *entry;

	if (!(entry = getgrgid(id)))
		sprintf(buf, "#%u", (uint) id);
	else
		sstrncpy(buf, entry->gr_name, MAXNAMELEN);
}

/*
 *	Convert id to user/groupname
 */
void id2name(int id, int qtype, char *buf)
{
	if (qtype == USRQUOTA)
		uid2user(id, buf);
	else
		gid2group(id, buf);
}

/*
 *	Convert quota format name to number
 */
int name2fmt(char *str)
{
	if (!strcmp(str, _("vfsold")))	/* Old quota format */
		return QF_VFSOLD;
	if (!strcmp(str, _("vfsv0")))	/* New quota format */
		return QF_VFSV0;
	if (!strcmp(str, _("rpc")))	/* RPC quota calls */
		return QF_RPC;
	fprintf(stderr, _("Unknown quota format: %s\nSupported formats are:\n\
  vfsold - original quota format\n\
  vfsv0 - new quota format\n\
  rpc - use RPC calls\n"), str);
	return QF_ERROR;
}

/*
 * Convert time difference of seconds and current time
 */
void difftime2str(time_t seconds, char *buf)
{
	time_t now;

	buf[0] = 0;
	if (!seconds)
		return;
	time(&now);
	if (seconds <= now) {
		strcpy(buf, _("none"));
		return;
	}
	time2str(seconds - now, buf, TF_ROUND);
}

/*
 * Convert time to printable form
 */
void time2str(time_t seconds, char *buf, int flags)
{
	uint minutes, hours, days;

	minutes = (seconds + 30) / 60;	/* Rounding */
	hours = minutes / 60;
	minutes %= 60;
	days = hours / 24;
	hours %= 24;
	if (flags & TF_ROUND) {
		if (days >= 2)
			sprintf(buf, _("%ddays"), days);
		else
			sprintf(buf, _("%02d:%02d"), hours + days * 24, minutes);
	}
	else {
		if (minutes || (!minutes && !hours && !days))
			sprintf(buf, _("%uminutes"), (uint) (seconds + 30) / 60);
		else if (hours)
			sprintf(buf, _("%uhours"), hours + days * 24);
		else
			sprintf(buf, _("%udays"), days);
	}
}

/*
 *	Check to see if a particular quota is to be enabled (filesystem mounted with proper option)
 */
int hasquota(struct mntent *mnt, int type)
{
	char *option;

	if (!CORRECT_FSTYPE(mnt->mnt_type))
		return 0;
	
	option = hasmntopt(mnt, MNTOPT_USRQUOTA);
	if ((type == USRQUOTA) && (option || !strcmp(mnt->mnt_type, MNTTYPE_XFS)))
		return 1;
	option = hasmntopt(mnt, MNTOPT_GRPQUOTA);
	if ((type == GRPQUOTA) && (option || !strcmp(mnt->mnt_type, MNTTYPE_XFS)))
		return 1;
	option = hasmntopt(mnt, MNTOPT_QUOTA);
	if ((type == USRQUOTA) && (option || !strcmp(mnt->mnt_type, MNTTYPE_XFS)))
		return 1;
	return 0;
}

/* Check whether quotafile for given format exists - return its name in namebuf */
static int check_fmtfile_exists(struct mntent *mnt, int type, int fmt, char *namebuf)
{
	struct stat buf;

	sprintf(namebuf, "%s/%s.%s", mnt->mnt_dir, basenames[fmt], extensions[type]);
	if (!stat(namebuf, &buf))
		return 1;
	if (errno != ENOENT) {
		fprintf(stderr, "Can't stat quotafile %s: %s\n", namebuf, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *	Get quotafile name for given entry; "" means format has no quota
 *	Note that formats without quotafile *must* be detected prior to calling this function
 */
char *get_qf_name(struct mntent *mnt, int type, int fmt)
{
	char *option, *pathname, has_quota_file_definition = 0;
	char qfullname[PATH_MAX] = "";

	if ((type == USRQUOTA) && (option = hasmntopt(mnt, MNTOPT_USRQUOTA))) {
		if (*(pathname = option + strlen(MNTOPT_USRQUOTA)) == '=')
			has_quota_file_definition = 1;
	}
	else if ((type == GRPQUOTA) && (option = hasmntopt(mnt, MNTOPT_GRPQUOTA))) {
		if (*(pathname = option + strlen(MNTOPT_GRPQUOTA)) == '=')
			has_quota_file_definition = 1;
	}
	else if ((type == USRQUOTA) && (option = hasmntopt(mnt, MNTOPT_QUOTA))) {
		if (*(pathname = option + strlen(MNTOPT_QUOTA)) == '=')
			has_quota_file_definition = 1;
	}
	else
		return NULL;

	if (has_quota_file_definition) {
		if ((option = strchr(++pathname, ',')))
			strncpy(qfullname, pathname, min((option - pathname), sizeof(qfullname)));
		else
			strncpy(qfullname, pathname, sizeof(qfullname));
	}
	else if (fmt == -1) {	/* Should guess quota format? */
		int ret;

		if ((ret = check_fmtfile_exists(mnt, type, QF_VFSV0, qfullname)) == -1)
			return NULL;
		if (ret)
			fmt = QF_VFSV0;
		else {
			if ((ret = check_fmtfile_exists(mnt, type, QF_VFSOLD, qfullname)) == -1)
				return NULL;
			if (ret)
				fmt = QF_VFSOLD;
		}
		if (fmt == -1)
			return NULL;
	}
	else if (basenames[fmt][0])	/* Any name specified? */
		sprintf(qfullname, "%s/%s.%s", mnt->mnt_dir, basenames[fmt], extensions[type]);

	return sstrdup(qfullname);
}

/*
 *	Create NULL terminated list of quotafile handles from given list of mountpoints
 *	List of zero length means scan all entries in /etc/mtab
 */
struct quota_handle **create_handle_list(int count, char **mntpoints, int type, int fmt,
					 char local_only)
{
	FILE *mntf;
	struct mntent *mnt;
	int i, gotmnt = 0;
	static struct quota_handle *hlist[MAXMNTPOINTS];
	const char *dev;

	if (!(mntf = setmntent(MOUNTED, "r")))
		die(2, _("Can't open %s: %s\n"), MOUNTED, strerror(errno));
	while ((mnt = getmntent(mntf))) {
		if (!(dev = get_device_name(mnt->mnt_fsname)))
			continue;
		for (i = 0; i < gotmnt && strcmp(dev, hlist[i]->qh_quotadev); i++);
		/* We already have this device? (can happen when filesystem is mounted multiple times */
		if (i < gotmnt)
			continue;
		for (i = 0; i < count; i++)
			/* Is this what we want? */
			if (!strcmp(dev, mntpoints[i]) || !strcmp(mnt->mnt_dir, mntpoints[i]))
				break;
		free((char *)dev);
		if (!count || i < count) {
			if (strcmp(mnt->mnt_type, MNTTYPE_NFS)) {	/* No NFS? */
				if (gotmnt == MAXMNTPOINTS)
					die(3, _("Too many mountpoints. Please report to: %s\n"),
					    MY_EMAIL);
				if (!(hlist[gotmnt] = init_io(mnt, type, fmt)))
					continue;
				gotmnt++;
			}
			else if (!local_only) {	/* Use NFS? */
#ifdef RPC
				if (gotmnt == MAXMNTPOINTS)
					die(3, _("Too many mountpoints. Please report to: %s\n"),
					    MY_EMAIL);
				if (!(hlist[gotmnt] = init_io(mnt, type, 0)))
					continue;
				gotmnt++;
#endif
			}
		}
	}
	endmntent(mntf);
	hlist[gotmnt] = NULL;
	if (count && gotmnt != count)
		die(1, _("Not all specified mountpoints are using quota.\n"));
	return hlist;
}

/*
 *	Free given list of handles
 */
int dispose_handle_list(struct quota_handle **hlist)
{
	int i, ret;

	for (i = 0; hlist[i]; i++)
		if ((ret = end_io(hlist[i])))
			fprintf(stderr, _("Error while releasing file on %s\n"),
				hlist[i]->qh_quotadev);
	return 0;
}

/*
 *	Check kernel quota version
 */

#define KERN_KNOWN_QUOTA_VERSION (6*10000 + 5*100 + 0)

int kern_quota_format(void)
{
	struct dqstats stats;
	int ret = 0;
	struct stat st;

	if (!stat("/proc/fs/xfs/stat", &st))
		ret |= (1 << QF_XFS);
	if (quotactl(QCMD(Q_GETSTATS, 0), NULL, 0, (void *)&stats) < 0) {
		if (errno == ENOSYS || errno == ENOTSUP)	/* Quota not compiled? */
			return QF_ERROR;
		if (errno == EINVAL || errno == EFAULT || errno == EPERM)	/* Old quota compiled? */
			return ret | (1 << QF_VFSOLD);
		die(4, "Error while detecting kernel quota version: %s\n", strerror(errno));
	}
	/* We might do some more generic checks in future but this should be enough for now */
	if (stats.version > KERN_KNOWN_QUOTA_VERSION)	/* Newer kernel than we know? */
		return QF_TOONEW;
	return ret | (1 << QF_VFSV0);	/* New format supported */
}

/*
 *	Warn about too new kernel
 */
void warn_new_kernel(int fmt)
{
	if (fmt == -1 && kern_quota_format() == QF_TOONEW)
		fprintf(stderr,
			_
			("Warning: Kernel quota is newer than supported. Quotafile used by utils need not be the one used by kernel.\n"));
}

/* Check whether old quota is turned on on given device */
static int v1_kern_quota_on(const char *dev, int type)
{
	char tmp[1024];		/* Just temporary buffer */

	if (!quotactl(QCMD(Q_V1_GETQUOTA, type), dev, 0, tmp))	/* OK? */
		return 1;
	return 0;
}

/* Check whether new quota is turned on on given device */
static int v2_kern_quota_on(const char *dev, int type)
{
	char tmp[1024];		/* Just temporary buffer */

	if (!quotactl(QCMD(Q_V2_GETINFO, type), dev, 0, tmp))	/* OK? */
		return 1;
	return 0;
}

/* Check whether XFS quota is turned on on given device */
static int xfs_kern_quota_on(const char *dev, int type)
{
	struct xfs_mem_dqinfo info;

	if (!quotactl(QCMD(Q_XFS_GETQSTAT, type), dev, 0, (void *)&info)) {
		if (type == USRQUOTA && info.qs_flags & XFS_QUOTA_UDQ_ACCT)
			return 1;
		else if (type == GRPQUOTA && info.qs_flags & XFS_QUOTA_GDQ_ACCT)
			return 1;
	}
	return 0;
}

/*
 *	Check whether is quota turned on on given device for given type
 */
int kern_quota_on(const char *dev, int type, int fmt)
{
	/* Check whether quota is turned on... */
	if ((fmt & (1 << QF_VFSV0)) && v2_kern_quota_on(dev, type))	/* New quota format */
		return QF_VFSV0;
	if ((fmt & (1 << QF_XFS)) && xfs_kern_quota_on(dev, type))	/* XFS quota format */
		return QF_XFS;
	if ((fmt & (1 << QF_VFSOLD)) && v1_kern_quota_on(dev, type))	/* Old quota format */
		return QF_VFSOLD;
	return -1;
}
