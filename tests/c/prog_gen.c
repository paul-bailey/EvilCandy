#include <tests/prog_gen.h>
#include <tests/fuzzer_symtab.h>
#include <tests/strbuf.h>
#include <lib/helpers.h>

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

struct prog_t {
        struct strbuf_t sb;
        int depth;
        int nr_faults;
};

enum {
        MAX_DEPTH = 10,
        NR_ELEM = 5,
        NR_ARG = 5,
};

static bool
should_inject_fault(struct prog_t *prog)
{
        /* any more than this and maybe it's too faulty */
        enum { LOTSA_FAULTS = 3 };

        if (prog->nr_faults < LOTSA_FAULTS)
                return !!(rand() & 1);
        return false;
}

static int
print_indent(struct prog_t *prog)
{
        int depth = prog->depth;

        while (depth-- > 0) {
                if (sb_append(&prog->sb, "    ") < 0)
                        return -1;
        }
        return 0;
}

static int
insert_existing_name(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        const char *name = fuzzer_symtab_existing_name(sym);
        if (!name || should_inject_fault(prog)) {
                prog->nr_faults++;
                name = fuzzer_symtab_bad_name(sym);
        }
        return sb_append(&prog->sb, name);
}

static int
insert_new_name(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        const char *name = fuzzer_symtab_new_name(sym);
        if (!name)
                return -1;
        return sb_append(&prog->sb, name);
}

static int gen_key_expression(struct prog_t *prog,
                              struct fuzzer_symtab_t *sym);
static int gen_expr(struct prog_t *prog, struct fuzzer_symtab_t *sym);
static int gen_stmt(struct prog_t *prog, struct fuzzer_symtab_t *sym);

static int
gen_fooexpr(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        /*
         * The parentheses are needed because the expr could be an int,
         * and something like "1.foo" will generate a parsing error
         * instead of a runtime error.
         */
        if (sb_append(&prog->sb, "(") < 0)
                return -1;
        if (gen_expr(prog, sym) < 0)
                return -1;
        /* the .foo *is* the fault */
        prog->nr_faults++;
        return sb_append(&prog->sb, ").foo");
}

static int
gen_function_def_expression(struct prog_t *prog,
                            struct fuzzer_symtab_t *sym)
{
        int i, nr_args, nr_statements;
        struct fuzzer_symtab_t *newsym;

        if (prog->depth >= MAX_DEPTH)
                return -1;
        prog->depth++;
        newsym = fuzzer_symtab_new();

        if (sb_append(&prog->sb, "function(") < 0)
                return -1;

        nr_args = rand() % 3;
        for (i = 0; i < nr_args; i++) {
                if (i > 0) {
                        if (sb_append(&prog->sb, ", ") < 0)
                                return -1;
                }
                if (insert_new_name(prog, newsym) < 0)
                        return -1;
        }
        if (sb_append(&prog->sb, ") {\n") < 0)
                return -1;

        nr_statements = rand() % 3;
        for (i = 0; i < nr_statements; i++) {
                if (gen_stmt(prog, newsym) < 0)
                        return -1;
        }

        fuzzer_symtab_free(newsym);
        prog->depth--;
        assert(prog->depth >= 0);
        if (print_indent(prog) < 0)
                return -1;
        return sb_append(&prog->sb, "}");
}

static int
gen_function_call_expression(struct prog_t *prog,
                             struct fuzzer_symtab_t *sym)
{
        int i, nr_args;

        /*
         * FIXME: This is more of an issue with src/assembler.c than
         * with this program.  The statement:
         *
         *      x() == y;
         *
         * will cause a syntax error instead of a name-doesn't-exist
         * runtime error, because src/assembler.c rejects any tokens
         * after the closing parenthesis that isn't a de-reference
         * (one of ".[(") or end-of-statement (";").  The statement:
         *
         *      (x()) == y;
         *
         * will NOT cause a syntax error, because it goes through a
         * different parsing path; in the former case, assembler.c
         * assumes a subroutine, while in the latter case, assembler.c
         * assumes an expression.
         *
         * Hence our double-parentheses here.
         */
        if (sb_append(&prog->sb, "(") < 0)
                return -1;

