/*
 * QUOTA    An implementation of the diskquota system for the LINUX operating
 *          system. QUOTA is implemented using the BSD systemcall interface
 *          as the means of communication with the user level. Should work for
 *          all filesystems because of integration into the VFS layer of the
 *          operating system. This is based on the Melbourne quota system wich
 *          uses both user and group quota files.
 * 
 *          Program to mail to users that they are over there quota.
 * 
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: warnquota.c,v 1.9 2002/02/25 11:26:16 jkar8572 Exp $
 *
 *          This program is free software; you can redistribute it and/or
 *          modify it under the terms of the GNU General Public License as
 *          published by the Free Software Foundation; either version 2 of
 *          the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "mntopt.h"
#include "pot.h"
#include "bylabel.h"
#include "common.h"
#include "quotasys.h"
#include "quotaio.h"

/* these are just defaults, overridden in the WARNQUOTA_CONF file */
#define MAIL_CMD "/usr/lib/sendmail -t"
#define FROM     "support@localhost"
#define SUBJECT  "Disk Quota usage on system"
#define CC_TO    "root"
#define SUPPORT  "support@localhost"
#define PHONE    "(xxx) xxx-xxxx or (yyy) yyy-yyyy"

#define DEF_MESSAGE	_("Hi,\n\nWe noticed that you are in violation with the quotasystem\n" \
                          "used on this system. We have found the following violations:\n\n")
#define DEF_SIGNATURE	_("\nWe hope that you will cleanup before your grace period expires.\n" \
	                  "\nBasically, this means that the system thinks you are using more disk space\n" \
	                  "on the above partition(s) than you are allowed.  If you do not delete files\n" \
	                  "and get below your quota before the grace period expires, the system will\n" \
	                  "prevent you from creating new files.\n\n" \
                          "For additional assistance, please contact us at %s\nor via " \
                          "phone at %s.\n")

#define SHELL "/bin/sh"
#define QUOTATAB "/etc/quotatab"
#define CNF_BUFFER 2048
#define IOBUF_SIZE 16384		/* Size of buffer for line in config files */
#define WARNQUOTA_CONF "/etc/warnquota.conf"

struct usage {
	char *devicename;
	struct util_dqblk dq_dqb;
	struct usage *next;
};

struct configparams {
	char mail_cmd[CNF_BUFFER];
	char from[CNF_BUFFER];
	char subject[CNF_BUFFER];
	char cc_to[CNF_BUFFER];
	char support[CNF_BUFFER];
	char phone[CNF_BUFFER];
	char *message;
	char *signature;
};

struct offenderlist {
	int offender_id;
	char *offender_name;
	struct usage *usage;
	struct offenderlist *next;
};

typedef struct quotatable {
	char *devname;
	char *devdesc;
} quotatable_t;

int qtab_i = 0, fmt = -1;
char *configfile = WARNQUOTA_CONF, *quotatabfile = QUOTATAB;
char *progname;
quotatable_t *quotatable = (quotatable_t *) NULL;

/*
 * Global pointers to list.
 */
static struct offenderlist *offenders = (struct offenderlist *)0;

struct offenderlist *add_offender(int id, char *name)
{
	struct offenderlist *offender;
	char namebuf[MAXNAMELEN];
	
	if (!name) {
		if (id2name(id, USRQUOTA, namebuf)) {
			errstr(_("Can't get name for uid %u.\n"), id);
			return NULL;
		}
		name = namebuf;
	}
	offender = (struct offenderlist *)smalloc(sizeof(struct offenderlist));
	offender->offender_id = id;
	offender->offender_name = sstrdup(name);
	offender->usage = (struct usage *)NULL;
	offender->next = offenders;
	offenders = offender;
	return offender;
}

void add_offence(struct dquot *dquot, char *name)
{
	struct offenderlist *lptr;
	struct usage *usage;

	for (lptr = offenders; lptr; lptr = lptr->next)
		if (lptr->offender_id == dquot->dq_id)
			break;

	if (!lptr)
		if (!(lptr = add_offender(dquot->dq_id, name)))
			return;

	usage = (struct usage *)smalloc(sizeof(struct usage));
	memcpy(&usage->dq_dqb, &dquot->dq_dqb, sizeof(struct util_dqblk));

	usage->devicename = sstrdup(dquot->dq_h->qh_quotadev);
	/*
	 * Stuff it in front
	 */
	usage->next = lptr->usage;
	lptr->usage = usage;
}

