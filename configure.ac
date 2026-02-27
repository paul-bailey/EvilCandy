m4_define([EVILCANDY_VERSION], [0.0.1])
AC_INIT([EvilCandy], [EVILCANDY_VERSION], [roscidcup@falstaffandme.com])
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/m4])
AC_CONFIG_SRCDIR([src/types/function.c])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
])

have_uuid=no
AC_CHECK_HEADERS([uuid.h],
        [AC_CHECK_FUNCS([uuid_generate uuid_unparse_lower],
                        [have_uuid=yes])
])
AS_VAR_IF([have_uuid], [no],
        [AC_CHECK_HEADERS([uuid/uuid.h],
                [AC_CHECK_FUNCS([uuid_generate uuid_unparse_lower],
                                [have_uuid=yes])
        ])
])
AS_VAR_IF([have_uuid], [yes], ,[AC_MSG_ERROR([uuid header missing])])

AC_CHECK_TYPES([ssize_t], ,[AC_MSG_ERROR([signed ssize_t not available])])
AC_CHECK_FUNCS([strchrnul strrchrnul strrspn strrstr])

dnl TODO: Check all the headers, AC_CHECK_HEADERS

have_readline=no
AC_CHECK_LIB([readline], [readline],
        [AC_DEFINE([HAVE_READLINE],[1],
                   [Define to 1 if you have the realine function])
         AC_CHECK_LIB([readline], [add_history],
                      [AC_DEFINE([HAVE_ADD_HISTORY],[1],
                                 [Define to 1 if you have the add_history function])
                       have_readline=yes
                      ])
])
AS_VAR_IF([have_readline], [yes],
        [AC_CHECK_HEADERS([readline/readline.h readline/history.h],
                          , [have_readline=no])
])
AS_VAR_IF([have_readline], [yes], ,
          [AC_MSG_ERROR([readline headers missing])
])
AM_CONDITIONAL([WANT_READLINE], [test x$have_readline = "xyes"])

dnl TODO: This is for all the functions of questionable availabilty
dnl not covered in individual checks above
AC_CHECK_FUNCS([atexit clock])

dnl May be useful for speeding up the checksum algo in
dnl serializer.c.  I have written a fast algorithm for this,
dnl but it requires knowing endianness.
AC_C_BIGENDIAN

AM_SILENT_RULES([yes])
AC_DEFINE([_DARWIN_C_SOURCE], [1],
          [Might as well, since Im developing this on Darwin])

dnl all the socket stuff here

AC_CHECK_MEMBERS([struct sockaddr.sa_len],
        [], [], [[#include <sys/types.h>
#include <sys/socket.h>]])
AC_CHECK_MEMBERS([struct sockaddr_un.sun_len],
        [], [], [[#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>]])

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


