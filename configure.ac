# Process this file with autoconf to create configure.

AC_PREREQ([2.60])

# ====================
# Version informations
# ====================
m4_define([quota_version_major],[4])
m4_define([quota_version_minor],[09])
m4_define([quota_version],[quota_version_major.quota_version_minor])

# =============
# Automake init
# =============
AC_INIT([quota],[quota_version], [jack@suse.cz])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([1.11 foreign dist-xz dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_LANG([C])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

# ===========================
# Find required base packages
# ===========================
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
AC_PROG_CC([cc gcc clang])
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG([0.20])

AC_C_CONST
AC_C_INLINE
AC_CHECK_FUNC([nl_langinfo], [
    AC_DEFINE([HAVE_NL_LANGINFO], 1, [Use nl_langinfo for querying locale])
])

# ===============
# Gettext support
# ===============
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19])

AS_IF([test "$enable_nls" != "no"], [
    enable_nls="yes"
    AC_DEFINE([USE_GETTEXT], 1, [Use gettext to translate messages])
])
AS_IF([test "x${prefix}" = "xNONE"], [
  AC_DEFINE_UNQUOTED(PACKAGE_LOCALE_DIR, "${ac_default_prefix}/share/locale", [Locale-specific data directory])
], [
  AC_DEFINE_UNQUOTED(PACKAGE_LOCALE_DIR, "${prefix}/share/locale", [Locale-specific data directory])
])

# ================
# Check for ldflags
# ================
AC_ARG_ENABLE([pie],
   [AS_HELP_STRING([--disable-pie], [Disable Position Independent Executable])],
   [enable_pie="$enableval"],
   [enable_pie=yes]
)
AS_IF([test x"$enable_pie" != "xno"], [
    PIE_LDFLAGS="-pie"
])
AC_SUBST([PIE_LDFLAGS])

# ================
# Check for cflags
# ================
WARN_CFLAGS="-Wall"
AC_ARG_ENABLE([werror],
    [AS_HELP_STRING([--enable-werror], [Treat all warnings as errors, useful for development])],
    [enable_werror="$enableval"],
    [enable_werror=no]
)
AS_IF([test x"$enable_werror" != "xno"], [
    WARN_CFLAGS="$WARN_CFLAGS -Werror"
])
AC_SUBST([WARN_CFLAGS])

