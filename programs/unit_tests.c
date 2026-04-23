#include <evilcandy/assemble.h>
#include <evilcandy/debug.h>
#include <evilcandy/global.h>
#include <evilcandy/vm.h>
#include <evilcandy/err.h>
#include <evilcandy/var.h>
#include <internal/init.h>
#include <internal/path.h>
#include <internal/locations.h>
#include <tests/tap.h>

#include <assert.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

static void
test_good_snippet_in_child(const char *snippet)
{
        Object *xptr, *result;

        xptr = assemble_string(snippet, false);
        assert(xptr != NULL);
        assert(xptr != ErrorVar);

        result = vm_exec_script(xptr, NULL);
        /* Pass or fail, result should never be NULL */
        assert(result != NULL);
        assert(!err_occurred());
        assert(result != ErrorVar);

        VAR_DECR_REF(result);
}

static void
test_bad_rt_snippet_in_child(const char *snippet)
{
        Object *xptr, *result;

        xptr = assemble_string(snippet, false);
        assert(xptr != NULL);
        assert(xptr != ErrorVar);

        result = vm_exec_script(xptr, NULL);
        /* Pass or fail, result should never be NULL */
        assert(result != NULL);
        assert(err_occurred());
        assert(result == ErrorVar);

        err_clear();
}

static void
test_bad_syntax_snippet_in_child(const char *snippet)
{
        Object *xptr = assemble_string(snippet, false);
        assert(xptr == ErrorVar);
        assert(err_occurred());

        err_clear();
}

static enum result_t
fork_and_test_snippets(const char **snippet,
                       void (*cb)(const char *), bool all)
{
        int status = 0;
        int waited = 0;
        pid_t pid;

        /*
         * flush to allow exit() instead of _exit() in child process.
         * I want child's assert messages to fully print.
         */
        fflush(stdout);
        fflush(stderr);

        pid = fork();
        if (pid == 0) {
                initialize_program();
                if (all) {
                        while (*snippet) {
                                cb(*snippet);
                                snippet++;
                        }
                } else {
                        cb(*snippet);
                }
                end_program();
                exit(EXIT_SUCCESS);
                return RES_OK;
        }
        if (pid == (pid_t)-1) {
                perror("fork failed");
                exit(EXIT_FAILURE);
        }

        while (waited < 100) {
                if (waitpid(pid, &status, WNOHANG) != 0)
                        break;
                usleep(1000);
                waited++;
        }

        if (waited == 100) {
                kill(pid, SIGKILL);
                fprintf(stderr, "Program timed out\n");
                goto errmsg;
        }

        if (WIFSIGNALED(status)) {
                fprintf(stderr, "Program crashed! signal %d\n",
                        WTERMSIG(status));
                if (WCOREDUMP(status))
                        fprintf(stderr, "core dump generated\n");
                goto errmsg;
        }

        if (WIFEXITED(status)) {
                if (status != EXIT_SUCCESS) {
                        fprintf(stderr,
                                "Program exited with status %d\n",
                                WEXITSTATUS(status));
                        goto errmsg;
                }
        }

        return RES_OK;

errmsg:
        if (all) {
                fprintf(stderr,
                        "Test failed running all snippets "
                        "in the same process");
        } else {
                fprintf(stderr, "Test failed at snippet: %s\n",
                        *snippet);
        }
        return RES_ERROR;
}

/*
 * The following snippets are pieces of code which assemble()
 * will compile, but which would cause a runtime error when
 * executed.
 */
static const char *RUNTIME_ERROR_SNIPPETS[] = {
        /* core VM error families */
        "missing_name;",
        "let x = 1; x();",
        "1 + 'x';",
        "1['x'];",
        "[1][99];",
        "[1][-99];",
        "({'a': 1}['b']);",
        "null.foo;",
        "delete missing_name;",
        "throw RuntimeError('boom');",
        "throw 'not an exception';",

        /*
         * TODO: EvilCandy doesn't yet catch this kind of keyword misuse;
         * Remove the "#if 0" when I add code to do that.
         */
#if 0
        "print(unknown_kw=1);",
        "(function(**kw) {} )(a=1, a=2);",
#endif
        /* argument and call errors */
        "length();",
        "length(1, 2);",
        "int('not an int');",
        "range('x');",
        "(function(a) {} )();",
        "(function(a) {} )(1, 2);",

        /* control-flow cleanup and unwinding */
        "function f() { missing_name; } f();",
        "try { missing_name; } catch(e) { throw e; }",
        "for x in 1 { print(x); }",
        "for a, b in [1] { print(a); }",
        "(function() { for x in [1] { missing_name; } })();",

        /* user object and protocol errors */
        "class C() {};C().missing;",
        "class C() {};C().missing();",

        /* regression checks - fuzz tests which used to crash */
        "{let bar=bar(y=(38));}",
        "class C by inner () {}; let c = C(); c.inner = c; c.missing;",

        "class C by inner () {}\nlet b = C();\nlet c = C();\nb.inner = c;\nc.inner = b;\nb.missing;",

        NULL
};

