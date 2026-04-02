/*
 * Implementation of generators.
 * A generator is a function that uses the 'yield' statement.
 */
#include <evilcandy.h>
#include <vm.h>
#include <types/xptr.h>

struct generator_t {
        struct seqvar_t base;
        Frame *frame;
        enum {
                GENERATOR_STOPPED,
                GENERATOR_RUNNING,
                GENERATOR_DONE,
                GENERATOR_ERROR,
        } state;
};

static Object *
generator_iter_next(Object *generator)
{
        struct xptrvar_t *ex;
        struct generator_t *gv = (struct generator_t *)generator;
        bug_on(!isvar_generator(generator));

        if (gv->state != GENERATOR_STOPPED)
                return NULL;

        /* XXX: Do we know this is isn't an internal function? */
        ex = gv->frame->ex;
        if (gv->frame->ppii >= ex->instr + ex->n_instr) {
                gv->state = GENERATOR_DONE;
                return NULL;
        }
        return execute_loop(gv->frame);
}

static Object *
generator_iter(Object *target)
{
        bug_on(!isvar_generator(target));
        return VAR_NEW_REF(target);
}

/* This should only be called by vm.c, which has already set up @frame */
Object *
generatorvar_new(Frame *frame)
{
        Object *generator;
        struct generator_t *gv;

        generator = var_new(&GeneratorType);
        gv = (struct generator_t *)generator;
        gv->frame = frame;
        gv->state = GENERATOR_STOPPED;
        return generator;
}

struct type_t GeneratorType = {
        .flags          = 0,
        .name           = "generator",
        .opm            = NULL,
        .sqm            = NULL,
        .size           = sizeof(struct generator_t),
        .str            = NULL,
        .cmp            = NULL,
        .cmpz           = NULL,
        .reset          = NULL, /*< frame cleanup done by vm.c */
        .prop_getsets   = NULL,
        .create         = NULL,
        .hash           = NULL,
        .iter_next      = generator_iter_next,
        .get_iter       = generator_iter,
};
