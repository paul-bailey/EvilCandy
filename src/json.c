/* json.c - Create a TYPE_DICT var from a json file */
#include <evilcandy.h>
#include <token.h>
#include <setjmp.h>

struct json_state_t {
        struct token_state_t *tok_state;
        struct token_t *tok;
        jmp_buf env; /* because of all the recursive-li-ness... */
        int recursion; /* ...but not too much recursive-li-ness! */
        struct var_t *d;
};

enum {
        JE_PARSER = 1,
        JE_ADDATTR,
        JE_SYNTAX,
        JE_EXCESS,
        JE_RECURSION,
};

#define json_err(j_, e_) longjmp((j_)->env, e_)

static int
json_get_tok(struct json_state_t *j)
{
        int ret = get_tok(j->tok_state, &j->tok);
        if (ret == TOKEN_ERROR)
                json_err(j, JE_PARSER);
        return ret;
}

static void
json_unget_tok(struct json_state_t *j)
{
        unget_tok(j->tok_state, &j->tok);
}

static void parsedict(struct json_state_t *j, struct var_t *parent);
static void parsearray(struct json_state_t *j, struct var_t *parent);

static struct var_t *
parseatomic(struct json_state_t *j)
{
        struct var_t *child;

        if (j->recursion > 128)
                json_err(j, JE_RECURSION);
        j->recursion++;

        switch (j->tok->t) {
        case 'i':
        case 'f':
        case 'q':
        case OC_TRUE:
        case OC_FALSE:
                bug_on(j->tok->v == NULL);
                child = j->tok->v;
        case OC_NULL:
                VAR_INCR_REF(NullVar);
                child = NullVar;
                break;
        case OC_LBRACK:
                child = arrayvar_new(0);
                parsearray(j, child);
                break;
        case OC_LBRACE:
                child = objectvar_new();
                parsedict(j, child);
                break;
        default:
                /*
                 * XXX REVISIT: Wouldn't be a big lift to support some
                 * non-standard stuff here.  'u' and OC_THIS tokens of
                 * course cannot be supported because that would require
                 * us to evaluate it here.  But what about OC_FUNC and
                 * OC_LAMBDA?  We would simply need a hook in assembler.c,
                 * something that wraps its local assemble_function() call.
                 * These functions would have to be limited, as they
                 * cannot take arguments or refer to closures.
                 */
                child = ErrorVar;
                json_err(j, JE_SYNTAX);
        }
        bug_on(child == ErrorVar || child == NULL);


        bug_on(j->recursion < 1);
        j->recursion--;

        return child;
}

/* next token is first token after '[' */
static void
parsearray(struct json_state_t *j, struct var_t *parent)
{
        int t = json_get_tok(j);
        if (t == OC_RBRACK)
                return;

        json_unget_tok(j);
        do {
                struct var_t *child;
                json_get_tok(j);
                child = parseatomic(j);
                if (array_append(parent, child) != RES_OK)
                        json_err(j, JE_ADDATTR);
                json_get_tok(j);
        } while (j->tok->t == OC_COMMA);
        if (j->tok->t != OC_RBRACK)
                json_err(j, JE_SYNTAX);
}

/* next token is first token after '{' */
static void
parsedict(struct json_state_t *j, struct var_t *parent)
{
        int t;

        /* peek, see if we have an empty dict */
        t = json_get_tok(j);
        if (t == OC_RBRACE)
                return;

        json_unget_tok(j);
        do {
                char *name;
                struct var_t *child;
                json_get_tok(j);
                if (j->tok->t != 'q')
                        json_err(j, JE_SYNTAX);
                name = j->tok->s;
                json_get_tok(j);
                if (j->tok->t != OC_COLON)
                        json_err(j, JE_SYNTAX);

                json_get_tok(j);
                child = parseatomic(j);
                if (object_setattr(parent, name, child) != RES_OK)
                        json_err(j, JE_ADDATTR);

                json_get_tok(j);
        } while (j->tok->t == OC_COMMA);
        if (j->tok->t != OC_RBRACE)
                json_err(j, JE_SYNTAX);
}