int check_offence(struct dquot *dquot, char *name)
{
	if ((dquot->dq_dqb.dqb_bsoftlimit && toqb(dquot->dq_dqb.dqb_curspace) >= dquot->dq_dqb.dqb_bsoftlimit)
	    || (dquot->dq_dqb.dqb_isoftlimit && dquot->dq_dqb.dqb_curinodes >= dquot->dq_dqb.dqb_isoftlimit))
		add_offence(dquot, name);
	return 0;
}

FILE *run_mailer(char *command)
{
	int pipefd[2];
	FILE *f;

	if (pipe(pipefd) < 0) {
		errstr(_("Can't create pipe: %s\n"), strerror(errno));
		return NULL;
	}
	signal(SIGPIPE, SIG_IGN);
	switch(fork()) {
		case -1:
			errstr(_("Can't fork: %s\n"), strerror(errno));
			return NULL;
		case 0:
			close(pipefd[1]);
			if (dup2(pipefd[0], 0) < 0) {
				errstr(_("Can't duplicate descriptor: %s\n"), strerror(errno));
				exit(1);
			}			
			execl(SHELL, SHELL, "-c", command, NULL);
			errstr(_("Can't execute '%s': %s\n"), command, strerror(errno));
			exit(1);
		default:
			close(pipefd[0]);
			if (!(f = fdopen(pipefd[1], "w")))
				errstr(_("Can't open pine: %s\n"), strerror(errno));
			return f;
	}
}

int mail_user(struct offenderlist *offender, struct configparams *config)
{
	struct usage *lptr;
	FILE *fp;
	int cnt, status;
	char timebuf[MAXTIMELEN];
	struct util_dqblk *dqb;

	if (!(fp = run_mailer(config->mail_cmd)))
		return -1;
	fprintf(fp, "From: %s\n", config->from);
	fprintf(fp, "Reply-To: %s\n", config->support);
	fprintf(fp, "Subject: %s\n", config->subject);
	fprintf(fp, "To: %s\n", offender->offender_name);
	fprintf(fp, "Cc: %s\n", config->cc_to);
	fprintf(fp, "\n");
	if (config->message)
		fputs(config->message, fp);
	else
		fputs(DEF_MESSAGE, fp);

	for (lptr = offender->usage; lptr; lptr = lptr->next) {
		dqb = &lptr->dq_dqb;
		for (cnt = 0; cnt < qtab_i; cnt++)
			if (!strcmp(quotatable[cnt].devname, lptr->devicename)) {
				fprintf(fp, "\n%s (%s)\n", quotatable[cnt].devdesc, quotatable[cnt].devname);
				break;
			}
		if (cnt == qtab_i)	/* Description not found? */
			fprintf(fp, "\n%s\n", lptr->devicename);
		fprintf(fp, _("\n                        Block limits               File limits\n"));
		fprintf(fp, _("Filesystem           used    soft    hard  grace    used  soft  hard  grace\n"));
		if (strlen(lptr->devicename) > 15)
			fprintf(fp, "%s\n%15s", lptr->devicename, "");
		else
			fprintf(fp, "%-15s", lptr->devicename);
		if (dqb->dqb_bsoftlimit && dqb->dqb_bsoftlimit <= toqb(dqb->dqb_curspace))
			difftime2str(dqb->dqb_btime, timebuf);
		else
			timebuf[0] = '\0';
		fprintf(fp, "%c%c%8Lu%8Lu%8Lu%7s",
		        dqb->dqb_bsoftlimit && toqb(dqb->dqb_curspace) >= dqb->dqb_bsoftlimit ? '+' : '-',
			dqb->dqb_isoftlimit && dqb->dqb_curinodes >= dqb->dqb_isoftlimit ? '+' : '-',
			(long long)toqb(dqb->dqb_curspace), (long long)dqb->dqb_bsoftlimit,
			(long long)dqb->dqb_bhardlimit, timebuf);
		if (dqb->dqb_isoftlimit && dqb->dqb_isoftlimit <= dqb->dqb_curinodes)
			difftime2str(dqb->dqb_itime, timebuf);
		else
			timebuf[0] = '\0';
		fprintf(fp, "  %6Lu%6Lu%6Lu%7s\n\n", (long long)dqb->dqb_curinodes,
		        (long long)dqb->dqb_isoftlimit, (long long)dqb->dqb_ihardlimit, timebuf);
	}
	if (config->signature)
		fputs(config->signature, fp);
	else
		fprintf(fp, DEF_SIGNATURE, config->support, config->phone);
	fclose(fp);
	if (wait(&status) < 0)	/* Wait for mailer */
		errstr(_("Can't wait for mailer: %s\n"), strerror(errno));
	else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		errstr(_("Warning: Mailer exitted abnormally.\n"));

	return 0;
}

