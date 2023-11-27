#include "egq.h"
#include <string.h>

/*
 * Build a QFUNCTION_MAGIC struct var_t from code
 *
 * got something like "v = function (a, b, c) { ..."
 * PC is before first '(' of args.
 */
void
compile_function(struct var_t *v)
{
        int brace;
        struct marker_t mk;

        qlex();
        expect(OC_LPAR);

        function_init(v);
        /*
         * Set owner to "this", since we're declaring it.
         * Even if we're parsing an element of an object,
         * which could be a return value from a function,
         * we want our namespace to be in the current function
         * when returning to this.
         */
        do {
                char *name;
                struct var_t *deflt = NULL;
                qlex();
                if (cur_oc->t == OC_RPAR)
                        break; /* no args */
                expect('u');
                name = cur_oc->s;
                qlex();
                if (cur_oc->t == OC_EQ) {
                        deflt = var_new();
                        eval(deflt);
                        qlex();
                }
                function_add_arg(v, name, deflt);
        } while (cur_oc->t == OC_COMMA);
        expect(OC_RPAR);

        /*
         * PC is now at start of function call.
         * scan to end of function, first checking that
         * argument header is sane.
         */
        PC_SAVE(&mk);
        function_set_user(v, &mk);

        /* XXX: Require this? What about lambdas? */
        qlex();
        expect(OC_LBRACE);

        brace = 1;
        while (brace && cur_oc->t != EOF) {
                qlex();
                if (cur_oc->t == OC_LBRACE)
                        brace++;
                else if (cur_oc->t == OC_RBRACE)
                        brace--;
        }
        if (cur_oc->t == EOF)
                syntax("Unbalanced brace");
}

/*
 * Build a QOBJECT_MAGIC struct var_t from code.
 *
 * Parse something like...
 * {
 *      a1: b1,
 *      a2: const b2,
 *      a3: private b3,
 *      a4: private const b4    [<- no comma on last elem]
 * }
 * We're starting just after the left brace.
 */
void
compile_object(struct var_t *v)
{
        if (v->magic != QEMPTY_MAGIC)
                syntax("Cannot assign object to existing variable");
        object_init(v);
        do {
                unsigned flags = 0;
                struct var_t *child;
                char *name;
                qlex();
                expect('u');
                name = cur_oc->s;
                qlex();
                expect(OC_COLON);
                qlex();
                if (cur_oc->t == OC_PRIV) {
                        flags |= VF_PRIV;
                        qlex();
                }
                if (cur_oc->t == OC_CONST) {
                        flags |= VF_CONST;
                        qlex();
                }
                q_unlex();
                child = var_new();
                child->name = name;
                eval(child);
                child->flags = flags;
                object_add_child(v, child);
                qlex();
        } while (cur_oc->t == OC_COMMA);
        expect(OC_RBRACE);
}

/*
 * Build a QARRAY_MAGIC struct var_t from code
 *
 * parse something like "[elem1, elem2, ... ]"
 * we're starting just after the first '['
 */
void
compile_array(struct var_t *v)
{
        bug_on(v->magic != QEMPTY_MAGIC);
        array_from_empty(v);
        qlex();
        if (cur_oc->t == OC_RBRACK) /* empty array */
                return;
        q_unlex();
        do {
                struct var_t *child = var_new();
                qlex();
                eval(child);
                qlex();
                array_add_child(v, child);
        } while(cur_oc->t == OC_COMMA);
        expect(OC_RBRACK);
}


