/*
 *
 *	Interactions of quota with system - filenames, fstab and so on...
 *
 *	Jan Kara <jack@suse.cz> - sponsored by SuSE CR
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
(!strcmp(type, MNTTYPE_XFS)) || \
(!strcmp(type, MNTTYPE_NFS)))

static char extensions[MAXQUOTAS + 2][20] = INITQFNAMES;
static char *basenames[] = INITQFBASENAMES;
static char *fmtnames[] = INITQFMTNAMES;

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
		errstr(_("User %s doesn't exist.\n"), name);
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
		errstr(_("Group %s doesn't exist.\n"), name);
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
		snprintf(buf, MAXNAMELEN, "#%u", (uint) id);
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
		snprintf(buf, MAXNAMELEN, "#%u", (uint) id);
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
	int fmt;

	for (fmt = 0; fmt < QUOTAFORMATS; fmt++)
		if (!strcmp(str, fmtnames[fmt]))
			return fmt;
	errstr(_("Unknown quota format: %s\nSupported formats are:\n\
  vfsold - original quota format\n\
  vfsv0 - new quota format\n\
  rpc - use RPC calls\n\
  xfs - XFS quota format\n"), str);
	return QF_ERROR;
}

/*
 *	Convert quota format number to name
 */
char *fmt2name(int fmt)
{

	if (fmt < 0)
		return _("Unknown format");
	return fmtnames[fmt];
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
			snprintf(buf, MAXTIMELEN, _("%ddays"), days);
		else
			snprintf(buf, MAXTIMELEN, _("%02d:%02d"), hours + days * 24, minutes);
	}
	else {
		if (minutes || (!minutes && !hours && !days))
			snprintf(buf, MAXTIMELEN, _("%uminutes"), (uint) (seconds + 30) / 60);
		else if (hours)
			snprintf(buf, MAXTIMELEN, _("%uhours"), hours + days * 24);
		else
			snprintf(buf, MAXTIMELEN, _("%udays"), days);
	}
}

/*
 * Convert number in quota blocks to some nice short form for printing
 */
void space2str(qsize_t space, char *buf, int format)
{
	int i;
	char suffix[8] = " MGT";

	space = qb2kb(space);
	if (format)
		for (i = 3; i > 0; i--)
			if (space >= (1LL << (QUOTABLOCK_BITS*i))*100) {
				sprintf(buf, "%Lu%c", (space+(1 << (QUOTABLOCK_BITS*i))-1) >> (QUOTABLOCK_BITS*i), suffix[i]);
				return;
			}
	sprintf(buf, "%Lu", space);
}

/*
 *  Convert number to some nice short form for printing
 */
void number2str(unsigned long long num, char *buf, int format)
{
	int i;
	unsigned long long div;
	char suffix[8] = " kmgt";

	if (format)
		for (i = 4, div = 1000000000000LL; i > 0; i--, div /= 1000)
			if (num >= 100*div) {
				sprintf(buf, "%Lu%c", (num+div-1) / div, suffix[i]);
				return;
			}
	sprintf(buf, "%Lu", num);
}

/*
 *	Check for XFS filesystem with quota accounting enabled
 */
static int hasxfsquota(struct mntent *mnt, int type)
{
	int ret = 0;
	u_int16_t sbflags;
	struct xfs_mem_dqinfo info;
	int nonrootfs = strcmp(mnt->mnt_dir, "/");
	const char *dev = get_device_name(mnt->mnt_fsname);

	if (!dev)
		return ret;

	memset(&info, 0, sizeof(struct xfs_mem_dqinfo));
	if (!quotactl(QCMD(Q_XFS_GETQSTAT, type), dev, 0, (void *)&info)) {
		sbflags = (info.qs_flags & 0xff00) >> 8;
		if (type == USRQUOTA && (info.qs_flags & XFS_QUOTA_UDQ_ACCT))
			ret = 1;
		else if (type == GRPQUOTA && (info.qs_flags & XFS_QUOTA_GDQ_ACCT))
			ret = 1;
		else if (nonrootfs)
			ret = 0;
		else if (type == USRQUOTA && (sbflags & XFS_QUOTA_UDQ_ACCT))
			ret = 1;
		else if (type == GRPQUOTA && (sbflags & XFS_QUOTA_GDQ_ACCT))
			ret = 1;
	}
	free((char *)dev);
	return ret;
}

