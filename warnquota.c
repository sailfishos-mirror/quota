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
 * Version: $Id: warnquota.c,v 1.2 2001/05/02 09:32:22 jkar8572 Exp $
 *
 *          This program is free software; you can redistribute it and/or
 *          modify it under the terms of the GNU General Public License as
 *          published by the Free Software Foundation; either version 2 of
 *          the License, or (at your option) any later version.
 */

#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>

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

#define QUOTATAB "/etc/quotatab"
#define CNF_BUFFER 2048
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

int qtab_i = 0;
char *progname;
quotatable_t *quotatable = (quotatable_t *) NULL;

/*
 * Global pointers to list.
 */
static struct offenderlist *offenders = (struct offenderlist *)0;

struct offenderlist *add_offender(int id)
{
	struct passwd *pwd;
	struct offenderlist *offender;

	if ((pwd = getpwuid(id)) == (struct passwd *)0)
		return ((struct offenderlist *)0);

	offender = (struct offenderlist *)smalloc(sizeof(struct offenderlist));

	offender->offender_id = id;
	offender->offender_name = (char *)smalloc(strlen(pwd->pw_name) + 1);
	offender->usage = (struct usage *)NULL;
	strcpy(offender->offender_name, pwd->pw_name);
	offender->next = offenders;
	offenders = offender;
	return offender;
}

void add_offence(struct dquot *dquot)
{
	struct offenderlist *lptr;
	struct usage *usage;

	for (lptr = offenders; lptr; lptr = lptr->next)
		if (lptr->offender_id == dquot->dq_id)
			break;

	if (!lptr)
		if (!(lptr = add_offender(dquot->dq_id)))
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

int check_offence(struct dquot *dquot)
{
	if (
	    (dquot->dq_dqb.dqb_bsoftlimit
	     && toqb(dquot->dq_dqb.dqb_curspace) >= dquot->dq_dqb.dqb_bsoftlimit)
	    || (dquot->dq_dqb.dqb_isoftlimit
		&& dquot->dq_dqb.dqb_curinodes >= dquot->dq_dqb.dqb_isoftlimit)) add_offence(dquot);
	return 0;
}

void mail_user(struct offenderlist *offender, struct configparams *config)
{
	struct usage *lptr;
	FILE *fp;
	int cnt;
	char timebuf[MAXTIMELEN];
	struct util_dqblk *dqb;

	if ((fp = popen(config->mail_cmd, "w")) != (FILE *) 0) {
		fprintf(fp, "From: %s\n", config->from);
		fprintf(fp, "Reply-To: %s\n", config->support);
		fprintf(fp, "Subject: %s\n", config->subject);
		fprintf(fp, "To: %s\n", offender->offender_name);
		fprintf(fp, "Cc: %s\n", config->cc_to);
		fprintf(fp, "\n");
		fprintf(fp, DEF_MESSAGE);
		for (lptr = offender->usage; lptr != (struct usage *)0; lptr = lptr->next) {
			dqb = &lptr->dq_dqb;
			for (cnt = 0; cnt < qtab_i; cnt++) {
				if (!strncmp
				    (quotatable[cnt].devname, lptr->devicename,
				     strlen(quotatable[cnt].devname)))
					fprintf(fp, "\n%s\n", quotatable[cnt].devdesc);
			}
			fprintf(fp,
				_
				("\n                        Block limits               File limits\n"));
			fprintf(fp,
				_
				("Filesystem           used    soft    hard  grace    used  soft  hard  grace\n"));
			if (strlen(lptr->devicename) > 15)
				fprintf(fp, "%s\n%15s", lptr->devicename, "");
			else
				fprintf(fp, "%-15s", lptr->devicename);
			if (dqb->dqb_bsoftlimit && dqb->dqb_bsoftlimit <= toqb(dqb->dqb_curspace))
				difftime2str(dqb->dqb_btime, timebuf);
			else
				timebuf[0] = '\0';
			fprintf(fp, "%c%c%8Lu%8Lu%8Lu%7s",
				dqb->dqb_bsoftlimit
				&& toqb(dqb->dqb_curspace) >= dqb->dqb_bsoftlimit ? '+' : '-',
				dqb->dqb_isoftlimit
				&& dqb->dqb_curinodes >= dqb->dqb_isoftlimit ? '+' : '-',
				(long long)toqb(dqb->dqb_curspace), (long long)dqb->dqb_bsoftlimit,
				(long long)dqb->dqb_bhardlimit, timebuf);
			if (dqb->dqb_isoftlimit && dqb->dqb_isoftlimit <= dqb->dqb_curinodes)
				difftime2str(dqb->dqb_itime, timebuf);
			else
				timebuf[0] = '\0';
			fprintf(fp, "  %6Lu%6Lu%6Lu%7s\n\n",
				(long long)dqb->dqb_curinodes,
				(long long)dqb->dqb_isoftlimit,
				(long long)dqb->dqb_ihardlimit, timebuf);
		}
		fprintf(fp, DEF_SIGNATURE, config->support, config->phone);
		fclose(fp);
	}
}

void mail_to_offenders(struct configparams *config)
{
	struct offenderlist *lptr;

	/*
	 * Dump offenderlist.
	 */
	for (lptr = offenders; lptr != (struct offenderlist *)0; lptr = lptr->next)
		mail_user(lptr, config);
}

void get_quotatable(void)
{
	FILE *fp;
	char buffer[2048], *filename, *colpos;

	filename = (char *)smalloc(strlen(QUOTATAB) + 1);
	sprintf(filename, "%s", QUOTATAB);

	if ((fp = fopen(filename, "r")) == (FILE *) NULL)
		return;

	for (qtab_i = 0;
	     quotatable =
	     (quotatable_t *) realloc(quotatable, sizeof(quotatable_t) * (qtab_i + 1)),
	     fgets(buffer, sizeof(buffer), fp) != (char *)NULL; qtab_i++) {
		if ((colpos = strchr(buffer, ':'))) {
			*colpos = 0;
			quotatable[qtab_i].devname = (char *)smalloc(strlen(buffer) + 1);
			strcpy(quotatable[qtab_i].devname, buffer);
			quotatable[qtab_i].devdesc = (char *)smalloc(strlen(++colpos) + 1);
			strcpy(quotatable[qtab_i].devdesc, colpos);
			if ((colpos = strchr(quotatable[qtab_i].devdesc, '\n')))
				*colpos = 0;
			while ((colpos = strchr(quotatable[qtab_i].devdesc, '|')))
				*colpos = '\n';
		}

		if (buffer[0] == '#' ||	/* comment */
		    !quotatable[qtab_i].devname || !quotatable[qtab_i].devdesc ||
		    strlen(quotatable[qtab_i].devname) < 2 ||
		    strlen(quotatable[qtab_i].devdesc) < 2 /* stupid root */ )qtab_i--;
	}
	fclose(fp);
	free(filename);
}

/*
 * Wipe spaces, tabs, quotes and newlines from beginning and end of string
 */
void stripstring(char **buff)
{
	char *p;

	/* first put a \0 at the tight place to end the string */
	p = *buff + strlen(*buff) - 1;
	while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '"' || *p == '\'')
		p--;
	p[1] = 0;

	/* then determine the position to start */
	p = *buff;
	while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '"' || *p == '\'')
		p++;

	*buff = p;
}

