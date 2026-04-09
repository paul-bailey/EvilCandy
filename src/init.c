#include <evilcandy.h>
#include <evilcandy/init.h>

void
initialize_program(void)
{
        /* Note: the order matters */
        cfile_init_ewrappers();
        cfile_init_var();
        cfile_init_vm();
        cfile_init_global();
}

void
end_program(void)
{
        debug_clear_locations();
        cfile_deinit_global();
        cfile_deinit_vm();
        /* must be last */
        cfile_deinit_var();
        /* no deinit for ewrappers.c */
}