int mail_to_offenders(struct configparams *config)
{
	struct offenderlist *lptr;
	int ret = 0;

	/*
	 * Dump offenderlist.
	 */
	for (lptr = offenders; lptr; lptr = lptr->next)
		ret |= mail_user(lptr, config);
	return ret;
}

/*
 * Wipe spaces, tabs, quotes and newlines from beginning and end of string
 */
void stripstring(char **buff)
{
	int i;

	/* first put a \0 at the tight place to end the string */
	for (i = strlen(*buff) - 1; i >= 0 && (isspace((*buff)[i]) || (*buff)[i] == '"'
	     || (*buff)[i] == '\''); i--);
	(*buff)[i+1] = 0;

	/* then determine the position to start */
	for (i = 0; (*buff)[i] && (isspace((*buff)[i]) || (*buff)[i] == '"' || (*buff)[i] == '\''); i++);
	*buff += i;
}

/*
 * Substitute '|' with end of lines
 */
void create_eoln(char *buf)
{
	char *colpos = buf;

	while ((colpos = strchr(colpos, '|')))
		*colpos = '\n';
}

/*
 * Read /etc/quotatab (description of devices for users)
 */
int get_quotatable(void)
{
	FILE *fp;
	char buffer[IOBUF_SIZE], *colpos, *devname, *devdesc;
	int line;
	struct stat st;

	if (!(fp = fopen(quotatabfile, "r"))) {
		errstr(_("Can't open %s: %s\nWill use device names.\n"), quotatabfile, strerror(errno));
		qtab_i = 0;
		return 0;
	}

	line = 0;
	for (qtab_i = 0; quotatable = realloc(quotatable, sizeof(quotatable_t) * (qtab_i + 1)),
	     fgets(buffer, sizeof(buffer), fp); qtab_i++) {
		line++;
		quotatable[qtab_i].devname = NULL;
		quotatable[qtab_i].devdesc = NULL;
		if (buffer[0] == '#' || buffer[0] == ';') {	/* Comment? */
			qtab_i--;
			continue;
		}
		/* Empty line? */
		for (colpos = buffer; isspace(*colpos); colpos++);
		if (!*colpos) {
			qtab_i--;
			continue;
		}
		/* Parse line */
		if (!(colpos = strchr(buffer, ':'))) {
			errstr(_("Can't parse line %d in quotatab (missing ':')\n"), line);
			qtab_i--;
			continue;
		}
		*colpos = 0;
		devname = buffer;
		devdesc = colpos+1;
		stripstring(&devname);
		stripstring(&devdesc);
		quotatable[qtab_i].devname = sstrdup(devname);
		quotatable[qtab_i].devdesc = sstrdup(devdesc);
		create_eoln(quotatable[qtab_i].devdesc);

		if (stat(quotatable[qtab_i].devname, &st) < 0)
			errstr(_("Can't stat device %s (maybe typo in quotatab)\n"), quotatable[qtab_i].devname);
	}
	fclose(fp);
	return 0;
}

/*
 * Reads config parameters from configfile
 * uses default values if errstr occurs
 */
