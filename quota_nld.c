/*
 *  A deamon to read quota warning messages from the kernel netlink socket
 *  and either pipe them to the system DBUS or write them to user's console
 *
 *  Copyright (c) 2007 SUSE CR, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <utmp.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <netlink/netlink.h>
#include <netlink/netlink-kernel.h>
#include <netlink/genl/mngt.h>

#include <dbus/dbus.h>

#include "pot.h"
#include "common.h"
#include "quotasys.h"
#include "quota.h"

char *progname;

static const struct option options[] = {
	{ "version", 0, NULL, 'V' },
	{ "help", 0, NULL, 'h' },
	{ "no-dbus", 0, NULL, 'D' },
	{ "no-console", 0, NULL, 'C' },
	{ "no-daemon", 0, NULL, 'F' },
	{ NULL, 0, NULL, 0 }
};

struct quota_warning {
	uint32_t qtype;
	uint64_t excess_id;
	uint32_t warntype;
	uint32_t dev_major;
	uint32_t dev_minor;
	uint64_t caused_id;
};

static int quota_nl_warn_cmd_parser(struct genl_ops *ops, struct genl_cmd *cmd,
	struct genl_info *info, void *arg);

static struct nla_policy quota_nl_warn_cmd_policy[QUOTA_NL_A_MAX+1] = {
	[QUOTA_NL_A_QTYPE] = { .type = NLA_U32 },
	[QUOTA_NL_A_EXCESS_ID] = { .type = NLA_U64 },
	[QUOTA_NL_A_WARNING] = { .type = NLA_U32 },
	[QUOTA_NL_A_DEV_MAJOR] = { .type = NLA_U32 },
	[QUOTA_NL_A_DEV_MINOR] = { .type = NLA_U32 },
	[QUOTA_NL_A_CAUSED_ID] = { .type = NLA_U64 },
};

static struct genl_cmd quota_nl_warn_cmd = {
	.c_id		= QUOTA_NL_C_WARNING,
	.c_name		= "Quota warning",
	.c_maxattr	= QUOTA_NL_A_MAX,
	.c_attr_policy	= quota_nl_warn_cmd_policy,
	.c_msg_parser	= quota_nl_warn_cmd_parser,
};

static struct genl_ops quota_nl_ops = {
	.o_cmds		= &quota_nl_warn_cmd,
	.o_ncmds	= 1,
	.o_name		= "VFS_DQUOT",
	.o_hdrsize	= 0,
};

/* User options */
#define FL_NODBUS 1
#define FL_NOCONSOLE 2
#define FL_NODAEMON 4

int flags;

void show_help(void)
{
	errstr(_("Usage: %s [options]\nOptions are:\n\
 -h --help         shows this text\n\
 -V --version      shows version information\n\
 -C --no-console   do not try to write messages to console\n\
 -D --no-dbus      do not try to write messages to DBUS\n\
 -F --foreground   run daemon in foreground\n"), progname);
}

static void parse_options(int argc, char **argv)
{
	int opt;

	while ((opt = getopt_long(argc, argv, "VhDCF", options, NULL)) >= 0) {
		switch (opt) {
			case 'V':
				version();
				exit(0);
			case 'h':
				show_help();
				exit(0);
			case 'D':
				flags |= FL_NODBUS;
				break;
			case 'C':
				flags |= FL_NOCONSOLE;
				break;
			case 'F':
				flags |= FL_NODAEMON;
				break;
			default:
				errstr(_("Unknown option '%c'.\n"), opt);
				show_help();
				exit(1);
		}
	}
	if (flags & FL_NODBUS && flags & FL_NOCONSOLE) {
		errstr(_("No possible destination for messages. Nothing to do.\n"));
		exit(0);
	}
}

/* Parse netlink message and process it. */
static int quota_nl_warn_cmd_parser(struct genl_ops *ops, struct genl_cmd *cmd,
	struct genl_info *info, void *arg)
{
	struct quota_warning *warn = (struct quota_warning *)arg;

