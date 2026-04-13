#include <evilcandy.h>
#include <assert.h>
#include <internal/init.h>

extern void reduce_pathname_in_place(char *path);

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
        initialize_program();
        test_path();
        end_program();
        return 0;
}

