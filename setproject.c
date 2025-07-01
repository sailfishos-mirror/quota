
#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/fs.h>

#ifndef FS_IOC_FSGETXATTR
#define FS_IOC_FSGETXATTR		_IOR ('X', 31, struct fsxattr)
#define FS_IOC_FSSETXATTR		_IOW ('X', 32, struct fsxattr)

struct fsxattr {
	__u32		fsx_xflags;	/* xflags field value (get/set) */
	__u32		fsx_extsize;	/* extsize field value (get/set)*/
	__u32		fsx_nextents;	/* nextents field value (get)	*/
	__u32		fsx_projid;	/* project identifier (get/set) */
	__u32		fsx_cowextsize;	/* CoW extsize field value (get/set)*/
	unsigned char	fsx_pad[8];
};
#endif

#ifndef BCHFS_IOC_REINHERIT_ATTRS
#define BCHFS_IOC_REINHERIT_ATTRS	_IOR(0xbc, 64, const char *)
#endif

#include "pot.h"
#include "common.h"
#include "quotasys.h"

char *progname;

static void do_setproject(int fd, unsigned id)
{
	struct fsxattr fa;

	if (ioctl(fd, FS_IOC_FSGETXATTR, &fa))
		die(1, _("FS_IOC_FSGETXATTR: %m\n"));

	fa.fsx_projid = id;

	if (ioctl(fd, FS_IOC_FSSETXATTR, &fa))
		die(1, _("FS_IOC_FSSETXATTR: %m\n"));
}

static void setproject_recurse(int dirfd, unsigned id, unsigned dev)
{
	DIR *dir = fdopendir(dirfd);
	struct dirent *d;

	while ((errno = 0), (d = readdir(dir))) {
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;

		struct stat st;
		if (fstatat(dirfd, d->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
			errstr(_("error statting %s: %m"), d->d_name);
			errno = 0;
			continue;
		}

		if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))
			continue;

		int fd = openat(dirfd, d->d_name, O_NOFOLLOW);
		if (fd < 0) {
			errstr(_("error opening %s: %m"), d->d_name);
			errno = 0;
			continue;
		}

		if (st.st_dev != dev ||
		    ioctl(dirfd, BCHFS_IOC_REINHERIT_ATTRS, d->d_name) < 0)
			do_setproject(fd, id);

		if (S_ISDIR(st.st_mode))
			setproject_recurse(fd, id, st.st_dev);

		close(fd);
	}
}

static void setproject(const char *path, unsigned id)
{
	int fd = open(path, O_RDONLY|O_NOATIME);
	if (fd < 0) {
		errstr(_("error opening %s: %m"), path);
		return;
	}

	struct stat st;
	if (fstat(fd, &st)) {
		errstr(_("error statting %s: %m"), path);
		return;
	}

	if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))
		return;

	do_setproject(fd, id);

	if (S_ISDIR(st.st_mode))
		setproject_recurse(fd, id, st.st_dev);

	close(fd);
}

static void usage(void)
{
	errstr("Utility for setting project IDs on a directory subtree.\n%s%s",
		_("Usage: setproject -P project files...\n"),
		_("\n\
-P, --project                 project name\n\
-h, --help                    display this help message and exit\n\
-V, --version                 display version information and exit\n\n"));
	fprintf(stderr, _("Bugs to: %s\n"), PACKAGE_BUGREPORT);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct option long_opts[] = {
		{ "project",	1, NULL, 'P' },
		{ "help",	0, NULL, 'h' },
		{ "version",	0, NULL, 'V' },
		{ NULL,		0, NULL,  0  }
	};
	char *project_name = NULL;
	unsigned int project_id;
	int ret, i;

	gettexton();
	progname = basename(argv[0]);

	while ((ret = getopt_long(argc, argv, "P:c", long_opts, NULL)) != -1) {
		switch (ret) {
		case 'P':
			project_name = optarg;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (!project_name)
		die(1, _("No project specified.\n"));

	project_id = project2pid(project_name, 0, NULL);
	for (i = 0; i < argc; i++)
		setproject(argv[i], project_id);

	return 0;
}