	warn->qtype = nla_get_u32(info->attrs[QUOTA_NL_A_QTYPE]);
	warn->excess_id = nla_get_u64(info->attrs[QUOTA_NL_A_EXCESS_ID]);
	warn->warntype = nla_get_u32(info->attrs[QUOTA_NL_A_WARNING]);
	warn->dev_major = nla_get_u32(info->attrs[QUOTA_NL_A_DEV_MAJOR]);
	warn->dev_minor = nla_get_u32(info->attrs[QUOTA_NL_A_DEV_MINOR]);
	warn->caused_id = nla_get_u64(info->attrs[QUOTA_NL_A_CAUSED_ID]);

	return 0;
}

static struct nl_handle *init_netlink(void)
{
	struct nl_handle *handle;
	int ret;

	handle = nl_handle_alloc();
	if (!handle)
		die(2, _("Cannot allocate netlink handle!\n"));
	nl_disable_sequence_check(handle);
	ret = nl_connect(handle, NETLINK_GENERIC);
	if (ret < 0)
		die(2, _("Cannot connect to netlink socket: %s\n"), strerror(-ret));
	ret = genl_ops_resolve(handle, &quota_nl_ops);
	if (ret < 0)
		die(2, _("Cannot resolve quota netlink name: %s\n"), strerror(-ret));

	ret = nl_socket_add_membership(handle, quota_nl_ops.o_id);
	if (ret < 0)
		die(2, _("Cannot join quota multicast group: %s\n"), strerror(-ret));

	ret = genl_register(&quota_nl_ops);
	if (ret < 0)
		die(2, _("Cannot register netlink family: %s\n"), strerror(-ret));

	return handle;
}

static DBusConnection *init_dbus(void)
{
	DBusConnection *handle;
	DBusError err;

	dbus_error_init(&err);
	handle = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (dbus_error_is_set(&err))
		die(2, _("Cannot connect to system DBUS: %s\n"), err.message);

	dbus_connection_set_exit_on_disconnect(handle, FALSE);
	return handle;
}

static int write_all(int fd, char *buf, int len)
{
	int ret;

	while (len) {
		ret = write(fd, buf, len);
		if (ret < 0)
			return -1;
		buf += ret;
		len -= ret;
	}
	return 0;
}

#define WARN_BUF_SIZE 512

/* Scan through utmp, find latest used controlling tty and write to it */
static void write_console_warning(struct quota_warning *warn)
{
	struct utmp *uent;
	char user[MAXNAMELEN];
	struct stat st;
	char dev[PATH_MAX];
	time_t max_atime = 0;
	char max_dev[PATH_MAX];
	int fd;
	char warnbuf[WARN_BUF_SIZE];
	char *level, *msg;

	uid2user(warn->caused_id, user);
	strcpy(dev, "/dev/");

	setutent();
	endutent();
	while ((uent = getutent())) {
		if (uent->ut_type != USER_PROCESS)
			continue;
		/* Entry for a different user? */
		if (strcmp(user, uent->ut_user))
			continue;
		sstrncpy(dev+5, uent->ut_line, PATH_MAX-5);
		if (stat(dev, &st) < 0)
			continue;	/* Failed to stat - not a good candidate for warning... */
		if (max_atime < st.st_atime) {
			max_atime = st.st_atime;
			strcpy(max_dev, dev);
		}
	}
	if (!max_atime) {
		errstr(_("Failed to find tty of user %Lu to report warning to.\n"), (unsigned long long)warn->caused_id);
		return;
	}
	fd = open(max_dev, O_WRONLY);
	if (fd < 0) {
		errstr(_("Failed to open tty %s of user %Lu to report warning.\n"), dev, (unsigned long long)warn->caused_id);
		return;
	}
	id2name(warn->excess_id, warn->qtype, user);
	if (warn->warntype == QUOTA_NL_ISOFTWARN || warn->warntype == QUOTA_NL_BSOFTWARN)
		level = "Warning";
	else
		level = "Error";
	switch (warn->warntype) {
		case QUOTA_NL_IHARDWARN:
			msg = "file limit reached";
			break;
		case QUOTA_NL_ISOFTLONGWARN:
			msg = "file quota exceeded too long";
			break;
		case QUOTA_NL_ISOFTWARN:
			msg = "file quota exceeded";
			break;
		case QUOTA_NL_BHARDWARN:
			msg = "block limit reached";
			break;
		case QUOTA_NL_BSOFTLONGWARN:
			msg = "block quota exceeded too long";
			break;
		case QUOTA_NL_BSOFTWARN:
			msg = "block quota exceeded";
			break;
		default:
			msg = "unknown quota warning";
	}
	sprintf(warnbuf, "%s: %s %s %s.\r\n", level, type2name(warn->qtype), user, msg);
	if (write_all(fd, warnbuf, strlen(warnbuf)) < 0)
		errstr(_("Failed to write quota message for user %Lu to %s: %s\n"), (unsigned long long)warn->caused_id, dev, strerror(errno));
	close(fd);
}

