#ifndef TYPES_PRIV_H
#define TYPES_PRIV_H

#include <evilcandy.h>
#include <typedefs.h>
#include <uarg.h>

/*
 * can't just be a-b, because if they're floats, a non-zero result
 * might cast to 0
 */
#define OP_CMP(a_, b_) (a_ == b_ ? 0 : (a_ < b_ ? -1 : 1))


#endif /* TYPES_PRIV_H */
