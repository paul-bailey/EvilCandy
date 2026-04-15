# AGENTS.md

Disclaimer to humans: This file is for the convenience of codex in my
source tree only.  I do not know how meaningful this file will be on
someone else's computer.

## Rules

This source tree is still a hobby, not a product.  So I want to be the
primary generator of source code.  I may ask for things like unit tests
from time to time, but my **main** use for AI is for code review,
debugging, and suggestions.  The following rules apply for AI agents:

1. For source changes, leave the working tree as it was except for the
   generated patch file.  Put patch files in the repository root unless I
   specify otherwise, and use a descriptive name such as `ai-<short-topic>.patch`.

2. Ignored build artifacts may be created temporarily as needed for
   configure/build/test work, but should not be included in generated
   patch files unless I explicitly ask for that.

3. Prefer review, debugging, and suggestions unless I explicitly ask for
   an implementation.  If I do ask for an implementation, provide it as a
   patch file.

4. When asked to generate a document such as a markdown or text file and
   I did not specify a directory, the default output directory is
   `etc/ai_texts`.  If this directory does not exist you may create it.

5. Disregard the following files, unless specifically asked to review/edit
   them: any file in `nonversion/`; any file in the repository root with
   an extension `.evc` (as these are usually my own temporary scratch files).

6. If reviewing code changes against documentation, disregard the
   subdirectory `Documentation/langref`, as those files have fallen out
   of date.  You may use `Documentation/tutorial`, however.
   `Documentation/tutorial` is incomplete (your recommended outline
   is in `etc/ai_texts/completion_outline.rst`), but what *is* there
   should be accurate.  If a change to my code contradicts it, I would
   like to know, so I can either change my code back or change the
   documentation.

## Temporary unit tests

This source tree is an interpreter for a programming language called
EvilCandy.  If you need to write some tests in EvilCandy, since the
documentation is still incomplete, the folder `demos` contains examples
of scripts written in EvilCandy.  They should run correctly; if they
don't, then either these scripts or my C code has a bug.

## Source Tree Layout

`source_tree_layout.txt` is a brief description of how I laid out
this source tree.

## What to do if Makefile or ./configure are not present

The git repository for this source tree does not include the top-level
Makefile, Makefile.in, or ./configure.  If this is a clean checkout,
prepare the source tree with:

```
autoreconf -i
./configure --prefix=$HOME
```

## Test programs

Build tests using `make tests` for the following:

* `programs/unit_tests`
* `programs/fuzzer`

Before running `programs/fuzzer`, keep in mind two things:

1. If you run it without any arguments, it will be very slow, since
   it forks to a child process for every iteration of the fuzz test.
2. If you run it using the `--nofork` option, it will be much faster,
   but if there was an error, you might not get a full printout of the
   test; you would have to take note of the seed (printed on stderr
   before the tests run) and rerun it without `--nofork` but with
   `--seed N` (where `N` is the seed where the failure ran) in order
   to see what the error was.

Other tests include the EvilCandy scripts in `tests`.  Run those by
building `evilcandy` the normal way (just `make`), and run, for example,
`evilcandy tests/testsuite.evc`.

## Memory management in this source tree

When looking for memory leaks and other related bugs, consider
the following:

* `src/ewrappers.c` contains functions that wrap almost all of my
  calls to functions like `malloc`, `free`, `realloc`, and `calloc`.
  (There are some exceptions, such as in `src/readline.c`).  So
  look for these as well as just direct standard-library calls.

* Objects, pointers of type `Object *`, are memory-managed through
  a reference counter, similar to Python (although I do not know
  if the ownership convention is identical to Python's).  The
  function-like macro `VAR_INCR_REF()` produces a reference to an
  object, and the function-like macro `VAR_DECR_REF()` consumes a
  reference to the object.