static const char *RUNTIME_GOOD_SNIPPETS[] = {
        /*
         * Regression checks - these had thrown errors when they should
         * have been okay.
         */
        /* non-string keys were not matching properly in dict.c */
        "let a = {b'abcd': 1}; let b = a[b'abcd'];",
        NULL
};

static const char *SYNTAX_BAD_SNIPPETS[] = {
        /* regression check gh issue #55 */
        "string('1') abc;",
        NULL
};

static enum result_t
test_snippets(bool stop_on_failure,
                        const char **snippets,
                        void (*cb)(const char *))
{
        enum result_t ret = RES_OK;
        const char **ppsave = snippets;
        while (*snippets != NULL) {
                enum result_t tres;
                tres = fork_and_test_snippets(snippets, cb, false);
                if (tres == RES_ERROR) {
                        ret = tres;
                        if (stop_on_failure)
                                return ret;
                }
                snippets++;
        }
        if (ret == RES_OK) {
                /*
                 * Re-test all in a single context, to see how well we
                 * manage re-executing upon error.
                 */
                ret = fork_and_test_snippets(ppsave, cb, true);
        }
        return ret;
}

/*
 * XXX: This isn't a unit test per-se, maybe it should go in its own
 * program.
 */
static enum result_t
test_runtime_error(bool stop_on_failure)
{
        return test_snippets(stop_on_failure,
                             RUNTIME_ERROR_SNIPPETS,
                             test_bad_rt_snippet_in_child);
}

static enum result_t
test_runtime_noerror(bool stop_on_failure)
{
        return test_snippets(stop_on_failure,
                             RUNTIME_GOOD_SNIPPETS,
                             test_good_snippet_in_child);
}

static enum result_t
test_syntax_error(bool stop_on_failure)
{
        return test_snippets(stop_on_failure,
                             SYNTAX_BAD_SNIPPETS,
                             test_bad_syntax_snippet_in_child);
}

static bool
test_rpip(const char *trypath, const char *expect)
{
        char path[1000];
        bug_on(strlen(trypath) > sizeof(path)-1);
        strcpy(path, trypath);
        reduce_pathname_in_place(path);
        return !strcmp(path, expect);
}

static void
test_path(struct tap_t *tap)
{
        /* TODO: a bunch more */
        tap_test(tap, test_rpip("/../a", "/a"));
        tap_test(tap, test_rpip("////a/././b", "/a/b"));
        tap_test(tap, test_rpip("/a/b/c", "/a/b/c"));
        tap_test(tap, test_rpip("/a/../../../b/./.c/..c", "/b/.c/..c"));
}

#define RETURN_IF_ERROR(tap_, cond_) \
        do { if (tap_test(tap_, cond_) == RES_ERROR) return; } while (0)

