# C Coding Style for EvilCandy

## Disclaimer

   The following document was written by OpenAI's Codex, not me.  I would
   rather have this file describe the code as it is than have it describe
   what I might wish it to be, because the only coding style that's
   better than "good" is "consistent".  Nevertheless, I have read it over
   and saw that it was good.

   -- Paul Bailey

This document describes the C style used in the EvilCandy source tree.  It is
meant as a consistency guide for contributors, not as a replacement for reading
nearby code.  When in doubt, match the file you are editing.

## General Principles

Keep changes local and boring.  EvilCandy's C code favors direct control flow,
small helper functions, explicit ownership handling, and readable invariants over
large abstractions.

Prefer the existing project helpers and conventions:

- use `emalloc`, `ecalloc`, `erealloc`, `efree`, `estrdup`, and related wrappers
  instead of raw allocation functions in interpreter code;
- use `bug_on()` for internal invariants that should never fail;
- use `err_*()` and `RES_ERROR` for user-visible or recoverable runtime errors;
- use `VAR_INCR_REF()`, `VAR_DECR_REF()`, and `VAR_NEW_REF()` consistently for
  `Object *` ownership.

Tools under `tools/` are looser and may use ordinary libc allocation directly,
but new runtime code should follow the interpreter conventions.

## Files and Includes

Source files normally begin with an optional file comment, then project includes,
then system includes.

```c
/*
 * array.c - Code for managing numerical arrays and tuples
 */
#include <evilcandy/debug.h>
#include <evilcandy/types/array.h>
#include <internal/type_registry.h>

#include <stdlib.h>
#include <string.h>
```

Use installed-style angle includes for project headers:

```c
#include <evilcandy/types/string.h>
#include <internal/types/sequential_types.h>
#include <lib/helpers.h>
```

Generated or private `.c.h` includes may appear where the surrounding file
already uses that pattern.

Headers use include guards, not `#pragma once`.  The guard name should reflect
the include path.

```c
#ifndef EVILCANDY_TYPES_ARRAY_H
#define EVILCANDY_TYPES_ARRAY_H

...

#endif /* EVILCANDY_TYPES_ARRAY_H */
```

Public function declarations in headers use `extern`.

```c
extern Object *arrayvar_new(int n_items);
extern enum result_t array_append(Object *array, Object *child);
```

Group declarations by implementation file when that makes the header easier to
scan.

## Indentation and Whitespace

Indent one level with 8 spaces.  Do not introduce tab characters into files
that currently use spaces.

```c
if (!vm) {
        vm = emalloc(sizeof(*vm) + type->size);
} else {
        type->n_freelist--;
        type->freelist = vm->list;
}
```

Do not align ordinary statements into columns unless the local code already does
it for a compact macro or table.  Simple vertical alignment is common for short
macro constants.

```c
#define V2ARR(v_)       ((struct arrayvar_t *)(v_))
#define V2SQ(v_)        ((struct seqvar_t *)(v_))
```

Keep lines reasonably short.  When wrapping function declarations or calls,
indent continuation lines to make the arguments easy to see.

```c
extern enum result_t array_delete_chunk(Object *array,
                                        size_t at, size_t n_items);

return var_traverse(seq, array_extend_one_item,
                    (void *)array, "extend");
```

Use blank lines between logical sections and between functions.  Avoid extra
vertical whitespace inside small blocks.

## Braces and Control Flow

Use K&R-style braces for functions and control statements.

```c
static Object *
parseatomic(struct json_state_t *j)
{
        Object *child;

        if (j->recursion > 128)
                json_err(j, JE_RECURSION);
        ...
}
```

The return type goes on the line above the function name, including for `static`
functions.

```c
static enum result_t
array_extend_one_item(Object *item, void *data)
{
        ...
}
```

Omit braces for simple one-line `if`, `while`, and `for` bodies.

```c
if (!dict)
        goto err;

while (s < end)
        hash = (hash * FNV_OFFSET) ^ *s++;
```

Use braces when a branch has multiple statements or when it improves clarity in
a larger block.

```c
if (size > at) {
        Object **src = &h->items[at];
        Object **dst = &h->items[at + n_items];
        size_t movsize = (size - at) * sizeof(Object *);
        memmove(dst, src, movsize);
}
```

`switch` labels are indented one level inside the switch, with case bodies
indented one level below the label.

```c
switch (j->tok->t) {
case OC_INTEGER:
case OC_FLOAT:
        child = j->tok->v;
        break;
default:
        child = ErrorVar;
        json_err(j, JE_SYNTAX);
}
```

`goto` is acceptable for shared error handling or cleanup paths.  Use short,
descriptive labels such as `err:` or `resize:` and keep the target nearby.

## Names

Use lower_snake_case for functions and variables.

```c
static void var_free(Object *v);
static Object *vmframe_get_or_alloc(void);
```

Project type names commonly end in `_t` for structs and enums.

```c
struct json_state_t {
        struct token_state_t *tok_state;
        struct token_t *tok;
        jmp_buf env;
        int recursion;
        Object *d;
};
```

Macros and enum constants use upper case when they act as constants or public
operation names.

```c
#define MAX_FREELIST_SIZE       64

enum {
        AF_STRICT       = 0x001,
        AF_RIGHT        = 0x002,
};
```

Short local names are acceptable when the scope is small and the meaning is
obvious (`i`, `j`, `v`, `fr`, `ret`, `idx`).  Prefer clearer names when the
value survives across more than a few lines.

Macros that evaluate arguments should suffix parameter names with an underscore
to reduce accidental collisions.

```c
#define VM2VAR(x_)      ((Object *)((x_) + 1))
#define VAR2VM(x_)      (((struct var_mem_t *)(x_)) - 1)
```

