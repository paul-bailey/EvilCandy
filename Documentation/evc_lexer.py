from pygments.lexer import RegexLexer, DelegatingLexer, bygroups, default
from pygments import token
from pygments.token import Number, String

class EvilCandyLexer(RegexLexer):
    name = 'evilcandy'
    aliases = ['evc']
    filenames = ['*.evc']

    tokens = {
        'root': [
            (r'\s+', token.Text),
            (r'/\*.*?\*/', token.Comment.Multiline),
            (r'#.*$', token.Comment.Single),
            (r'//.*$', token.Comment.Single),
            (r'(namespace|and|or|not|function|private|import|class|for|let|global|if|else|while|return|in|nobreak)\b',
             token.Keyword),
            (r'(as|by)\b', token.Keyword.Soft),
            (r'"[^"]*"', token.String),
            (r'[a-zA-Z_][a-zA-Z0-9_]*', token.Name),
            (r'[0-9]+', token.Number),
            (r'[{}()\[\],.;]', token.Punctuation),
            (r'[%!<>&\^|+-/*?:=]', token.Operator),
            (r'0[bB][01]+n?', Number.Bin),
            (r'0[oO]?[0-7]+n?', Number.Oct),
            (r'0[xX][0-9a-fA-F]+n?', Number.Hex),
            (r'[0-9]+n', Number.Integer),
            (r'(\.[0-9]+|[0-9]+\.[0-9]*|[0-9]+)([eE][-+]?[0-9]+)?', Number.Float),
            (r'"(\\\\|\\[^\\]|[^"\\])*"', String.Double),
            (r"'(\\\\|\\[^\\]|[^'\\])*'", String.Single),
        ],
    }

class EvilCandyPromptLexerBase(RegexLexer):
    name = 'evilcandy interactive session'
    aliases = ['evc-console']

    tokens = {
        'root': [
            (r'(evc> )(.*\n)', bygroups(token.Generic.Prompt, token.Other.Code), 'continuations'),
            (r'(evc>)(\n)', bygroups(token.Generic.Prompt, token.Whitespace)),
            (r'.*\n', token.Generic.Output),
        ],
        'continuations': [
            (r'( \.\.\. )(.*\n)', bygroups(token.Generic.Prompt, token.Other.Code)),
            (r'( \.\.\.)(\n)', bygroups(token.Generic.Prompt, token.Whitespace)),
            default('#pop'),
        ]
    }

class EvilCandyPromptLexer(DelegatingLexer):
    name = 'evilcandy interactive session'
    aliases = ['evc-console']

    def __init__(self, **options):
        super().__init__(EvilCandyLexer, EvilCandyPromptLexerBase, token.Other.Code, **options)
