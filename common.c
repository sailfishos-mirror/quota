/*
 *
 *	Common things for all utilities
 *
 *	Jan Kara <jack@suse.cz> - sponsored by SuSE CR
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "pot.h"
#include "common.h"

void die(int ret, char *fmtstr, ...)
{
	va_list args;

	fprintf(stderr, "%s: ", progname);
	va_start(args, fmtstr);
	vfprintf(stderr, fmtstr, args);
	va_end(args);
	exit(ret);
}

void errstr(char *fmtstr, ...)
{
	va_list args;

	fprintf(stderr, "%s: ", progname);
	va_start(args, fmtstr);
	vfprintf(stderr, fmtstr, args);
	va_end(args);
}

void *smalloc(size_t size)
{
	void *ret = malloc(size);

	if (!ret) {
		puts("Not enough memory.\n");
		exit(3);
	}
	return ret;
}

void sstrncpy(char *d, const char *s, int len)
{
	strncpy(d, s, len);
	d[len - 1] = 0;
}

void sstrncat(char *d, const char *s, int len)
{
	strncat(d, s, len);
	d[len - 1] = 0;
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

int devcmp(const char *mtab_dev, char *user_dev)
{
	struct stat mtab_stat, user_stat;

	if (stat(mtab_dev, &mtab_stat) < 0 || stat(user_dev, &user_stat) < 0)
		return (strcmp(mtab_dev, user_dev) == 0);
	if (!S_ISBLK(mtab_stat.st_mode) || !S_ISBLK(user_stat.st_mode))
		return 0;
	if (mtab_stat.st_rdev != user_stat.st_rdev)
		return 0;
	return 1;
}

int dircmp(char *mtab_dir, char *user_dir)
{
	struct stat mtab_stat, user_stat;

	if (stat(mtab_dir, &mtab_stat) < 0 || stat(user_dir, &user_stat) < 0)
		return (strcmp(mtab_dir, user_dir) == 0);
	if (!S_ISDIR(mtab_stat.st_mode) || !S_ISDIR(user_stat.st_mode))
		return 0;
	if (mtab_stat.st_dev != user_stat.st_dev)
		return 0;
	if (mtab_stat.st_ino != user_stat.st_ino)
		return 0;
	return 1;
}

void version(void)
{
	printf(_("Quota utilities version %s.\n"), QUOTA_VERSION);
#if defined(RPC) || defined(EXT2_DIRECT)
	printf(_("Compiled with "));
#if defined(RPC) && defined(EXT2_DIRECT)
	puts(_("RPC and EXT2_DIRECT"));
#elif defined(RPC)
	puts(_("RPC"));
#else
	puts(_("EXT2_DIRECT"));
#endif /* defined RPC && EXT2_DIRECT */
#endif /* defined RPC || EXT2_DIRECT */
	printf(_("Bugs to %s\n"), MY_EMAIL);
}
