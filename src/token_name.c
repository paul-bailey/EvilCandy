#include <evilcandy.h>
#include <token.h>

#define TOKNAME(X)  [OC_##X] = #X

static const char *TOKEN_NAMES[OC_NTOK] = {
        "Error, not a token",
        TOKNAME(PLUS),
        TOKNAME(MINUS),
        TOKNAME(GT),
        TOKNAME(LT),
        TOKNAME(EQ),
        TOKNAME(AND),
        TOKNAME(OR),
        TOKNAME(PER),
        TOKNAME(EXCLAIM),
        TOKNAME(SEMI),
        TOKNAME(COMMA),
        TOKNAME(DIV),
        TOKNAME(MUL),
        TOKNAME(POW),
        TOKNAME(MOD),
        TOKNAME(XOR),
        TOKNAME(LPAR),
        TOKNAME(RPAR),
        TOKNAME(LBRACK),
        TOKNAME(RBRACK),
        TOKNAME(LBRACE),
        TOKNAME(RBRACE),
        TOKNAME(COLON),
        TOKNAME(TILDE),
        TOKNAME(PLUSPLUS),
        TOKNAME(MINUSMINUS),
        TOKNAME(LSHIFT),
        TOKNAME(RSHIFT),
        TOKNAME(EQEQ),
        TOKNAME(ANDAND),
        TOKNAME(OROR),
        TOKNAME(LEQ),
        TOKNAME(GEQ),
        TOKNAME(NEQ),
        TOKNAME(LAMBDA),
        TOKNAME(PLUSEQ),
        TOKNAME(MINUSEQ),
        TOKNAME(MULEQ),
        TOKNAME(DIVEQ),
        TOKNAME(MODEQ),
        TOKNAME(XOREQ),
        TOKNAME(LSEQ),
        TOKNAME(RSEQ),
        TOKNAME(OREQ),
        TOKNAME(ANDEQ),
        TOKNAME(FUNC),
        TOKNAME(LET),
        TOKNAME(THIS),
        TOKNAME(RETURN),
        TOKNAME(BREAK),
        TOKNAME(CONTINUE),
        TOKNAME(IF),
        TOKNAME(WHILE),
        TOKNAME(ELSE),
        TOKNAME(DO),
        TOKNAME(FOR),
        TOKNAME(GBL),
        TOKNAME(CONST),
        TOKNAME(PRIV),
        TOKNAME(TRUE),
        TOKNAME(FALSE),
        TOKNAME(TRY),
        TOKNAME(CATCH),
        TOKNAME(FINALLY),
        TOKNAME(NULL), /* <- not what it looks like */
        TOKNAME(EOF),  /* <-  ^^^ditto */
        TOKNAME(STRING),
        TOKNAME(BYTES),
        TOKNAME(IDENTIFIER),
        TOKNAME(INTEGER),
        TOKNAME(FLOAT)
};

const char *
token_name(int t)
{
        if (t <= 0 || t >= OC_NTOK)
                return NULL;
        bug_on(TOKEN_NAMES[t] == NULL);
        return TOKEN_NAMES[t];
}