        if (insert_existing_name(prog, sym) < 0)
                return -1;

        if (sb_append(&prog->sb, "(") < 0)
                return -1;
        nr_args = rand() % NR_ARG;
        for (i = 0; i < nr_args; i++) {
                if (i > 0) {
                        if (sb_append(&prog->sb, ", ") < 0)
                                return -1;
                }
                if (gen_expr(prog, sym) < 0)
                        return -1;
        }
        return sb_append(&prog->sb, "))");
}

static int
gen_binop_expression(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        static const char *OPS[] = {
                " in ", " instanceof ",
                " and ", " or ",
                " + ", " - ", " * ", " ** ", " / ",
                " == ", " === ", " !== ", " != ",
                " & ", " | ", " % ", " ^ ",
        };

        const char *op = OPS[rand() % ARRAY_SIZE(OPS)];
        if (gen_expr(prog, sym) < 0)
                return -1;
        if (sb_append(&prog->sb, op) < 0)
                return -1;
        return gen_expr(prog, sym);
}

static int
gen_int_expression(struct prog_t *prog)
{
        char buf[128];
        /*
         * Keep this number small. We could end up with something
         * like "112312333 * [1, 'a', 5.0]", which is fine for stress
         * tests (it's a valid expression), but it's likely to cause timeouts
         * during fuzzer tests.
         */
        int val = rand() % 50;
        evc_sprintf(buf, sizeof(buf), "%i", val);
        return sb_append(&prog->sb, buf);
}

static int
gen_array_expression(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        size_t i, nr_elem;
        nr_elem = rand() % NR_ELEM;
        if (sb_append(&prog->sb, "[") < 0)
                return -1;
        for (i = 0; i < nr_elem; i++) {
                if (i > 0) {
                        if (sb_append(&prog->sb, ", ") < 0)
                                return -1;
                }
                if (gen_expr(prog, sym) < 0)
                        return -1;
        }
        return sb_append(&prog->sb, "]");
}

static int
gen_subscript_expression(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        /*
         * see fixme in gen_function_call_expression.  The parentheses
         * here are for roughly the same reason.
         */
        if (sb_append(&prog->sb, "(") < 0)
                return -1;
        if (insert_existing_name(prog, sym) < 0)
                return -1;
        if (sb_append(&prog->sb, "[") < 0)
                return -1;
        if (gen_key_expression(prog, sym) < 0)
                return -1;
        return sb_append(&prog->sb, "])");
}

static int
gen_keyed_tuple_expression(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        size_t i, nr_elem;

        nr_elem = rand() % NR_ELEM;
        if (sb_append(&prog->sb, "(") < 0)
                return -1;
        for (i = 0; i < nr_elem; i++) {
                if (i > 0) {
                        if (sb_append(&prog->sb, ", ") < 0)
                                return -1;
                }
                if (gen_key_expression(prog, sym) < 0)
                        return -1;
        }

        if (i == 1) {
                if (sb_append(&prog->sb, ",") < 0)
                        return -1;
        }
        return sb_append(&prog->sb, ")");
}

static int
gen_key_expression(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        enum {
                KEY_TYPE_INT = 0,
                KEY_TYPE_BYTES,
                KEY_TYPE_STRING,
                KEY_TYPE_TUPLE,
                NR_KEY_TYPES,
        };

        if (should_inject_fault(prog))
                return gen_expr(prog, sym);

        switch (rand() % NR_KEY_TYPES) {
        case KEY_TYPE_INT:
                return gen_int_expression(prog);
        case KEY_TYPE_BYTES:
                return sb_append(&prog->sb, "b'abc'");
        case KEY_TYPE_STRING:
                return sb_append(&prog->sb, "'abc'");
        case KEY_TYPE_TUPLE:
                return gen_keyed_tuple_expression(prog, sym);
        default:
                assert(false);
                return -1;
        }
}

