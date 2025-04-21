m4_define([EVILCANDY_VERSION], [0.0.1])
AC_INIT([evilcandy], [EVILCANDY_VERSION], [roscidcup@falstaffandme.com])
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/m4])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 tools/Makefile
 src/Makefile
])
AM_SILENT_RULES([yes])
AC_OUTPUT

echo \
"-------------------------------------------------

${PACKAGE_NAME} Version ${PACKAGE_VERSION}

Prefix: '${prefix}'.
Compiler: '${CC} ${CFLAGS} ${CPPFLAGS}'

Now type 'make @<:@<target>@:>@'
    where the optional <target> is:
        all         - build all binaries
        install     - install everything

--------------------------------------------------"


