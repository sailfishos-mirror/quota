/*
 *
 *	Various things common for all utilities
 *
 */

#ifndef _COMMON_H
#define _COMMON_H

#define MY_EMAIL "mvw@planets.elm.net, jack@suse.cz"

/* Finish programs being */
void die(int, char *, ...);

/* malloc() with error check */
void *smalloc(size_t);

/* Safe strncpy - always finishes string */
void sstrncpy(char *, const char *, int);

/* Safe strncat - always finishes string */
void sstrncat(char *, const char *, int);

/* Safe version of strdup() */
char *sstrdup(const char *s);

/* Print version string */
void version(void);

#endif /* _COMMON_H */
