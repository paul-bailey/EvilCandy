#include "egq.h"
#include <string.h>

enum {
        FRAME_STACK_MAX = 128,
        FRAME_NEST_MAX  = 32,
        FRAME_CLOSURE_MAX = 24,
};

struct frame_t {
        unsigned short fp, sp, ap;
        unsigned char cp, nest;
        struct frame_t *prev_scope;
        struct var_t *owner;
        struct var_t *func;
        struct list_t others;
        char *symtab[FRAME_STACK_MAX];
        struct var_t *stack[FRAME_STACK_MAX];
        int fps[FRAME_NEST_MAX];
        char *clotab[FRAME_CLOSURE_MAX];
        struct var_t *closures[FRAME_CLOSURE_MAX];
};
#define FRAME_ZSIZE (offsetof(struct frame_t, others))


static struct frame_t *frames;
static struct list_t free_frames;

#define list2frame(li) container_of(li, struct frame_t, others)

static void
frame_clear(struct frame_t *fr)
{
        memset(fr, 0, FRAME_ZSIZE);
        list_init(&fr->others);
        list_add_tail(&fr->others, &free_frames);
}

struct frame_t *
frame_alloc(void)
{
        struct frame_t *fr;

        if (list_is_empty(&free_frames))
                syntax("Frames nested too deep");

        fr = list2frame(free_frames.next);
        list_remove(&fr->others);
        return fr;
}

void
frame_push_weak(void)
{
        struct frame_t *fr = q_.frame;
        bug_on(!fr);
        if (fr->nest >= FRAME_NEST_MAX)
                syntax("Program nested too deep");
        fr->fps[fr->nest] = fr->fp;
        fr->nest++;
        fr->fp = fr->sp;
}

void
frame_push(struct frame_t *fr)
{
        bug_on(!fr);
        fr->ap = fr->sp;
        fr->prev_scope = q_.frame;
        q_.frame = fr;
}

static void
frame_pop_to(struct frame_t *fr, int fp)
{
        while (fr->sp > fp) {
                --fr->sp;
                var_delete(fr->stack[fr->sp]);
        }
}

void
frame_pop_weak(void)
{
        struct frame_t *fr = q_.frame;
        bug_on(!fr);
        bug_on(fr->nest <= 0);
        frame_pop_to(fr, fr->fp);
        --fr->nest;
        fr->fp = fr->fps[fr->nest];
}

void
frame_pop(void)
{
        struct frame_t *fr = q_.frame;
        bug_on(!fr);
        frame_pop_to(fr, 0);
        /*
         * Don't delete closures, these are not copies
         * like the stack vars.
         */
        q_.frame = fr->prev_scope;
        frame_clear(fr);
}

static void
frame_add_var_(struct var_t *var, char *name, struct frame_t *fr)
{
        if (fr->sp >= FRAME_STACK_MAX)
                syntax("Local-variable stack overflow");
        fr->symtab[fr->sp] = name;
        fr->stack[fr->sp] = var;
        fr->sp++;
}

void
frame_add_var(struct var_t *var, char *name)
{
        struct frame_t *fr = q_.frame;
        bug_on(!fr);
        frame_add_var_(var, name, fr);
}

void
frame_add_arg(struct frame_t *fr, struct var_t *var, char *name)
{
        frame_add_var_(var, name, fr);
        /* ap will be set at push_frame time */
}

void
frame_add_closure(struct frame_t *fr, struct var_t *clo, char *name)
{
        if (fr->cp >= FRAME_CLOSURE_MAX)
                syntax("Closure-variable stack overflow");
        fr->clotab[fr->cp] = name;
        fr->closures[fr->cp] = clo;
        fr->cp++;
}

struct var_t *
frame_get_arg(unsigned int idx)
{
        struct frame_t *fr = q_.frame;
        bug_on(!fr);
        if (idx >= fr->ap)
                return NULL;
        return fr->stack[idx];
}

struct var_t *
frame_get_var(const char *name, bool gbl)
{
        bug_on(!q_.frame);
        int i;
        struct frame_t *fr = q_.frame;
        for (i = 0; i < fr->sp; i++) {
                if (fr->symtab[i] == name)
                        return fr->stack[i];
        }
        for (i = 0; i < fr->cp; i++) {
                if (fr->clotab[i] == name)
                        return fr->closures[i];
        }
        return NULL;
}

int
frame_nargs(void)
{
        bug_on(!q_.frame);
        return q_.frame->ap;
}

void
frame_add_owners(struct frame_t *fr, struct var_t *obj, struct var_t *func)
{
        bug_on(!obj);
        bug_on(!func);

        fr->owner = obj;
        fr->func = func;
}

void
moduleinit_frame(void)
{
        enum {
                POOL_ALLOCSIZE = sizeof(struct frame_t) * FRAME_DEPTH_MAX
        };
        int i;
        list_init(&free_frames);
        frames = emalloc(POOL_ALLOCSIZE);
        for (i = 0; i < FRAME_DEPTH_MAX; i++)
                frame_clear(&frames[i]);
}

struct var_t *
frame_get_this(void)
{
        return q_.frame ? q_.frame->owner : q_.gbl;
}

struct var_t *
frame_get_this_func(void)
{
        return q_.frame ? q_.frame->func : NULL;
}
