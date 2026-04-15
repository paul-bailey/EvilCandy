/*
 * TODO: Move this -> evilcandy/version.h
 */
#ifndef EVILCANDY_VERSION_H
#define EVILCANDY_VERSION_H

#include "config.h"

#ifdef GIT_VERSION
# define EVILCANDY_VERSION PACKAGE_STRING "-" GIT_VERSION
#else
# define EVILCANDY_VERSION PACKAGE_STRING
#endif

#endif /* EVILCANDY_VERSION_H */
