#include <evilcandy/debug.h>
#include <internal/init.h>

void
initialize_program(void)
{
        /* Note: the order matters */
        cfile_init_ewrappers();
        cfile_init_type_registry();
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
        /* no deinit for var.c */
        /* must be last */
        cfile_deinit_type_registry();
        /* no deinit for ewrappers.c */
}