/*
 *	Check to see if a particular quota is to be enabled (filesystem mounted with proper option)
 */
int hasquota(struct mntent *mnt, int type)
{
	char *option;

	if (!CORRECT_FSTYPE(mnt->mnt_type))
		return 0;
	
	if (!strcmp(mnt->mnt_type, MNTTYPE_XFS))
		return hasxfsquota(mnt, type);
	if (!strcmp(mnt->mnt_type, MNTTYPE_NFS))	/* NFS always has quota or better there is no good way how to detect it */
		return 1;

	if ((type == USRQUOTA) && (option = hasmntopt(mnt, MNTOPT_USRQUOTA)))
		return 1;
	if ((type == GRPQUOTA) && (option = hasmntopt(mnt, MNTOPT_GRPQUOTA)))
		return 1;
	if ((type == USRQUOTA) && (option = hasmntopt(mnt, MNTOPT_QUOTA)))
		return 1;
	return 0;
}

/* Check whether quotafile for given format exists - return its name in namebuf */
static int check_fmtfile_exists(struct mntent *mnt, int type, int fmt, char *namebuf)
{
	struct stat buf;

	snprintf(namebuf, PATH_MAX, "%s/%s.%s", mnt->mnt_dir, basenames[fmt], extensions[type]);
	if (!stat(namebuf, &buf))
		return 1;
	if (errno != ENOENT) {
		errstr(_("Can't stat quotafile %s: %s\n"),
			namebuf, strerror(errno));
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
		snprintf(qfullname, PATH_MAX, "%s/%s.%s", mnt->mnt_dir, basenames[fmt], extensions[type]);

	return sstrdup(qfullname);
}

/*
 *	Create NULL terminated list of quotafile handles from given list of mountpoints
 *	List of zero length means scan all entries in /etc/mtab
 */
struct quota_handle **create_handle_list(int count, char **mntpoints, int type, int fmt,
					 int flags)
{
	struct mntent *mnt;
	int gotmnt = 0;
	static struct quota_handle *hlist[MAXMNTPOINTS];

	if (init_mounts_scan(count, mntpoints) < 0)
		die(2, _("Can't initialize mountpoint scan.\n"));
	while ((mnt = get_next_mount())) {
		if (strcmp(mnt->mnt_type, MNTTYPE_NFS)) {	/* No NFS? */
			if (gotmnt+1 == MAXMNTPOINTS)
				die(2, _("Too many mountpoints with quota. Contact %s\n"), MY_EMAIL);
			if (!(hlist[gotmnt] = init_io(mnt, type, fmt, flags)))
				continue;
			gotmnt++;
		}
		else if (!(flags & IOI_LOCALONLY) && (fmt == -1 || fmt == QF_RPC)) {	/* Use NFS? */
#ifdef RPC
			if (gotmnt+1 == MAXMNTPOINTS)
				die(2, _("Too many mountpoints with quota. Contact %s\n"), MY_EMAIL);
			if (!(hlist[gotmnt] = init_io(mnt, type, fmt, flags)))
				continue;
			gotmnt++;
#endif
		}
	}
	end_mounts_scan();
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
	int i;

	for (i = 0; hlist[i]; i++)
		if (end_io(hlist[i]) < 0)
			errstr(_("Error while releasing file on %s\n"),
				hlist[i]->qh_quotadev);
	return 0;
}

/*
 *	Check whether given device name matches this quota handle
 */
int devcmp_handle(const char *dev, struct quota_handle *h)
{
	struct stat sbuf;

	if (stat(dev, &sbuf) < 0)
		return (strcmp(dev, h->qh_quotadev) == 0);
	if (!S_ISBLK(sbuf.st_mode))
		return (strcmp(dev, h->qh_quotadev) == 0);
	if (sbuf.st_rdev != h->qh_stat.st_rdev)
		return 0;
	return 1;
}

/*
 *	Check whether two quota handles are for the same device
 */
int devcmp_handles(struct quota_handle *a, struct quota_handle *b)
{
	if (!S_ISBLK(a->qh_stat.st_mode) || !S_ISBLK(b->qh_stat.st_mode))
		return (strcmp(a->qh_quotadev, b->qh_quotadev) == 0);
	if (a->qh_stat.st_rdev != b->qh_stat.st_rdev)
		return 0;
	return 1;
}

/*
 *	Check kernel quota version
 */

int kern_quota_format(void)
{
	struct dqstats stats;
	FILE *f;
	int ret = 0;
	struct stat st;

	if (!stat("/proc/fs/xfs/stat", &st))
		ret |= (1 << QF_XFS);
	if ((f = fopen(QSTAT_FILE, "r"))) {
		if (fscanf(f, "Version %u", &stats.version) != 1) {
			fclose(f);
			return QF_TOONEW;
		}
		fclose(f);
	}
	else if (quotactl(QCMD(Q_GETSTATS, 0), NULL, 0, (void *)&stats) < 0) {
		if (errno == ENOSYS || errno == ENOTSUP)	/* Quota not compiled? */
			return QF_ERROR;
		if (errno == EINVAL || errno == EFAULT || errno == EPERM)	/* Old quota compiled? */
			return ret | (1 << QF_VFSOLD);
		die(4, _("Error while detecting kernel quota version: %s\n"), strerror(errno));
	}
	/* We might do some more generic checks in future but this should be enough for now */
	if (stats.version > KERN_KNOWN_QUOTA_VERSION)	/* Newer kernel than we know? */
		return QF_TOONEW;
	if (stats.version <= 6*10000+4*100+0)		/* Old quota format? */
		ret |= (1 << QF_VFSOLD);
	else
		ret |= (1 << QF_VFSV0);
	return ret;	/* New format supported */
}

/*
 *	Warn about too new kernel
 */
void warn_new_kernel(int fmt)
{
	if (fmt == -1 && kern_quota_format() == QF_TOONEW)
		errstr(
			_("WARNING - Kernel quota is newer than supported. Quotafile used by utils need not be the one used by kernel.\n"));
}

/* Check whether old quota is turned on on given device */
static int v1_kern_quota_on(const char *dev, int type)
{
	char tmp[1024];		/* Just temporary buffer */
	qid_t id = (type == USRQUOTA) ? getuid() : getgid();

	if (!quotactl(QCMD(Q_V1_GETQUOTA, type), dev, id, tmp))	/* OK? */
		return 1;
	return 0;
}

/* Check whether new quota is turned on on given device */
static int v2_kern_quota_on(const char *dev, int type)
{
	char tmp[1024];		/* Just temporary buffer */
	qid_t id = (type == USRQUOTA) ? getuid() : getgid();

	if (!quotactl(QCMD(Q_V2_GETQUOTA, type), dev, id, tmp))	/* OK? */
		return 1;
	return 0;
}

/* Check whether XFS quota is turned on on given device */
static int xfs_kern_quota_on(const char *dev, int type)
{
	struct xfs_mem_dqinfo info;

	if (!quotactl(QCMD(Q_XFS_GETQSTAT, type), dev, 0, (void *)&info)) {
		if (type == USRQUOTA && (info.qs_flags & XFS_QUOTA_UDQ_ACCT))
			return 1;
		if (type == GRPQUOTA && (info.qs_flags & XFS_QUOTA_GDQ_ACCT))
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

/*
 *
 *	mtab/fstab handling routines
 *
 */

struct mount_entry {
	char *me_type;		/* Type of filesystem for given entry */
	char *me_opts;		/* Options of filesystem */
	dev_t me_dev;		/* Device filesystem is mounted on */
	ino_t me_ino;		/* Inode number of root of filesystem */
	const char *me_devname;	/* Name of device (after pass through get_device_name()) */
	const char *me_dir;	/* One of mountpoints of filesystem */
};

struct searched_dir {
	int sd_dir;		/* Is searched dir mountpoint or in fact device? */
	dev_t sd_dev;		/* Device mountpoint lies on */
	ino_t sd_ino;		/* Inode number of mountpoint */
	const char *sd_name;	/* Name of given dir/device */
};

#define ALLOC_ENTRIES_NUM 16	/* Allocate entries by this number */

static int mnt_entries_cnt;	/* Number of cached mountpoint entries */
static struct mount_entry *mnt_entries;	/* Cached mounted filesystems */
static int check_dirs_cnt, act_checked;	/* Number of dirs to check; Actual checked dir/(mountpoint in case of -a) */
static struct searched_dir *check_dirs;	/* Directories to check */

/* Cache mtab/fstab */
static int cache_mnt_table(void)
{
	FILE *mntf;
	struct mntent *mnt;
	struct stat st;
	int allocated = 0, i;
	dev_t dev;
	char mntpointbuf[PATH_MAX];

	if (!(mntf = setmntent(_PATH_MOUNTED, "r"))) {
		if (errno != ENOENT) {
			errstr(_("Can't open %s: %s\n"), _PATH_MOUNTED, strerror(errno));
			return -1;
		}
		else	/* Fallback on fstab when mtab not available */
			if (!(mntf = setmntent(_PATH_MNTTAB, "r"))) {
				errstr(_("Can't open %s: %s\n"), _PATH_MNTTAB, strerror(errno));
				return -1;
			}
	}
	mnt_entries = smalloc(sizeof(struct mount_entry) * ALLOC_ENTRIES_NUM);
	allocated += ALLOC_ENTRIES_NUM;
	while ((mnt = getmntent(mntf))) {
		const char *devname;

		if (!CORRECT_FSTYPE(mnt->mnt_type))	/* Just basic filtering */
			continue;
		if (!(devname = get_device_name(mnt->mnt_fsname))) {
			errstr(_("Can't get device name for %s\n"), mnt->mnt_fsname);
			continue;
		}
		if (!realpath(mnt->mnt_dir, mntpointbuf)) {
			errstr(_("Can't resolve mountpoint path %s: %s\n"), mnt->mnt_dir, strerror(errno));
			free((char *)devname);
			continue;
		}
		if (strcmp(mnt->mnt_type, MNTTYPE_NFS)) {
			if (stat(devname, &st) < 0) {	/* Can't stat mounted device? */
				errstr(_("Can't stat() mounted device %s: %s\n"), devname, strerror(errno));
				free((char *)devname);
				continue;
			}
			if (!S_ISBLK(st.st_mode) && !S_ISCHR(st.st_mode))
				errstr(_("Warning: Device %s is not block nor character device.\n"), devname);
			dev = st.st_rdev;
			for (i = 0; i < mnt_entries_cnt && mnt_entries[i].me_dev != dev; i++);
		}
		else {	/* Cope with network filesystems */
			dev = 0;
			for (i = 0; i < mnt_entries_cnt && strcmp(mnt_entries[i].me_devname, devname); i++);
		}
		if (i == mnt_entries_cnt) {	/* New mounted device? */
			if (stat(mnt->mnt_dir, &st) < 0) {	/* Can't stat mountpoint? We have better ignore it... */
				errstr(_("Can't stat() mountpoint %s: %s\n"), mnt->mnt_dir, strerror(errno));
				free((char *)devname);
				continue;
			}
			if (allocated == mnt_entries_cnt) {
				allocated += ALLOC_ENTRIES_NUM;
				mnt_entries = srealloc(mnt_entries, allocated * sizeof(struct mount_entry));
			}
			mnt_entries[i].me_type = sstrdup(mnt->mnt_type);
			mnt_entries[i].me_opts = sstrdup(mnt->mnt_opts);
			mnt_entries[i].me_dev = dev;
			mnt_entries[i].me_ino = st.st_ino;
			mnt_entries[i].me_devname = devname;
			mnt_entries[i].me_dir = sstrdup(mntpointbuf);
			mnt_entries_cnt++;
		}
		else 
			free((char *)devname);	/* We don't need it any more */
	}
	endmntent(mntf);
	return 0;
}

/* Process and store given paths */
static int process_dirs(int dcnt, char **dirs)
{
	struct stat st;
	int i;
	char mntpointbuf[PATH_MAX];

	check_dirs_cnt = 0;
	act_checked = -1;
	if (dcnt) {
		check_dirs = smalloc(sizeof(struct searched_dir) * dcnt);
		for (i = 0; i < dcnt; i++) {
			if (stat(dirs[i], &st) < 0) {
				errstr(_("Can't stat() given mountpoint %s: %s\nSkipping...\n"), dirs[i], strerror(errno));
				continue;
			}
			check_dirs[check_dirs_cnt].sd_dir = S_ISDIR(st.st_mode);
			if (S_ISDIR(st.st_mode)) {
				check_dirs[check_dirs_cnt].sd_dev = st.st_dev;
				check_dirs[check_dirs_cnt].sd_ino = st.st_ino;
				if (!realpath(dirs[i], mntpointbuf)) {
					errstr(_("Can't resolve path %s: %s\n"), dirs[i], strerror(errno));
					continue;
				}
			}
			else if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
				int mentry;

				check_dirs[check_dirs_cnt].sd_dev = st.st_rdev;
				for (mentry = 0; mentry < mnt_entries_cnt && mnt_entries[mentry].me_dev != st.st_rdev; mentry++);
				if (mentry == mnt_entries_cnt) {
					errstr(_("Can't find mountpoint for device %s\n"), dirs[i]);
					continue;
				}
				sstrncpy(mntpointbuf, mnt_entries[mentry].me_dir, PATH_MAX-1);
			}
			else {
				errstr(_("Specified path %s is not directory nor device.\n"), dirs[i]);
				continue;
			}
			check_dirs[check_dirs_cnt].sd_name = sstrdup(mntpointbuf);
			check_dirs_cnt++;
		}
		if (!check_dirs_cnt) {
			errstr(_("No correct mountpoint specified.\n"));
			free(check_dirs);
			return -1;
		}
	}
	return 0;
}

/*
 *	Initialize mountpoint scan
 */ 
int init_mounts_scan(int dcnt, char **dirs)
{
	if (cache_mnt_table() < 0)
		return -1;
	return process_dirs(dcnt, dirs);
}

/* Find next usable mountpoint when scanning all mountpoints */
static int find_next_entry_all(int *pos)
{
	struct mntent mnt;

restart:
	if (++act_checked == mnt_entries_cnt)
		return 0;
	mnt.mnt_fsname = (char *)mnt_entries[act_checked].me_devname;
	mnt.mnt_type = mnt_entries[act_checked].me_type;
	mnt.mnt_opts = mnt_entries[act_checked].me_opts;
	mnt.mnt_dir = (char *)mnt_entries[act_checked].me_dir;
	if (hasmntopt(&mnt, MNTOPT_NOAUTO))
		goto restart;
	*pos = act_checked;
	return 1;
}

/* Find next usable mountpoint when scanning selected mountpoints */
static int find_next_entry_sel(int *pos)
{
	int i;
	struct searched_dir *sd;

restart:
	if (++act_checked == check_dirs_cnt)
		return 0;
	sd = check_dirs + act_checked;
	for (i = 0; i < mnt_entries_cnt; i++) {
		if (sd->sd_dir) {
			if (sd->sd_dev == mnt_entries[i].me_dev && sd->sd_ino == mnt_entries[i].me_ino)
				break;
		}
		else
			if (sd->sd_dev == mnt_entries[i].me_dev)
				break;
	}
	if (i == mnt_entries_cnt) {
		errstr(_("Mountpoint (or device) %s not found.\n"), sd->sd_name);
		goto restart;
	}
	*pos = i;
	return 1;
}

/*
 *	Return next directory from the list
 */
struct mntent *get_next_mount(void)
{
	static struct mntent mnt;
	int mntpos;

	if (!check_dirs_cnt) {	/* Scan all mountpoints? */
		if (!find_next_entry_all(&mntpos))
			return NULL;
		mnt.mnt_dir = (char *)mnt_entries[mntpos].me_dir;
	}
	else {
		if (!find_next_entry_sel(&mntpos))
			return NULL;
		mnt.mnt_dir = (char *)check_dirs[act_checked].sd_name;
	}
	mnt.mnt_fsname = (char *)mnt_entries[mntpos].me_devname;
	mnt.mnt_type = mnt_entries[mntpos].me_type;
	mnt.mnt_opts = mnt_entries[mntpos].me_opts;
	return &mnt;
}

/*
 *	Free all structures allocated for mountpoint scan
 */
void end_mounts_scan(void)
{
	int i;

	for (i = 0; i < mnt_entries_cnt; i++) {
		free(mnt_entries[i].me_type);
		free(mnt_entries[i].me_opts);
		free((char *)mnt_entries[i].me_devname);
		free((char *)mnt_entries[i].me_dir);
	}
	free(mnt_entries);
	mnt_entries = NULL;
	mnt_entries_cnt = 0;
	if (check_dirs_cnt) {
		for (i = 0; i < check_dirs_cnt; i++)
			free((char *)check_dirs[i].sd_name);
		free(check_dirs);
	}
	check_dirs = NULL;
	check_dirs_cnt = 0;
}
