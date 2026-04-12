#include <sys/stat.h>
#include <evilcandy.h>

struct import_key_t {
        dev_t dev;
        ino_t ino;
};

enum {
        MODULE_ERROR = 0,
        MODULE_LOADING,
        MODULE_LOADED,
};

static Object *
module_makekey(FILE *fp, const char *file_name)
{
        struct import_key_t key;
        struct stat statbuf;

        /*
         * Key imports by file identity rather than normalized path. The
         * path cleanup in push_path() is lexical, so symlinks and hard
         * links can still name the same file different ways. fstat()
         * also lets us reject unsupported non-regular files after
         * opening.
         */
        if (fstat(fileno(fp), &statbuf) < 0) {
                err_errno("Cannot access '%s' properly", file_name);
                return ErrorVar;
        }
        /*
         * XXX REVISIT: Here I am limiting imports to just regular files,
         * but could it ever make sense to "import" a socket or pipe?
         */
        if ((statbuf.st_mode & S_IFMT) != S_IFREG) {
                err_setstr(NotImplementedError,
                           "cannot import '%s': currently only regular files are supported",
                           file_name);
                return ErrorVar;
        }
        memset(&key, 0, sizeof(key));
        key.dev = statbuf.st_dev;
        key.ino = statbuf.st_ino;
        return bytesvar_new((unsigned char *)&key, sizeof(key));
}

static Object *
compile_and_execute(Frame *fr, FILE *fp, const char *file_name)
{
        Object *xptr, *func, *ret;

        xptr = assemble(file_name, fp, NULL);
        if (!xptr || xptr == ErrorVar) {
                if (!err_occurred()) {
                        err_setstr(RuntimeError,
                                   "Failed to import module '%s'", file_name);
                }
                return ErrorVar;
        }
        func = funcvar_new_user(xptr, NULL);
        VAR_DECR_REF(xptr);

        ret = vm_exec_func(fr, func, NULL, NULL);
        VAR_DECR_REF(func);
        return ret;
}

static void
module_cache_insert(Object *module, Object *key)
{
        enum result_t res;
        if (!gbl.import_dict)
                gbl.import_dict = dictvar_new();
        res = dict_setitem(gbl.import_dict, key, module);
        bug_on(res == RES_ERROR);
        (void)res;
}

static Object *
module_cache_lookup(Object *key)
{
        if (!gbl.import_dict)
                return NULL;
        return dict_getitem(gbl.import_dict, key);
}

/* Done early to catch cyclic loads */
static Object *
module_new(Object *key)
{
        Object *mod, *stack[2];
        stack[0] = VAR_NEW_REF(NullVar);
        stack[1] = intvar_new(MODULE_LOADING);
        mod = arrayvar_from_stack(stack, 2, true);

        module_cache_insert(mod, key);
        return mod;
}

static void
module_set(Object *module, Object *value, int state)
{
        Object **data;
        bug_on(!isvar_array(module) || seqvar_size(module) != 2);
        data = array_get_data(module);
        VAR_DECR_REF(data[0]);
        VAR_DECR_REF(data[1]);
        data[0] = VAR_NEW_REF(value);
        data[1] = intvar_new(state);
}

static Object *
reimport(Object *module)
{
        Object *ret;
        int state = MODULE_ERROR;
        enum result_t res;

        res = vm_getargs_sv(module, "[<*>i!]", &ret, &state);
        bug_on(res == RES_ERROR);
        (void)res;

        if (state == MODULE_LOADED)
                return VAR_NEW_REF(ret);

        switch (state) {
        case MODULE_LOADING:
                err_setstr(RuntimeError, "cyclic load error");
                break;
        case MODULE_ERROR:
                err_setstr(RuntimeError, "bad module state");
                break;
        default:
                DBUG("malformed module state %d", state);
                bug();
        }
        return ErrorVar;
}

/**
 * evc_import - Import a module
 * @fr: Frame to pass to interpreter while executing module code
 * @file_name: File name of the module.
 *
 * Return: Executed return value of the module at @file_name.  This may
 * be cached already, in which case the cached value will be returned.
 */
Object *
evc_import(Frame *fr, const char *file_name)
{
        FILE *fp;
        Object *key, *ret, *mod;

        fp = push_path(file_name);
        if (!fp) {
                err_errno("Cannot access '%s' properly", file_name);
                return ErrorVar;
        }

        ret = ErrorVar;

        key = module_makekey(fp, file_name);
        if (key == ErrorVar)
                goto out_pop_path;

        mod = module_cache_lookup(key);
        if (mod) {
                ret = reimport(mod);
        } else {
                mod = module_new(key);
                ret = compile_and_execute(fr, fp, file_name);
                if (ret == ErrorVar) {
                        /* Don't save ErrorVar in mod. state will tell */
                        module_set(mod, NullVar, MODULE_ERROR);
                } else {
                        module_set(mod, ret, MODULE_LOADED);
                }
        }

        VAR_DECR_REF(mod);
        VAR_DECR_REF(key);
out_pop_path:
        pop_path(fp);
        return ret;
}