static int
gen_dict_expression(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        size_t i, nr_elem;

        nr_elem = rand() % NR_ELEM;
        /*
         * The parentheses are because dictionary expressions may
         * not begin a full statement, due to syntactic over-
         * loading of the OC_LBRACE token. and we don't know here
         * if we're at the start of a statement or not.
         */
        if (sb_append(&prog->sb, "({") < 0)
                return -1;

        for (i = 0; i < nr_elem; i++) {
                if (i > 0) {
                        if (sb_append(&prog->sb, ", ") < 0)
                                return -1;
                }
                if (gen_key_expression(prog, sym) < 0)
                        return -1;
                if (sb_append(&prog->sb, ": ") < 0)
                        return -1;
                if (gen_expr(prog, sym) < 0)
                        return -1;
        }

        return sb_append(&prog->sb, "})");
}

static int
gen_expr(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        enum {
                EXPR_TYPE_INT = 0,
                EXPR_TYPE_STRING,
                EXPR_TYPE_NIL,
                EXPR_TYPE_LIST,
                EXPR_TYPE_DICT,
                EXPR_TYPE_FUNCTION_CALL,
                EXPR_TYPE_FUNCTION_DEF,
                EXPR_TYPE_NAME,
                EXPR_TYPE_BINARY_OP,
                EXPR_TYPE_SUBSCRIPT,
                NR_EXPR_TYPES,
        };

        switch (rand() % NR_EXPR_TYPES) {
        case EXPR_TYPE_INT:
                return gen_int_expression(prog);
        case EXPR_TYPE_STRING:
                return sb_append(&prog->sb, "'abc'");
        case EXPR_TYPE_NIL:
                return sb_append(&prog->sb, "null");
        case EXPR_TYPE_LIST:
                return gen_array_expression(prog, sym);
        case EXPR_TYPE_DICT:
                return gen_dict_expression(prog, sym);
        case EXPR_TYPE_FUNCTION_CALL:
                return gen_function_call_expression(prog, sym);
        case EXPR_TYPE_FUNCTION_DEF:
                return gen_function_def_expression(prog, sym);
        case EXPR_TYPE_NAME:
                return insert_existing_name(prog, sym);
        case EXPR_TYPE_BINARY_OP:
                return gen_binop_expression(prog, sym);
        case EXPR_TYPE_SUBSCRIPT:
                return gen_subscript_expression(prog, sym);
        default:
                assert(0);
                return -1;
        }

}

static int
end_statement(struct prog_t *prog)
{
        return sb_append(&prog->sb, ";\n");
}

static int
gen_function_call(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        unsigned int i, nargs = rand() % NR_ARG;

        if (insert_existing_name(prog, sym) < 0)
                return -1;
        if (sb_append(&prog->sb, "(") < 0)
                return -1;
        /*
         * FIXME: this most likely injects a fault, since I don't
         * know that existing function has this many args.
         */
        for (i = 0; i < nargs; i++) {
                if (i > 0) {
                        if (sb_append(&prog->sb, ", ") < 0)
                                return -1;
                }
                if (gen_expr(prog, sym) < 0)
                        return -1;
        }
        if (sb_append(&prog->sb, ")") < 0)
                return -1;
        return end_statement(prog);
}

static int
gen_assignment_statement(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        if (insert_existing_name(prog, sym) < 0)
                return -1;
        if (sb_append(&prog->sb, " = ") < 0)
                return -1;
        if (gen_expr(prog, sym) < 0)
                return -1;
        return end_statement(prog);
}

/* actually, declarator + initializer */
static int
gen_declarator_statement(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        if (sb_append(&prog->sb, "let ") < 0)
                return -1;
        if (insert_new_name(prog, sym) < 0)
                return -1;
        if (sb_append(&prog->sb, " = ") < 0)
                return -1;
        if (gen_expr(prog, sym) < 0)
                return -1;
        return end_statement(prog);
}

