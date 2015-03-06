ACLOCAL_AMFLAGS = -I m4

docdir = $(datadir)/doc/@PACKAGE@
doc_DATA = \
	README.mailserver \
	README.ldap-support \
	set_limits_example.c \
	doc/quotadoc.sgml \
	doc/quotas.ms \
	doc/quotas.preformated \
	doc/quotas-1.eps

rpcsvcdir = $(includedir)/rpcsvc
rpcsvc_DATA = \
	rquota.h \
	rquota.x

sysconf_DATA = \
	warnquota.conf \
	quotatab \
	quotagrpadmins

man_MANS = \
	convertquota.8 \
	edquota.8 \
	quotagrpadmins.5 \
	quotacheck.8 \
	quota_nld.8 \
	quotaon.8 \
	quotastats.8 \
	quotasync.1 \
	quotatab.5 \
	quota.1 \
	quot.8 \
	repquota.8 \
	rpc.rquotad.8 \
	rquota.3 \
	setquota.8 \
	warnquota.conf.5 \
	warnquota.8 \
	xqmstats.8

CLEANFILES = rquota.c rquota.h rquota_clnt.c

SUBDIRS = po

EXTRA_DIST = \
	$(sysconf_DATA) \
	$(doc_DATA) \
	$(man_MANS) \
	$(rpcsvc_DATA) \
	autogen.sh

noinst_LIBRARIES = \
	libquota.a \
	librpcclient.a

librpcclient_a_SOURCES = \
	rquota.c \
	rquota.h \
	rquota_client.c \
	rquota_client.h \
	rquota_clnt.c
librpcclient_a_CFLAGS = -Wno-unused

if WITH_RPC
RPCLIBS = librpcclient.a
endif

libquota_a_SOURCES = \
	dqblk_rpc.h \
	dqblk_v1.h \
	dqblk_v2.h \
	dqblk_xfs.h \
	quotaio.c \
	quotaio.h \
	quotaio_v1.c \
	quotaio_v1.h \
	quotaio_v2.c \
	quotaio_v2.h \
	quotaio_tree.c \
	quotaio_rpc.c \
	quotaio_xfs.c \
	quotaio_xfs.h \
	quotaio_meta.c \
	quotaio_generic.c \
	quotaio_generic.h \
	bylabel.c \
	bylabel.h \
	common.c \
	config.h \
	common.h \
	quotasys.c \
	quotasys.h \
	mntopt.h \
	pot.c \
	pot.h
libquota_a_LIBADD = \
	$(RPCLIBS)

bin_PROGRAMS = \
	quota \
	quotasync

sbin_PROGRAMS = \
	quotacheck \
	quotaon \
	quot \
	repquota \
	warnquota \
	quotastats \
	xqmstats \
	edquota \
	setquota \
	convertquota \
	rpc.rquotad
if WITH_NETLINK
sbin_PROGRAMS += \
	quota_nld
endif

quotaon_SOURCES = \
	quotaon.c \
	quotaon.h \
	quotaon_xfs.c
quotaon_LDADD = \
	libquota.a \
	$(RPCLIBS)

quotacheck_SOURCES = \
	quotacheck.c \
	quotacheck.h \
	quotacheck_v1.c \
	quotacheck_v2.c \
	quota_tree.h \
	quotaops.c
quotacheck_CFLAGS = \
	$(EXT2FS_CFLAGS) \
	$(COM_ERR_CFLAGS)
quotacheck_LDADD = \
	libquota.a \
	$(EXT2FS_LIBS) \
	$(COM_ERR_LIBS) \
	$(RPCLIBS)

quota_SOURCES = \
	quota.c \
	quota.h \
	quotaops.c \
	quotaops.h
quota_LDADD = \
	libquota.a \
	$(RPCLIBS)

quotasync_SOURCES = quotasync.c
quotasync_LDADD = \
	libquota.a \
	$(RPCLIBS)

quot_SOURCES = \
	quot.c \
	quot.h
quot_LDADD = \
	libquota.a \
	$(RPCLIBS)

repquota_SOURCES = repquota.c
repquota_LDADD = \
	libquota.a \
	$(RPCLIBS)

warnquota_SOURCES = warnquota.c
warnquota_LDADD = \
	libquota.a \
	$(LDAP_LIBS) \
	$(RPCLIBS)

quotastats_SOURCES = \
	quotastats.c \
	common.c \
	common.h \
	pot.c \
	pot.h

xqmstats_SOURCES = \
	xqmstats.c \
	common.c \
	common.h \
	pot.c \
	pot.h

edquota_SOURCES = \
	edquota.c \
	quotaops.c \
	quotaops.h
edquota_LDADD = \
	libquota.a \
	$(RPCLIBS)

setquota_SOURCES = \
	setquota.c \
	quotaops.c \
	quotaops.h
setquota_LDADD = \
	libquota.a \
	$(RPCLIBS)

convertquota_SOURCES = convertquota.c
convertquota_LDADD = \
	libquota.a \
	$(RPCLIBS)

rpc_rquotad_SOURCES = \
	rquota_server.c \
	rquota_svc.c \
	svc_socket.c
rpc_rquotad_LDADD = \
	libquota.a \
	$(WRAP_LIBS) \
	$(RPCLIBS)

quota_nld_SOURCES = quota_nld.c
quota_nld_CFLAGS = \
	$(DBUS_CFLAGS) \
	$(LIBNL3_CFLAGS)
quota_nld_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(DBUS_LIBS) \
	$(LIBNL3_LIBS)

# ------------------
# Rpcgen conversions
# ------------------
%.h: %.x
	@rm -f $@
	@$(RPCGEN) -h -o $@ $^

%.c: %.x
	@rm -f $@
	@$(RPCGEN) -c -o $@ $^

rquota_clnt.c: rquota.x
	@rm -f $@
	@$(RPCGEN) -l -o $@ $<

# --------
# Quotaoff
# --------

install-exec-hook:
	rm -f $(DESTDIR)$(sbindir)/quotaoff
	@$(LN_S) quotaon $(DESTDIR)$(sbindir)/quotaoff

install-data-hook:
	rm -f $(DESTDIR)$(mandir)/man8/quotaoff.8
	@$(LN_S) quotaon.8 $(DESTDIR)$(mandir)/man8/quotaoff.8

# -------------
# Extra options
# -------------
distclean-local:
	rm -rf *.cache *~

dist-hook:
	git log --date=short --pretty="format:@%cd  %an  <%ae> [%H]%n%n%s%n%n%e%b" | sed -e "s|^\([^@]\)|\t\1|" -e "s|^@||" >$(distdir)/ChangeLog
