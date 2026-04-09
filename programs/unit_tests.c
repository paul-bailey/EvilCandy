#include <evilcandy.h>
#include <assert.h>

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

void
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
        test_path();
        return 0;
}