static int
gen_return_statement(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        if (sb_append(&prog->sb, "return ") < 0)
                return -1;
        if (prog->depth == 0) {
                /*
                 * We must guarantee a bad expression here, or else
                 * the program flow might not actually hit the bad
                 * expression.
                 */
                if (gen_fooexpr(prog, sym) < 0)
                        return -1;
        } else {
                if (gen_expr(prog, sym) < 0)
                        return -1;
        }
        return end_statement(prog);
}

static int
gen_stmt(struct prog_t *prog, struct fuzzer_symtab_t *sym)
{
        enum {
                STMT_TYPE_CALL = 0,
                STMT_TYPE_ASSIGNMENT,
                STMT_TYPE_DECLARE,
                STMT_TYPE_RETURN,
                NR_STMT_TYPES,
        };

        if (print_indent(prog) < 0)
                return -1;

        switch (rand() % NR_STMT_TYPES) {
        case STMT_TYPE_CALL:
                return gen_function_call(prog, sym);
        case STMT_TYPE_ASSIGNMENT:
                return gen_assignment_statement(prog, sym);
        case STMT_TYPE_DECLARE:
                return gen_declarator_statement(prog, sym);
        case STMT_TYPE_RETURN:
                return gen_return_statement(prog, sym);
        default:
                assert(false);
                return -1;
        }
}

static int
prog_gen_top(struct prog_t *prog)
{
        enum {
                PROG_TYPE_EXPR_FOO = 0,
                PROG_TYPE_EXPR,
                PROG_TYPE_STMT,
                PROG_TYPE_STMTS,
                NR_PROG_TYPES,
        };

        struct fuzzer_symtab_t *sym = fuzzer_symtab_new();
        if (!sym)
                return -1;

        switch (rand() % NR_PROG_TYPES) {
        case PROG_TYPE_EXPR_FOO:
                if (gen_fooexpr(prog, sym) < 0)
                        return -1;
                return end_statement(prog);
        case PROG_TYPE_EXPR:
                if (gen_expr(prog, sym) < 0)
                        return -1;
                if (end_statement(prog) < 0)
                        return -1;
                break;
        case PROG_TYPE_STMT:
                if (gen_stmt(prog, sym) < 0)
                        return -1;
                break;
        case PROG_TYPE_STMTS:
            {
                int i;
                for (i = 0; i < 3; i++) {
                        if (gen_stmt(prog, sym) < 0)
                                return -1;
                }
                break;
            }
        default:
                assert(0);
                return -1;
        }

        /*
         * nr_fault is not to be taken as gospel, since the fault
         * could be injected in a function we'll never call.  So add an
         * always-failing (EXPR).foo at the end of the statement just
         * to be extra sure.
         */
        if (gen_fooexpr(prog, sym) < 0)
                return -1;
        return end_statement(prog);
}

/**
 * prog_gen - Generate a syntactically valid but semantically wrong program
 * @buf:        Buffer to put the program in
 * @bufsize:    Size of @buf
 * @spinlock_tolerance: Threshold number of failed attempts before bailing.
 *              Keep it small, single-digit.  If we fail, it means @buf is
 *              getting set too small.
 *
 * Return: 0 if success, -1 if @spinlock_tolerance attempts at generating
 *         a program were made without success.  If @buf is big enough, this
 *         should rarely ever happen.
 */
int
prog_gen(char *buf, size_t bufsize, unsigned int spinlock_tolerance)
{
        struct prog_t prog;
        int i;

        for (i = 0; i < spinlock_tolerance; i++) {
                int result;

                fuzzer_symtab_reset_state();
                sb_init(&prog.sb, buf, bufsize);
                prog.depth = 0;
                prog.nr_faults = 0;
                result = prog_gen_top(&prog);
                if (prog.nr_faults != 0 && result == 0)
                        break;
        }
        return i == spinlock_tolerance ? -1 : 0;
}

