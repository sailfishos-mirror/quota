/*
 *
 *	Various things common for all utilities
 *
 */

#ifndef _COMMON_H
#define _COMMON_H

#define MY_EMAIL "mvw@planets.elm.net, jack@suse.cz"

/* Name of current program for error reporting */
extern char *progname;

/* Finish programs being */
void die(int, char *, ...);

/* Print an error */
void errstr(char *, ...);

/* malloc() with error check */
void *smalloc(size_t);

/* realloc() with error check */
void *srealloc(void *, size_t);

/* Safe strncpy - always finishes string */
void sstrncpy(char *, const char *, int);

/* Safe strncat - always finishes string */
void sstrncat(char *, const char *, int);

/* Safe version of strdup() */
char *sstrdup(const char *s);

/* Print version string */
void version(void);

#endif /* _COMMON_H */