/*
 * Reads config parameters from configfile
 * uses default values if errstr occurs
 */
void readconfigfile(const char *filename, struct configparams *config)
{
	FILE *fp;
	char *buff;
	char *var;
	char *value;
	char *pos;
	int line;

	/* set default values */
	strncpy(config->mail_cmd, MAIL_CMD, CNF_BUFFER);
	strncpy(config->from, FROM, CNF_BUFFER);
	strncpy(config->subject, SUBJECT, CNF_BUFFER);
	strncpy(config->cc_to, CC_TO, CNF_BUFFER);
	strncpy(config->support, SUPPORT, CNF_BUFFER);
	strncpy(config->phone, PHONE, CNF_BUFFER);

	fp = fopen(filename, "r");
	if (fp == (FILE *) NULL) {	/* if config file doesn't exist or is not readable */
		return;
	}

	buff = (char *)smalloc(CNF_BUFFER);
	line = 0;
	while (fgets(buff, CNF_BUFFER, fp)) {	/* start reading lines */
		line++;

		/* check for comments or empty lines */
		if (buff[0] == '#' || buff[0] == ';' || buff[0] == '\n')
			continue;

		/* check for a '=' char */
		if ((pos = strchr(buff, '='))) {
			pos[0] = '\0';	/* split buff in two parts: var and value */
			var = buff;
			value = pos + 1;

			stripstring(&var);	/* clean up var and value */
			stripstring(&value);

			/* check if var matches anything */
			if (!strncmp(var, "MAIL_CMD", CNF_BUFFER)) {
				strncpy(config->mail_cmd, value, CNF_BUFFER);
			}
			else if (!strncmp(var, "FROM", CNF_BUFFER)) {
				strncpy(config->from, value, CNF_BUFFER);
			}
			else if (!strncmp(var, "SUBJECT", CNF_BUFFER)) {
				strncpy(config->subject, value, CNF_BUFFER);
			}
			else if (!strncmp(var, "CC_TO", CNF_BUFFER)) {
				strncpy(config->cc_to, value, CNF_BUFFER);
			}
			else if (!strncmp(var, "SUPPORT", CNF_BUFFER)) {
				strncpy(config->support, value, CNF_BUFFER);
			}
			else if (!strncmp(var, "PHONE", CNF_BUFFER)) {
				strncpy(config->phone, value, CNF_BUFFER);
			}
			else {	/* not matched at all */
				errstr( "Error in config file (line %d), ignoring\n",
					line);
			}
		}
		else {		/* no '=' char in this line */
			errstr( "Possible error in config file (line %d), ignoring\n",
				line);
		}
	}
	fclose(fp);

	free(buff);

	return;
}

void warn_quota(void)
{
	struct quota_handle **handles;
	struct configparams config;
	int i;

	readconfigfile(WARNQUOTA_CONF, &config);

	handles = create_handle_list(0, NULL, USRQUOTA, -1, IOI_LOCALONLY | IOI_READONLY | IOI_OPENFILE);
	for (i = 0; handles[i]; i++)
		handles[i]->qh_ops->scan_dquots(handles[i], check_offence);
	get_quotatable();
	mail_to_offenders(&config);
}

int main(int argc, char **argv)
{
	gettexton();
	progname = basename(argv[0]);

	warn_new_kernel(-1);
	warn_quota();

	return 0;
}