/**
 * dict_from_json - Parse a JSON file and turn it into an EvilCandy
 *                  dictionary
 * @filename: Path to file, either absolute or from the current working
 *            directory.
 *
 * Return:
 *      - If error, ErrorVar.
 *
 *      - If success, a dictionary.  If the file was empty or its only
 *      - tokens were an opening and closing brace, then a dictionary
 *      - containing no entries will be returned.
 *
 * Errors:
 *      - File access error
 *      - Bad syntax
 *      - Excess tokens exist in the file after the balanced closing
 *        brace has been parsed.
 *
 * Syntax:
 *      The file must begin with an opening brace as its very first
 *      non-whitespace token and end with a balanced closing brace.
 *
 *      Keys must be expressed as string literals.
 *      Write this:
 *              'name': 'Paul',
 *      not this:
 *              name: 'Paul',
 *
 *      The last item in an array or dictionary must not have a
 *      superfluous comma, or else a syntax error will occur.
 *
 *      The following data types are permitted:
 *      - Integer
 *      - Float
 *      - true, written in all lower case with no quotes
 *      - false,        ^^ditto...
 *      - null,         ^^ditto...
 *      - string literal, with same rules as EvilCandy (same backslash
 *        escapes, rules regarding single and double quotes, etc.)
 *      - Numerical array with square brackets
 *      - A nested dictionary (associative array) with curly braces
 *
 *      Identifiers and function definitions are not yet supported.
 *      Limited function definitions might be supported in the future.
 *      Identifiers, on the other hand, will likely never be supported.
 *
 *      Arrays and dictionaries may be nested arbitrarily deep, but be
 *      aware you're a jerk if you abuse this.
 *
 *      Because dict_from_json() uses the same tokenizer as the assembler,
 *      it has some comformance quirks:
 *
 *      - EvilCandy comments are skipped over.  JSON technically does
 *        not permit this, but we do.
 *      - String literals may not use backslash-zero to escape a
 *        null-char.  EvilCandy (as of 4/2025) still internally uses a
 *        lot of C-string processing on TYPE_STRING variables.  Rather
 *        than let this break a ton of stuff, the parser simply
 *        throws away both the backslash and the zero.
 *      - Other backslash escapes are permitted in the same way as
 *        string literals in an EvilCandy script.  This may or may not
 *        conform to JSON standards, I can't be bothered to find out,
 *        because...
 *
 *      ...the _idea_ of JSON is a hundred times better than the as-is
 *      strict standards of json.
 */
struct var_t *
dict_from_json(const char *filename)
{
        struct json_state_t jstate;
        FILE *fp = fopen(filename, "r");
        int status;
        struct var_t *ret;
        if (!fp) {
                err_setstr(SystemError,
                           "Could not open JSON file '%s'\n", filename);
                return ErrorVar;
        }
        jstate.tok_state = token_state_new(fp, filename);
        if (!jstate.tok_state) {
                /* Empty file: treat as error or OK? */
                fclose(fp);
                return objectvar_new();
        }
        jstate.d = NULL;
        jstate.recursion = 0;

        if ((status = setjmp(jstate.env)) != 0) {
                switch (status) {
                case JE_PARSER:
                case JE_ADDATTR:
                        /* error string already set */
                        break;
                case JE_SYNTAX:
                        err_setstr(RuntimeError,
                                   "JSON file has improper syntax");
                        break;
                case JE_EXCESS:
                        err_setstr(RuntimeError,
                                   "Excess tokens in JSON file");
                        break;
                case JE_RECURSION:
                        err_setstr(RuntimeError,
                                   "JSON elements nested too deeply");
                        break;
                default:
                        bug();
                }
                if (jstate.d)
                        VAR_DECR_REF(jstate.d);
                ret = ErrorVar;
        } else {
                int t = json_get_tok(&jstate);
                if (t == EOF) {
                        /* empty file */
                        ret = objectvar_new();
                } else if (t == OC_LBRACE) {
                        jstate.d = ret = objectvar_new();

                        parsedict(&jstate, ret);

                        /* make sure this file only contained json */
                        t = json_get_tok(&jstate);
                        if (t != EOF)
                                json_err(&jstate, JE_EXCESS);
                } else {
                        /* first tok must be '{' */
                        json_err(&jstate, JE_SYNTAX);
                }
        }

        token_state_free(jstate.tok_state);
        return ret;
}