AC_CHECK_SIZEOF([time_t], [], [#include <time.h>])

# =========
# Find ldap
# =========
AC_ARG_ENABLE([ldapmail],
    [AS_HELP_STRING([--disable-ldapmail], [Do not use the ldap mail address lookups.])],
    [enable_ldapmail="$enableval"],
    [enable_ldapmail=auto]
)
AS_IF([test "x$enable_ldapmail" != "xno"], [
    AC_CHECK_LIB([ldap], [ldap_initialize], [
	build_ldap="yes"
        LDAP_LIBS="-lldap -llber"
        AC_DEFINE([USE_LDAP_MAIL_LOOKUP], 1, [Lookup email address using LDAP])
        COMPILE_OPTS="$COMPILE_OPTS USE_LDAP_MAIL_LOOKUP"
    ], [
        build_ldap="no"
        AS_IF([test "x$enable_ldapmail" = "xyes"], [
            AC_MSG_ERROR([LDAP support required but library not found.]);
        ])
    ])
], [
    build_ldap="no"
])
AC_SUBST(LDAP_LIBS)

# ===============
# Find ext2direct
# ===============
AC_ARG_ENABLE([ext2direct],
    [AS_HELP_STRING([--disable-ext2direct], [Disable scanning of EXT2/EXT3 filesystem using e2fslib.])],
    [enable_ext2direct="$enableval"],
    [enable_ext2direct=auto]
)
AS_IF([test "x$enable_ext2direct" != "xno"], [
    build_ext2direct="yes"
    PKG_CHECK_MODULES([COM_ERR],[com_err], [], [
        build_ext2direct="no"
        AS_IF([test "x$enable_ext2direct" = "xyes"], [
            AC_MSG_ERROR([Ext2direct requested but required com_err library not found])
        ])
    ])
    PKG_CHECK_MODULES([EXT2FS],[ext2fs], [], [
        build_ext2direct="no"
        AS_IF([test "x$enable_ext2direct" = "xyes"], [
            AC_MSG_ERROR([Ext2direct requested but required ext2fs library not found])
        ])
    ])
    AS_IF([test "x$build_ext2direct" != "xno"], [
        AC_DEFINE([EXT2_DIRECT], 1, [Scanning of ext? filesystems using e2fslib])
        COMPILE_OPTS="$COMPILE_OPTS EXT2_DIRECT"
    ])
], [
   build_ext2direct="no"
])
AC_SUBST(EXT2FS_CFLAGS)
AC_SUBST(EXT2FS_LIBS)
AC_SUBST(COM_ERR_CFLAGS)
AC_SUBST(COM_ERR_LIBS)

# ==================
# Netlink enablement
# ==================
AC_ARG_ENABLE([netlink],
    [AS_HELP_STRING([--disable-netlink], [Do not compile daemon receiving quota messages via netlink.])],
    [enable_netlink="$enableval"],
    [enable_netlink=auto]
)
AS_IF([test "x$enable_netlink" != "xno"], [
    build_netlink="yes"
    PKG_CHECK_MODULES([DBUS], [dbus-1], [], [
        build_netlink="no"
        AS_IF([test "x$enable_netlink" = "xyes"] , [
            AC_MSG_ERROR([Required dbus libraries for quota netlink daemon not found.])
        ])
    ])
    PKG_CHECK_MODULES([LIBNL3], [libnl-3.0 >= 3.1 libnl-genl-3.0], [], [
        build_netlink="no"
        AS_IF([test "x$enable_netlink" = "xyes"] , [
            AC_MSG_ERROR([Required libnl3 libraries for quota netlink daemon not found.])
        ])
    ])
], [
    build_netlink="no"
])
AM_CONDITIONAL([WITH_NETLINK], [test "x$build_netlink" != "xno"])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)
AC_SUBST(LIBNL3_CFLAGS)
AC_SUBST(LIBNL3_LIBS)

# =============
# libwrap usage
# =============
AC_ARG_ENABLE([libwrap],
    [AS_HELP_STRING([--disable-libwrap], [Do not use hosts.* for access checking on rpc.quotad.])],
    [enable_libwrap="$enableval"],
    [enable_libwrap=auto]
)
AS_IF([test "x$enable_libwrap" != "xno"], [
    AC_CHECK_HEADER([tcpd.h], [
    	build_libwrap="yes"
        WRAP_LIBS="-lwrap"
        AC_DEFINE([HOSTS_ACCESS], 1, [Use hosts.allow and hosts.deny for access checking of rpc.rquotad])
        COMPILE_OPTS="$COMPILE_OPTS HOSTS_ACCESS"
    ], [
        build_libwrap="no"
        AS_IF([test "x$enable_libwrap" = "xyes"] , [
            AC_MSG_ERROR([tcpd.h not found and requested])
        ], [
            AC_MSG_WARN([tcpd.h not found])
        ])
    ])
], [
    build_libwrap="no"
])
AC_SUBST(WRAP_LIBS)

# =====================
# various build options
# =====================
AC_ARG_ENABLE([rpc],
    [AS_HELP_STRING([--disable-rpc], [Disable RPC support])],
    [enable_rpc="$enableval"],
    [enable_rpc=auto]
)
AS_IF([test x"$enable_rpc" != "xno"], [
    AC_PATH_PROG([RPCGEN], [rpcgen])
    AC_ARG_VAR([RPCGEN], [RPC compiler command])
    build_rpc="yes"
    AS_IF([test x"$RPCGEN" = "x"], [
	AS_IF([test x"$enable_rpc" = "xyes"], [
	    AC_MSG_ERROR([could not locate required rpcgen])
	], [
	    AC_MSG_WARN([rpcgen not found])
	])
	build_rpc="no"
    ])

    PKG_CHECK_MODULES([TIRPC], [libtirpc], [], [
	AS_IF([test x"$enable_rpc" = "xyes"], [
	    AC_MSG_ERROR([could not locate required libtirpc])
	], [
	    AC_MSG_WARN([libtirpc not found])
	])
	build_rpc="no"
    ])
	
    AS_IF([test x"$build_rpc" != "xno"], [
	AC_DEFINE([RPC], 1, [Support for RPC])
	COMPILE_OPTS="$COMPILE_OPTS RPC"
    ])
], [
    build_rpc="no"
])
AM_CONDITIONAL([WITH_RPC], [test x"$build_rpc" != "xno"])

AC_ARG_ENABLE([rpcsetquota],
    [AS_HELP_STRING([--enable-rpcsetquota], [Use RPC for setting quotas])],
    [enable_rpcsetquota="$enableval"],
    [enable_rpcsetquota=no]
)
AS_IF([test x"$enable_rpcsetquota" = "xyes"], [
    AC_DEFINE([RPC_SETQUOTA], 1, [Allow setting of quotas over RPC])
    COMPILE_OPTS="$COMPILE_OPTS RPC_SETQUOTA"
])

AC_ARG_ENABLE([xfs-roothack],
    [AS_HELP_STRING([--enable-xfs-roothack], [Support old XFS root filesystems.])],
    [enable_xfs_roothack="$enableval"],
    [enable_xfs_roothack=no]
)
AS_IF([test x"$enable_xfs_roothack" = "xyes"], [
    AC_DEFINE([XFS_ROOTHACK], 1, [Assume quota mount options for root filesystem])
    COMPILE_OPTS="$COMPILE_OPTS XFS_ROOTHACK"
])

AC_ARG_ENABLE([bsd-behaviour],
    [AS_HELP_STRING([--disable-bsd-behaviour], [Mimic BSD behaviour])],
    [enable_bsd_behaviour="$enableval"],
    [enable_bsd_behaviour="no"]
)
AS_IF([test x"$enable_bsd_behaviour" != "xno"], [
    AC_DEFINE([BSD_BEHAVIOUR], 1, [Check rights to query / set quotas before calling quotactl])
    COMPILE_OPTS="$COMPILE_OPTS BSD_BEHAVIOUR"
])

AC_ARG_WITH([proc-mounts],
    [AS_HELP_STRING([--with-proc-mounts], [Use alternate mtab file instead of /proc/mounts])],
    [with_proc_mounts="$withval"],
    [with_proc_mounts="/proc/mounts"]
)
AS_IF([test "$with_proc_mounts" != "no"], [
    AC_DEFINE_UNQUOTED([ALT_MTAB], ["$with_proc_mounts"], [File with mounted filesystems])
])

AC_ARG_WITH([pid-dir],
    [AS_HELP_STRING([--with-pid-dir=DIRECTORY], [Create PID files in this directory instead of /var/run])],
    [with_pid_dir="$withval"]
)
AS_IF([test "X$with_pid_dir" = "X" -o "$with_pid_dir" = "yes" -o "$with_pid_dir" = "no"],[
    with_pid_dir="/var/run"
])
AC_DEFINE_UNQUOTED([PID_DIR], ["$with_pid_dir"], [Directory for PID files])

AC_DEFINE_UNQUOTED([COMPILE_OPTS], ["$COMPILE_OPTS"], [Configuration options])

AC_CONFIG_FILES([
Makefile
po/Makefile.in
])
AC_OUTPUT

# ==============================================
# Display final informations about configuration
# ==============================================
AC_MSG_NOTICE([
==============================================================================
Build configuration:
	bsd-behaviour:   ${enable_bsd_behaviour}
	ext2-direct:     ${build_ext2direct}
	ldap-mail:       ${build_ldap}
	libwrap:         ${build_libwrap}
	netlink:         ${build_netlink}
	nls:             ${enable_nls}
	pid-dir:         ${with_pid_dir}
	proc-mounts:     ${with_proc_mounts}
	rpc:             ${build_rpc}
	rpcsetquota:     ${enable_rpcsetquota}
	xfs-roothack:    ${enable_xfs_roothack}
	werror:          ${enable_werror}
==============================================================================
])
