#include <evilcandy.h>
#include <internal/init.h>
#include <internal/path.h>

#include <assert.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

/* in child process */
static void
runtime_test_one_snippet_in_child(const char *snippet, bool should_pass)
{
        Object *xptr, *result;

        xptr = assemble_string(snippet, false);
        assert(xptr != NULL);
        assert(xptr != ErrorVar);

        result = vm_exec_script(xptr, NULL);
        /* Pass or fail, result should never be NULL */
        assert(result != NULL);
        if (should_pass) {
                assert(!err_occurred());
                assert(result != ErrorVar);

                VAR_DECR_REF(result);
        } else {
                assert(err_occurred());
                assert(result == ErrorVar);

                err_clear();
        }
}

static enum result_t
fork_and_test_snippets(const char **snippet, bool should_pass, bool all)
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
                                runtime_test_one_snippet_in_child(
                                                *snippet, should_pass);
                                snippet++;
                        }
                } else {
                        runtime_test_one_snippet_in_child(*snippet,
                                                          should_pass);
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

        NULL

        /*
         * TODO: Below would result in compile-time errors, not runtime
         * errors.  But is our syntax fuzzer good enough, or should I
         * make a test of these as well?
         */
#if 0
        "{'a': 1}['b'];",
        "try { missing_name; } finally { print('cleanup'); }",
#endif
};

/*
 * XXX: This isn't a unit test per-se, maybe it should go in its own
 * program.
 */
static enum result_t
test_runtime_error(bool stop_on_failure)
{
        enum result_t ret = RES_OK;
        const char **snippets = RUNTIME_ERROR_SNIPPETS;
        while (*snippets != NULL) {
                enum result_t tres;
                tres = fork_and_test_snippets(snippets, false, false);
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
                ret = fork_and_test_snippets(RUNTIME_ERROR_SNIPPETS,
                                             false, true);
        }
        return ret;
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
test_path(void)
{
        /* TODO: a bunch more */
        assert(test_rpip("/../a", "/a"));
        assert(test_rpip("////a/././b", "/a/b"));
        assert(test_rpip("/a/b/c", "/a/b/c"));
        assert(test_rpip("/a/../../../b/./.c/..c", "/b/.c/..c"));
}

int
main(int argc, char **argv)
{
        if (test_runtime_error(true) == RES_ERROR)
                return EXIT_FAILURE;

        initialize_program();
        test_path();
        end_program();

        return EXIT_SUCCESS;
}