static void
test_locations(struct tap_t *tap)
{
        enum { LOC_BUFSIZE = 128 };
        unsigned char buf[LOC_BUFSIZE];
        struct location_t loc;
        ssize_t size, size2;
        enum result_t res;

        memset(&loc, 0, sizeof(loc));
        loc.loc_startline = 0;
        loc.loc_instruction = 0;
        size = location_pack(buf, sizeof(buf), &loc);
        /* minimum 1 byte per field */
        RETURN_IF_ERROR(tap, size >= 2);

        res = location_unpack(buf, size, 0, &loc);
        RETURN_IF_ERROR(tap, res == RES_OK);
        RETURN_IF_ERROR(tap, loc.loc_startline == 0);
        RETURN_IF_ERROR(tap, loc.loc_instruction == 0);

        res = location_unpack(buf, size, 1, &loc);
        /* backup would have been used */
        RETURN_IF_ERROR(tap, res == RES_OK);
        RETURN_IF_ERROR(tap, loc.loc_startline == 0);
        RETURN_IF_ERROR(tap, loc.loc_instruction == 0);

        loc.loc_startline = 1;
        loc.loc_instruction = 1;
        size2 = location_pack(buf + size, sizeof(buf) - size, &loc);
        RETURN_IF_ERROR(tap, size2 >= 2);

        res = location_unpack(buf, size + size2, 0, &loc);
        RETURN_IF_ERROR(tap, res == RES_OK);
        RETURN_IF_ERROR(tap, loc.loc_startline == 0);
        RETURN_IF_ERROR(tap, loc.loc_instruction == 0);

        res = location_unpack(buf, size + size2, 1, &loc);
        RETURN_IF_ERROR(tap, res == RES_OK);
        RETURN_IF_ERROR(tap, loc.loc_startline == 1);
        RETURN_IF_ERROR(tap, loc.loc_instruction == 1);
}

static void
test_unpack(struct tap_t *tap)
{
        unsigned char buf[20];
        long result;
        unsigned char *endptr;
        ssize_t pack_result, i;

        memset(buf, -1, sizeof(buf));
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == -1L);

        memset(buf, 0, sizeof(buf));
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == 0);
        RETURN_IF_ERROR(tap, endptr == &buf[1]);

        buf[0] = 'a';
        buf[1] = 'b';
        endptr = NULL;
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == 'a');
        RETURN_IF_ERROR(tap, endptr > buf && *endptr == 'b');

        memset(buf, 0, sizeof(buf));
        pack_result = pack_value(buf, sizeof(buf), INT_MAX);
        RETURN_IF_ERROR(tap, pack_result > 0);
        endptr = NULL;
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == INT_MAX);
        RETURN_IF_ERROR(tap, endptr == &buf[pack_result] && *endptr == 0);

        memset(buf, 0, sizeof(buf));
        pack_result = pack_value(buf, sizeof(buf), 0);
        RETURN_IF_ERROR(tap, pack_result == 1);

        memset(buf, 0, sizeof(buf));
        pack_result = pack_value(buf, sizeof(buf), -10L);
        RETURN_IF_ERROR(tap, pack_result > 0);
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == -1L);

        memset(buf, 0, sizeof(buf));
        pack_result = pack_value(buf, sizeof(buf), LONG_MAX);
        RETURN_IF_ERROR(tap, pack_result > 0);
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == LONG_MAX);
        RETURN_IF_ERROR(tap, endptr == &buf[pack_result]);

        memset(buf, 0, sizeof(buf));
        /* IE highest-magnitude negative number */
        pack_result = pack_value(buf, sizeof(buf),
                                 (unsigned long)LONG_MAX + 1);
        RETURN_IF_ERROR(tap, pack_result > 0);
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == -1);

        for (i = 0; i < 9; i++)
                buf[i] = 0x80;
        buf[9] = 0x2;
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == -1);
        buf[9] = 0;
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == -1);

        buf[10] = 0x80;
        buf[11] = 0;
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == -1);
        buf[11] = 4;
        result = unpack_value(buf, sizeof(buf), &endptr);
        RETURN_IF_ERROR(tap, result == -1);
}

int
main(int argc, char **argv)
{
        struct tap_t tap;

        /*
         * TODO: Move these to a different test program, maybe
         * to rtfuzzer.c, to run before the actual fuzz tests.
         * We cannot control how a child process prints to stderr
         * from here, short of overkill redirection, so we can't
         * guarantee proper TAP output for the whole process.
         */
        if (test_runtime_error(true) == RES_ERROR)
                return EXIT_FAILURE;
        if (test_runtime_noerror(true) == RES_ERROR)
                return EXIT_FAILURE;
        if (test_syntax_error(true) == RES_ERROR)
                return EXIT_FAILURE;

        tap_init(&tap, stderr, -1);

        initialize_program();
        test_path(&tap);
        test_locations(&tap);
        test_unpack(&tap);
        end_program();

        tap_end_tests(&tap);

        return tap_nr_error(&tap) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

