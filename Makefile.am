ACLOCAL_AMFLAGS = -I m4
AM_LDFLAGS = -pie

doc_DATA = \
	README.mailserver \
	README.ldap-support \
	set_limits_example.c \
	doc/quotadoc.sgml \
	doc/quotas.ms \
	doc/quotas.preformated \
	doc/quotas-1.eps

sysconf_DATA = \
	warnquota.conf \
	quotatab \
	quotagrpadmins

man_MANS = \
	convertquota.8 \
	edquota.8 \
	quotagrpadmins.5 \
	quotacheck.8 \
	quotaon.8 \
	quotastats.8 \
	quotasync.1 \
	quotatab.5 \
	quota.1 \
	quot.8 \
	repquota.8 \
	setquota.8 \
	warnquota.conf.5 \
	warnquota.8 \
	xqmstats.8

if WITH_NETLINK
man_MANS += quota_nld.8
endif

SUBDIRS = po

EXTRA_DIST = \
	$(sysconf_DATA) \
	$(doc_DATA) \
	$(man_MANS) \
	$(rpcsvc_DATA) \
	autogen.sh \
	Changelog \
	ldap-scripts

noinst_LIBRARIES = libquota.a

if WITH_RPC
rpcsvcdir = $(includedir)/rpcsvc
rpcsvc_DATA = \
	rquota.h \
	rquota.x

noinst_LIBRARIES += librpcclient.a

librpcclient_a_SOURCES = \
	rquota.c \
	rquota.h \
	rquota_client.c \
	rquota_client.h \
	rquota_clnt.c
librpcclient_a_CFLAGS = -Wno-unused

RPCLIBS = librpcclient.a
BUILT_SOURCES = rquota.h rquota.c rquota_clnt.c
CLEANFILES = rquota.c rquota.h rquota_clnt.c
man_MANS += \
	rpc.rquotad.8 \
	rquota.3

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
libquota_a_CFLAGS = \
	$(TIRPC_CFLAGS)
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
	convertquota
if WITH_RPC
sbin_PROGRAMS += \
	rpc.rquotad
endif
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
	$(RPCLIBS) \
	$(TIRPC_LIBS)

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
	$(RPCLIBS) \
	$(TIRPC_LIBS)

quota_SOURCES = \
	quota.c \
	quota.h \
	quotaops.c \
	quotaops.h
quota_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(TIRPC_LIBS)

quotasync_SOURCES = quotasync.c
quotasync_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(TIRPC_LIBS)

quot_SOURCES = \
	quot.c \
	quot.h
quot_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(TIRPC_LIBS)

repquota_SOURCES = repquota.c
repquota_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(TIRPC_LIBS)

warnquota_SOURCES = warnquota.c
warnquota_LDADD = \
	libquota.a \
	$(LDAP_LIBS) \
	$(RPCLIBS) \
	$(TIRPC_LIBS)

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
	$(RPCLIBS) \
	$(TIRPC_LIBS)

setquota_SOURCES = \
	setquota.c \
	quotaops.c \
	quotaops.h
setquota_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(TIRPC_LIBS)

convertquota_SOURCES = convertquota.c
convertquota_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(TIRPC_LIBS)

if WITH_RPC
rpc_rquotad_SOURCES = \
	rquota_server.c \
	rquota_svc.c \
	svc_socket.c
rpc_rquotad_CFLAGS = $(TIRPC_CFLAGS)
rpc_rquotad_LDADD = \
	libquota.a \
	$(WRAP_LIBS) \
	$(RPCLIBS) \
	$(TIRPC_LIBS)
endif

quota_nld_SOURCES = quota_nld.c
quota_nld_CFLAGS = \
	$(DBUS_CFLAGS) \
	$(LIBNL3_CFLAGS)
quota_nld_LDADD = \
	libquota.a \
	$(RPCLIBS) \
	$(TIRPC_LIBS) \
	$(DBUS_LIBS) \
	$(LIBNL3_LIBS)

if WITH_RPC
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
endif

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
