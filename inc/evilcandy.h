/*
 * TODO: Move this -> evilcandy/version.h
 */
#ifndef EVILCANDY_H
#define EVILCANDY_H

#include "config.h"

#ifdef GIT_VERSION
# define EVILCANDY_VERSION PACKAGE_STRING "-" GIT_VERSION
#else
# define EVILCANDY_VERSION PACKAGE_STRING
#endif

#endif /* EVILCANDY_H */