int readconfigfile(const char *filename, struct configparams *config)
{
	FILE *fp;
	char buff[IOBUF_SIZE];
	char *var;
	char *value;
	char *pos;
	int line, len, bufpos;

	/* set default values */
	sstrncpy(config->mail_cmd, MAIL_CMD, CNF_BUFFER);
	sstrncpy(config->from, FROM, CNF_BUFFER);
	sstrncpy(config->subject, SUBJECT, CNF_BUFFER);
	sstrncpy(config->cc_to, CC_TO, CNF_BUFFER);
	sstrncpy(config->support, SUPPORT, CNF_BUFFER);
	sstrncpy(config->phone, PHONE, CNF_BUFFER);
	config->signature = config->message = NULL;

	if (!(fp = fopen(filename, "r"))) {
		errstr(_("Can't open %s: %s\n"), filename, strerror(errno));
		return -1;
	}

	line = 0;
	bufpos = 0;
	while (fgets(buff + bufpos, sizeof(buff) - bufpos, fp)) {	/* start reading lines */
		line++;

		if (!bufpos) {
			/* check for comments or empty lines */
			if (buff[0] == '#' || buff[0] == ';')
				continue;
			/* Is line empty? */
			for (pos = buff; isspace(*pos); pos++);
			if (!*pos)			/* Nothing else was on the line */
				continue;
		}
		len = bufpos + strlen(buff+bufpos);
		if (buff[len-1] != '\n')
			errstr(_("Line %d too long. Truncating.\n"));
		else {
			len--;
			if (buff[len-1] == '\\') {	/* Should join with next line? */
				bufpos += len-1;
				continue;
			}
		}
		buff[len] = 0;
		bufpos = 0;
		
		/* check for a '=' char */
		if ((pos = strchr(buff, '='))) {
			*pos = 0;	/* split buff in two parts: var and value */
			var = buff;
			value = pos + 1;

			stripstring(&var);
			stripstring(&value);

			/* check if var matches anything */
			if (!strcmp(var, "MAIL_CMD"))
				sstrncpy(config->mail_cmd, value, CNF_BUFFER);
			else if (!strcmp(var, "FROM"))
				sstrncpy(config->from, value, CNF_BUFFER);
			else if (!strcmp(var, "SUBJECT"))
				sstrncpy(config->subject, value, CNF_BUFFER);
			else if (!strcmp(var, "CC_TO"))
				sstrncpy(config->cc_to, value, CNF_BUFFER);
			else if (!strcmp(var, "SUPPORT"))
				sstrncpy(config->support, value, CNF_BUFFER);
			else if (!strcmp(var, "PHONE"))
				sstrncpy(config->phone, value, CNF_BUFFER);
			else if (!strcmp(var, "MESSAGE")) {
				config->message = sstrdup(value);
				create_eoln(config->message);
			}
			else if (!strcmp(var, "SIGNATURE")) {
				config->signature = sstrdup(value);
				create_eoln(config->signature);
			}
			else	/* not matched at all */
				errstr(_("Error in config file (line %d), ignoring\n"), line);
		}
		else		/* no '=' char in this line */
			errstr(_("Possible error in config file (line %d), ignoring\n"), line);
	}
	if (bufpos)
		errstr(_("Unterminated last line, ignoring\n"));
	fclose(fp);

	return 0;
}

void warn_quota(void)
{
	struct quota_handle **handles;
	struct configparams config;
	int i;

	if (readconfigfile(configfile, &config) < 0)
		exit(1);
	if (get_quotatable() < 0)
		exit(1);

	handles = create_handle_list(0, NULL, USRQUOTA, -1, IOI_LOCALONLY | IOI_READONLY | IOI_OPENFILE);
	for (i = 0; handles[i]; i++)
		handles[i]->qh_ops->scan_dquots(handles[i], check_offence);
	if (mail_to_offenders(&config) < 0)
		exit(1);
}

/* Print usage information */
static void usage(void)
{
	errstr(_("Usage:\n  warnquota [-F quotaformat] [-c configfile] [-q quotatabfile]\n"));
}

static void parse_options(int argcnt, char **argstr)
{
	int ret;

	while ((ret = getopt(argcnt, argstr, "VF:hc:q:")) != -1) {
		switch (ret) {
		  case '?':
		  case 'h':
			usage();
		  case 'V':
			version();
			break;
		  case 'F':
			if ((fmt = name2fmt(optarg)) == QF_ERROR)
				exit(1);
			break;
		  case 'c':
			configfile = optarg;
			break;
		  case 'q':
			quotatabfile = optarg;
			break;
		}
	}
}

int main(int argc, char **argv)
{
	gettexton();
	progname = basename(argv[0]);

	warn_new_kernel(-1);
	parse_options(argc, argv);
	warn_quota();

	return 0;
}