## Types, Pointers, and Declarations

Attach `*` to the declarator, not the base type.

```c
Object *child;
struct var_mem_t *vm;
```

Declare locals near the start of a function or block, but it is acceptable to
open a small inner block when declarations are only needed for one `case` or
branch.

```c
case 's':
            {
                char *s = va_arg(ap, char *);
                string_writer_appends(wr, s);
                break;
            }
```

Use `sizeof(*ptr)` when allocating an object through a pointer.

```c
ret = ecalloc(sizeof(*ret));
va->items = erealloc(va->items, new_size);
```

Prefer project typedefs where the surrounding code uses them (`Object`, `Frame`,
`hash_t`, `instruction_t`).  Use standard integer and size types (`size_t`,
`ssize_t`, `bool`) where appropriate.

## Functions

Keep functions focused.  File-local helpers should be `static`; use external
linkage only for API functions or callbacks needed from other translation units.

For callbacks, mention the protocol or callback slot in the comment when useful.

```c
/**
 * array_getitem - seq_methods_t .getitem callback
 *
 * Given extern linkage since some internal code needs it
 */
Object *
array_getitem(Object *array, size_t idx)
{
        ...
}
```

Return `enum result_t` with `RES_OK` and `RES_ERROR` for operations that mutate
objects or can fail without returning an object.

Return `ErrorVar` for object-returning functions when that is the established
contract for the caller.  Set an error before returning an error value unless
the helper you called already did so.

## Comments and Documentation

Use comments to explain invariants, ownership, non-obvious control flow, or
project history that affects the current code.  Avoid comments that simply
restate a line of code.

Block comments use the existing C style:

```c
/*
 * Similar to our hash table algorithm, except we don't need to
 * worry about collisions, so alpha=.75 is fine.
 */
```

Function comments often use a kernel-doc-like form:

```c
/**
 * array_delete_chunk - Delete a section of an array and collapse it down
 * @array: Array to delete from
 * @at: Start position in array
 * @n_items: Number of items to remove
 *
 * Return: RES_OK or RES_ERROR
 */
```

Use `DOC:` comments for longer design notes that explain a subsystem or
algorithm.

```c
/*
 * DOC: Frame allocation
 *
 *      When returning from one function it will probably not be long
 *      before we call another one.
 */
```

Existing code uses `XXX`, `FIXME`, `TODO`, and `REVISIT`.  Prefer `FIXME` for a
known defect, `TODO` for intended work, and `XXX` for a warning or questionable
tradeoff.

## Memory and Object Ownership

Most interpreter allocation should go through the EvilCandy wrappers:

```c
ptr = emalloc(size);
ptr = erealloc(ptr, new_size);
efree(ptr);
```

`efree()` expects a non-NULL pointer.  Check for NULL before calling it if NULL
is possible.

For `Object *`, be explicit about reference ownership:

- `VAR_INCR_REF(obj)` creates an owned reference;
- `VAR_DECR_REF(obj)` consumes an owned reference;
- `VAR_NEW_REF(obj)` returns `obj` after incrementing its reference count;
- functions named `borrow...` normally return a borrowed reference and should
  document or implement that contract carefully.

When storing an `Object *` in a container, increment the reference unless the
called API explicitly consumes one.

```c
VAR_INCR_REF(children[i]);
h->items[j++] = children[i];
```

When removing objects from containers, decrement references before overwriting or
discarding the slots.

```c
for (i = at; i < at + n_items; i++)
        VAR_DECR_REF(h->items[i]);
```

## Error Handling and Assertions

Use `bug_on()` for internal programmer errors and impossible states.

```c
bug_on(!isvar_array(array));
bug_on(idx >= seqvar_size(array));
```

Use runtime error helpers for user-visible failures.

```c
if (h->lock) {
        err_locked();
        return RES_ERROR;
}
```

Fatal system failures should go through `fail()`, usually indirectly through the
allocation wrappers.

Avoid returning ambiguous failure values without setting or preserving an error.
If a helper can fail and may not set an error, add the error at the call site.

## Preprocessor Style

Use macros for small casts, generated-table glue, debug hooks, and constants.
Keep function-like macros parenthesized and use `do { ... } while (0)` for
multi-statement macros.

```c
#define REGISTER_ALLOC(n_) do { \
        var_nalloc++;           \
} while (0)
```

For disabled debug macros, preserve type checking or silence unused values with
`(void)0`.

```c
# define REGISTER_ALLOC(x) do { (void)0; } while (0)
```

Keep `#if` / `#else` / `#endif` comments when the block is nontrivial.

```c
#else /* !DBUG_REPORT_VARS_ON_EXIT */
...
#endif /* !DBUG_REPORT_VARS_ON_EXIT */
```

## Structs and Inline Helpers

Put private structs in `.c` files or internal headers.  Public headers should
expose only what callers need.

Small accessors may be `static inline` in internal headers, with the whole
function on one line when it remains readable.

```c
static inline Object **array_get_data(Object *v)
        { return ((struct arrayvar_t *)v)->items; }
```

Warn callers in comments when an inline helper assumes type checking or checked
indexes.

```c
/* Warning!! Only call these if you already type-checked @v */
```

## Contributor Checklist

Before submitting C changes, check that:

- indentation and braces match the edited file;
- new runtime allocation uses the `e*` wrappers;
- every stored or returned `Object *` has a clear reference ownership story;
- failure paths set or preserve an EvilCandy error;
- internal invariants use `bug_on()` rather than silent undefined behavior;
- public declarations are added to the appropriate header with `extern`;
- comments explain decisions and contracts, not obvious syntax.
