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