/* Send warning through DBUS */
static void write_dbus_warning(struct DBusConnection *dhandle, struct quota_warning *warn)
{
	DBusMessage* msg;
	DBusMessageIter args;

	msg = dbus_message_new_signal("/", "com.system.quota.warning", "warning");
	if (!msg) {
no_mem:
		errstr(_("Cannot create DBUS message: No enough memory.\n"));
		goto out;
	}
	dbus_message_iter_init_append(msg, &args);
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &warn->qtype))
		goto no_mem;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &warn->excess_id))
		goto no_mem;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &warn->warntype))
		goto no_mem;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &warn->dev_major))
		goto no_mem;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &warn->dev_minor))
		goto no_mem;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &warn->caused_id))
		goto no_mem;

	if (!dbus_connection_send(dhandle, msg, NULL)) {
		errstr(_("Failed to write message to dbus: No enough memory.\n"));
		goto out;
	}
	dbus_connection_flush(dhandle);
out:
	if (msg)
		dbus_message_unref(msg);
}

static void run(struct nl_handle *nhandle, struct DBusConnection *dhandle)
{
	struct sockaddr_nl nla;
	struct ucred *creds;
	struct quota_warning warn;
	unsigned char *buf;
	struct nlmsghdr *hdr;
	int ret;

	while (1) {
		ret = nl_recv(nhandle, &nla, &buf, &creds);
		if (ret < 0)
			die(2, _("Read from netlink socket failed: %s\n"), strerror(-ret));
		hdr = (struct nlmsghdr *)buf;
		/* Not message from quota? */
		if (hdr->nlmsg_type != quota_nl_ops.o_id) {
			free(buf);
			continue;
		}
		ret = genl_msg_parser(&quota_nl_ops, &nla, (struct nlmsghdr *)buf, &warn);
		if (!ret) {	/* Parsing successful? */
			if (!(flags & FL_NOCONSOLE))
				write_console_warning(&warn);
			if (!(flags & FL_NODBUS))
				write_dbus_warning(dhandle, &warn);
		}
		else
			errstr(_("Failed parsing netlink command: %s\n"), strerror(errno));
		free(buf);
	}
}

int main(int argc, char **argv)
{
	struct nl_handle *nhandle;
	DBusConnection *dhandle = NULL;

	gettexton();
	progname = basename(argv[0]);
	parse_options(argc, argv);

	nhandle = init_netlink();
	if (!(flags & FL_NODBUS))
		dhandle = init_dbus();
	if (!(flags & FL_NODAEMON)) {
		use_syslog();
		daemon(0, 0);
	}
	run(nhandle, dhandle);
	return 0;
}
