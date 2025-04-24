==================
EvilCandy tutorial
==================

:Author: Paul Bailey
:Date: April 2025

.. sectnum::

.. contents::
   :depth: 2

**Update** April 2025.  This source tree, and the language it implements,
is still unstable.  As for the documentation below, about a third of it
is true right now, another third of it was true before but no longer, and
still another third is yet to be true.

**FIXME** April 2025.  This is beginning to look more like a specification
than a tutorial.

Note:

        If you're reading the raw version of this file, you may notice
        it uses the syntax highlighting for JavaScript everywhere.
        This is because I do not expect Github to support anything that
        isn't widely supported by Pygments.  But this is *cosmetic*.
        EvilCandy and JavaScript have *fundamentally incompatible*
        differences, which this document should clear up.

Introduction
============

Intended Reader
---------------

This document assumes you are already familiar with C, JavaScript,
and possibly Python.  If EvilCandy is your first programming language,
then god help you.

This document concerns the language itself, not this source tree
or the compilation/installation process, which should be documented
elsewhere.

Running EvilCandy
-----------------

EvilCandy runs in two modes: script mode and interactive mode.
To run EvilCandy in interactive mode, simply execute ``evilcandy``
without any command-line arguments.  To run a script with EvilCandy,
you must either name it on the command line or pass it through
EvilCandy as a pipe:

.. code::

        # execute 'myscript.evc':
        evilcandy myscript.evc

        # pass 'myscript.evc' as a pipe
        cat myscript.evc | evilcandy

        # evilcandy will expect input from stdin:
        # Send EOF (^D on most terminals) to exit.
        evilcandy

Additional options exist for EvilCandy.  See ``man (1) evilcandy``.

Since interactive mode has some quirks which are best explained after
you know some of the language, we will first discuss script mode.

Source File Encoding Requirements
---------------------------------

Source files must be either ASCII or UTF-8.  Do not include byte order
marks in the file.  With the exception of quoted strings and comments,
all tokens, including whitespace, must be ASCII.

Hello World
-----------

In EvilCandy, a "Hello world" program is the following line:

.. code-block:: js

        print("Hello world");

The semicolon is needed; it marks the end of the expression.
EvilCandy does not look for a function called ``main``.
It executes statements in the order they are written,
starting from the top level of the file.  (A function definition
is a kind of partial statement called an "expression", more on that
in `Expressions and statements`_).

Syntax
======

Comments
--------

There are three kinds of comments, and you've definitely seen all of
them before:

1. Multi-line comments, beginning with ``/*`` and ending with ``*/``
2. Single-line comments, beginning with ``//`` and ending with the
   end of the line.
3. Single-line comments, beginning with ``#`` and ending with the
   end of the line.

Be a good citizen.  Don't mix/match type 3. with 1. and 2.  The only
reason I support 3. is because I want to make the shebang syntax
permissible, ie. having the first line be:

.. code-block:: bash

        #!/usr/bin/env evilcandy

so that the file will execute itself.

Tokens
------

EvilCandy classifies its tokens largely the same way as anyone else does:
whitespace, identifiers, keywords, constants like quoted strings or
numerical expressions, operators, and other separators and delimiters.

Whitespace Tokens
~~~~~~~~~~~~~~~~~

The whitespace characters are space, horizontal tab, vertical tab,
form-feed, newline, and carriage return.  Do not use non-ASCII whitespace.

EvilCandy ignores whitespace, except for some bookkeeping on the line
number to facilitate more helpful error messages.

Identifier Tokens
~~~~~~~~~~~~~~~~~

Identifiers are the names of variables.  They must start with a letter
or an underscore ``_``.
The remaining characters may be any combination of ASCII letters, numbers,
and underscores.
All identifiers in EvilCandy are case-sensitive.

Avoid using identifiers of the pattern "``__*__``" where '``*``' is a
wildcard, except for their use where documented in this tutorial.
EvilCandy uses this pattern for some built-in identifiers that may be
visible to the user.

Identifiers matching the pattern ``_*`` are built-in C accelerators for
library modules.

String Literal Tokens
~~~~~~~~~~~~~~~~~~~~~

String literals are wrapped by either single or double quotes.
Unicode characters are permitted within the quotes so long as they
are encoded in UTF-8.  If any non-UTF-8 characters are encountered,
for example certain Latin1 characters, then the entire string's
reported length will be the number of bytes, even if valid UTF-8
characters exist.  If the entire string is valid UTF-8 (and ASCII
is a subset of 'valid UTF-8'), then the reported length will be the
number of decoded characters.

Backslash Escapes
`````````````````

The following backslash escapes are supported for single characters:

================ =====================================
Escape           Meaning
---------------- -------------------------------------
``"\a"``         bell (ASCII 7--what is this, 1978?)
``"\b"``         backspace (ASCII 8)
``"\t"``         horizontal tab (ASCII 9)
``"\n"``         newline (ASCII 10)
``"\v"``         vertical tab (ASCII 11)
``"\f"``         form feed (ASCII 12)
``"\r"``         carriage return (ASCII 13)
``"\\"``         backslash itself
================ =====================================

Numerical backslashes are also supported.  The rules for numerical
backslashes are as follows:

* ``\U`` must be followed by eight hexadecimal digits.
* ``\u`` must be followed by four hexadecimal digits.
* Octal escapes ``\NNN`` must contain one to three octal digits.
* Hexadecimal escapes ``\xNN`` must contain one to two hexadecimal digits.

The best practice is to always use two digits for hexadecimal escapes
and three digits for octal escapes.  This prevents confusion between
an escaped numerical character and an adjacent numerical character that
is not to be escaped.

Backslash escapes that attempt to insert a nulchar, such as ``"\x00"`` or
``"\u0000"``, will be rejected.  If you must have a value of zero in the
middle, choose a bytes data type instead of a string, (see `Bytes
Literals`_ below).

Unsupported backslash escape sequences will result in a parsing error,
and the script will not be executed.

More on Unicode Escapes
```````````````````````

String literals may contain Unicode characters, either encoded in
UTF-8, or as ASCII representations using familiar backslash
conventions.  The following are all valid ways to express the Greek
letter β:

================== ================
Direct UTF-8       ``"β"``
lowercase u escape ``"\u03b2"``
Uppercase U escape ``"\U000003b2"``
Hexadecimal escape ``"\xCE\xB2"``
Octal escape       ``"\316\262"``
================== ================

For the ``u`` and ``U`` escape, EvilCandy will encode the character as
UTF-8 internally.  Only Unicode values between U+0001 and U+10FFFF are
supported.

Quotation Escapes
`````````````````

If the string literal must contain the same quotation mark as the one
wrapping it, you may either backslash-escape it, or use the alternative
quote.  The following two lines will be interpreted exactly the same way:

.. code-block:: js

        "This is a \"string\""
        'This is a "string"'

String Literal Concatenation
````````````````````````````

Strings must begin and end on the same line.
If a string must wrap for the sake of readability,
write two string literals adjacent to each other.
The parser will interpret this as a single string token.
The following two examples are syntactically identical:

.. code-block:: javascript

        let s = "First line\n"  // first part of token
                "Second line";  // second part of token

.. code-block:: javascript

        let s = "First line\nSecond line";

Note:

        In EvilCandy's current implementation, this kind of concatenation
        is quicker than using the ``+`` operator, because it occurs while
        tokenizing the input.  The ``+`` operation, on the other hand,
        occurs at execution time, even when the l-value and r-value are
        expressed as literals.  This may change in the future.

Bytes Literals
~~~~~~~~~~~~~~

Bytes literals express the bytes data type (see `Strings and Bytes`_
below).  This is used for storing binary data in a octet sequence
whose values are within the range of 0 to 255.  Unlike with string
literals, bytes literals may contain a value of zero within.

Bytes literals are expressed with a letter ``b`` before the quotes.
As with string literals, they may be either single or double quotes.
Unlike strings, bytes literals must all be ASCII text.  To express
non-ASCII or unprintable values, use backslash escapes.  Do not
use Unicode escape sequences.  An example bytes literal:

.. code::

        b'a\xff\033\000b'

This expresses a byte array whose elements are, in order 97
(ASCII ``'a'``), 255 (``ff`` hex), 27 (``033`` octal), 0,
and 98 (ASCII ``'b'``).

Numerical Tokens
~~~~~~~~~~~~~~~~

EvilCandy interprets two kinds of numbers--integer and float.
See `Integers and Floats`_ how these are stored internally.
Complex numbers may become supported in the future.

Literal expressions of these numbers follow the convention used by C,
except that you must not use numerical suffixes.
Write ``12``, not ``12ul``; write ``12.0``, not ``12f``.

The following table demonstrates various ways to express the number 12:

=========== ===========================
**integer expressions**
---------------------------------------
Decimal     ``12``
Hexadecimal ``0xC``
Octal       ``014``
Binary      ``0b1100``
----------- ---------------------------
**float expressions**
---------------------------------------
Decimal     ``12.``, ``12.000``, *etc.*
Exponential ``12e1``, ``1.2e2``, *etc.*
=========== ===========================

Specific rules of numerical interpretation:
 * A prefix of '0x' or '0X' indicates a number in base 16 (hexadecimal),
   and it will be interpreted as an integer.
 * A prefix of '0b' or '0B' indicates a number in base 2 (binary),
   and it will be interpreted as an integer.
 * A number that has a period or an 'E' or 'e' at a position appropriate
   for an exponent indicates a base 10 float.
 * A number beginning with a '0' otherwise indicates a base 8 (octal)
   number, and it will be interpreted as an integer.
 * The remaining valid numerical representations--those beginning with
   '1' through '9' and continuing with '0' through '9'--indicate a base 10
   (decimal) number, and they will be interpreted as an integer.

Note:

        The Python-style ``0o`` prefix for an octal number is not
        supported in this version.  It may be added in the future.


Keyword Tokens
~~~~~~~~~~~~~~

The following keywords are reserved for EvilCandy:

**Table 1**

================ =========== =============
Reserved Keywords
==========================================
``break``        ``const`` *  ``do``
``else``         ``false``   ``for``
``global``       ``if``      ``let``
``function``     ``null``    ``private`` *
``return``       ``this``    ``true``
``while``
================ =========== =============

Note:

        ``private`` and ``const`` were part of early development, but
        they are no longer supported, and may be removed from the
        list of reserved keywords.

All keywords in EvilCandy are case-sensitive.  None are "soft"; you
cannot, for example, declare a variable named ``function``.  (Built-in
functions might be thought of as "soft keywords", however, since they
exist as global variables; local variables take precedence over global
variables.)

Operators
~~~~~~~~~

Besides *relational operators*, which will be discussed in `Program Flow`_,
EvilCandy uses the following operators:

**Table 2.**

+---------+-------------------------+
| Operator| Operation               |
+=========+=========================+
| *Binary Operators*                |
+---------+-------------------------+
| ``+``   | add, concatenation [#]_ |
+---------+-------------------------+
| ``-``   | subtract                |
+---------+-------------------------+
| ``*``   | multiply                |
+---------+-------------------------+
| ``/``   | divide                  |
+---------+-------------------------+
| ``%``   | modulo (remainder)      |
+---------+-------------------------+
| ``**``  | exponentiation          |
+---------+-------------------------+
| ``&&``  | logical AND             |
+---------+-------------------------+
| ``||``  | logical OR              |
+---------+-------------------------+
| ``&``   | bitwise AND [#]_        |
+---------+-------------------------+
| ``|``   | bitwise OR              |
+---------+-------------------------+
| ``<<``  | bitwise left shift      |
+---------+-------------------------+
| ``>>``  | bitwise right shift     |
+---------+-------------------------+
| ``^``   | bitwise XOR             |
+---------+-------------------------+
| *Unary Operators* (before var)    |
+---------+-------------------------+
| ``!``   | logical NOT             |
+---------+-------------------------+
| ``~``   | bitwise NOT             |
+---------+-------------------------+
| ``-``   | negate (multiply by -1) |
+---------+-------------------------+
| *Unary Operators* (after var)     |
+---------+-------------------------+
| ``++``  | Increment by one [#]_   |
+---------+-------------------------+
| ``--``  | Decrement by one        |
+---------+-------------------------+
| *Assignment Operators* [#]_       |
+---------+-------------------------+
| ``=``   | lval = rval             |
+---------+-------------------------+
| ``+=``  | lval = lval ``+`` rval  |
+---------+-------------------------+
| ``-=``  | lval = lval ``-`` rval  |
+---------+-------------------------+
| ``*=``  | lval = lval ``*`` rval  |
+---------+-------------------------+
| ``/=``  | lval = lval ``/`` rval  |
+---------+-------------------------+
| ``%=``  | lval = lval ``%`` rval  |
+---------+-------------------------+
| ``&=``  | lval = lval ``&`` rval  |
+---------+-------------------------+
| ``|=``  | lval = lval ``|`` rval  |
+---------+-------------------------+
| ``<<=`` | lval = lval ``<<`` rval |
+---------+-------------------------+
| ``>>=`` | lval = lval ``>>`` rval |
+---------+-------------------------+
| ``^=``  | lval = lval ``^`` rval  |
+---------+-------------------------+

.. [#] For string and bytes data types, the plus operator concatenates
       the two strings.

.. [#] Except for the modulo operator, bitwise operators are valid when
       operating on integers, but not on floats.

.. [#] The "pre-" and "post-" of preincrement and postincrement are
       undefined for EvilCandy.  Currently increment and decrement
       operations must be their own expressions.

.. [#] Currently ``lval OP= rval`` is not only syntactically the same as
       ``lval = lval OP rval``, but it is the same implementation-wise as
       well, so do not express it this way if the normal way is clearer;
       you will not gain any speed advantage from it.


Expressions and statements
--------------------------

An *expression* is anything that can evaluated and assigned to a single
variable, such as ``1``, ``(1+x)/2``, ``my_function_result()``, and so on.

A *statement* may contain expressions.  Statements take two forms:

:single-line:   *stmt* ``;``
:block:         ``{`` *stmt* ``;`` *stmt* ``;`` ... ``}``

Blocks may be nested, thus each *stmt* above may be a block instead
of a single-line statement, in which case the semicolon is not required.
Braces also define a new `Scope`_, see below.

Valid statements are:

**Table 3**

=== ======================== =============================================
1.  Empty declaration        ``let`` *identifier*
... ...                      ``global`` *identifier*
2.  Assignment               *identifier* ``=`` *expr*
3.  Declaration + assignment ``let`` *identifier* ``=`` *expr*
... ...                      ``global`` *identifier* ``=`` *expr*
4.  Eval [#]_                *identifier* ``(`` *args* ... ``)``
5.  Eval                     ``(`` *expr* ``)``
6.  Empty expression         *identifier*
7.  Program flow             ``if (`` *expr* ``)`` *stmt*
8.  Program flow             ``if (`` *expr* ``)`` *stmt* ``else`` *stmt*
9.  Program flow             ``while (`` *expr* ``)`` *stmt*
10. Program flow             ``do`` *stmt* ``while (`` *expr* ``)``
11. Program flow             ``for (`` *stmt* ... ``)`` *stmt*
12. Return nothing           ``return``
13. Return something         ``return`` *expr*
14. Break                    ``break``
15. Nothing [#]_
=== ======================== =============================================

.. [#] *Eval* has limitations here, see below.

.. [#] ie. a line that's just a semicolon ``;``

Syntax Limitations Regarding Evaluation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In Table 3, *expr* means "thing that can be evaluated and stored in a
single variable". Some examples:

* Combination of literals and identifiers:

.. code-block:: js

        (1 + 2) / x

* Function definition:

.. code-block:: js

        function() {
                do_something();
        }

* List definition:

.. code-block:: js

        [ "this", "is", "a", "list" ]

* Dictionary definition:

.. code-block:: js

        { 'a': 1, 'b': 2 }

Statements may not begin with an expression, with two exceptions:

1. The expression is wrapped in parentheses.  This makes immediately-invoked
   function expressions possible in EvilCandy.  The result will be discarded.

   .. code-block:: js

        (function(arg) {
                do_something();
        })(my_arg);

2. A named function is called but the return value is not assigned to a
   variable.

   .. code-block:: js

        do_something();

All other expressions must either be on the right-hand side of an
assignment operator or else be as described in table 3, such as within
the parentheses of function arguments or program-flow statements.
Note that this restricts the ways to express IIFEs.  Some JavaScript
implementations might allow something like:

.. code-block:: js

        // bad style, doesn't work on EvilCandy :(
        function(arg) {
                thing();
        }(my_arg);

but EvilCandy does not, because no good programmer writes that way
unless they're trying to hide something.  Instead they write:

.. code-block:: js

        // better style, does work on EvilCandy :)
        (function(arg) {
                thing();
        })(my_arg);


Identifier Limitations
~~~~~~~~~~~~~~~~~~~~~~

While you must declare every new variable with either ``let`` or
``global`` (#1 and #3 in Table 3), you may not use these to declare
primary elements.  "Primary elements" refers to the sort of dereferencing
expressions like ``big.damn['mess'].of(stuff)``.  In this case ``big``
would need to be declared, and its descendant elements would all need
to exist for the expression to be valid.

Put more simply, you may state:

.. code-block:: js

        let x = a;      // permissible

but not:

.. code-block:: js

        let x.y = a;    // not permissible

Variables
=========

Storage Class
-------------

Abstracting away how it's truly implemented, there are three storage
classes for variables:

1. *automatic* variables, those stored in what can be thought of as
   a stack.  These are destroyed by garbage collection as soon as
   program flow leaves scope.
2. *closures*, which are created dynamically during the instantiation of
   a new function handle.  These will be explained in greater depth later
   on.
3. *global* variables, which are a part of the global symbol table, and
   are available to all functions, even outside of a script's execution
   (if, say, a script is loaded by another).

Declaring variables
~~~~~~~~~~~~~~~~~~~

The JavaScript ``var`` keyword does not exist in EvilCandy.

Global and automatic variables have a very simple declaration syntax:

* All automatic variables must be declared with the ``let`` keyword:

  .. code-block:: js

          let x;  // or "let x = some_expression;"

* All global variables must be declared with the ``global`` keyword:

  .. code-block:: js

          global x; // or "global x = some_expression;"

This is true *no matter where you are in the program flow*.  This is
important for a couple of reasons.  First, you do not want to declare
a global variable inside of a function or program flow statement
which may execute more than once, or you will get an error.  Second,
functions cannot access automatic variables at the file scope after
the functions' instantiations.

This merits special attention, because it is fundamentally different
from both JavaScript and Python.  **File-scope automatic variables
are not "global" to the functions within that file**.  Instead they
become Closures_, just as a parent function's local variables become
closures to a nested function.  Given the following code:

.. code-block:: js

        global a = 10;
        let b = 10;
        let myfunc = function() {
                a++;
                b++;
        };
        myfunc();
        myfunc();
        print('a:', a)
        print('b:', b));

The output will be:

.. code::

        a: 12
        b: 10

This is because ``b`` inside of ``myfunc`` is a *closure*, a variable
which was instantiated with a value of 10 when ``myfunc`` was created.
Any manipulation of ``b``, reading or writing, done by ``myfunc`` upon
later calls to it will be with the closure, not the outer variable.
*Full* access to automatic variables is only available to code at the
same function scope, where a script is thought of as a function itself.
(There's an additional block-level scope for program flow, but that does
not create closures from variables in the same function; this is
discussed in Scope_ below).

If a script needs its nested functions to access several script-level
variables normally, it can instead create a single file-scope dictionary,
for example ``let locals = { /*...*/ }``.  This works because dictionaries
are mutable objects (see `Dictionaries`_ below).  It has the added benefit
of clarity.  If you see ``locals.x`` instead of just ``x``, it's clearer
what's being manipulated.

Implementation note:

   Automatic variables are not, in the low-level implementation,
   accessed by name.  Rather, they are accessed as offsets from a frame
   pointer, cooked into the pseudo-assembly instructions at parsing time.
   It means that automatic variables are technically much faster than
   global variables.  This speed advantage is mostly only useful with
   algorithmically intense pure functions which need to repeatedly
   manipulate local variables.

   On the other hand, most other kinds of data accesses will be to a
   variable's dictionary attributes, which has approximately the same
   speed as accessing global variables.  So the real reason to avoid
   unnecessary global variables at the file scope is not speed; it's
   just to prevent namespace clutter.

There are two instances where global variables are useful:

1. Prevent cyclic importing of the same script.  When EvilCandy detects
   runaway recursion, it will not raise an exception.  Instead it will
   print a fatal-error message and abort.  This is a problem for complex
   projects where a top-level script may have to import an entire
   hierarchy of subordinate scripts.  Global variables can work around
   this roughly the way preprocessor macros can prevent C headers from
   recursively including themselves:

   .. code::

        $ cat definitions.evc   # some import named 'definitions.evc'

        if (!exists('MYPROJECT_DEFINITIONS_EVC')) {
                global MYPROJECT_DEFINITIONS_EVC;

                // The rest of the script here
        }
        // script returns 'null' by default

2. A work-around for an interactive-mode quirk, where the stack is
   cleared for every top-level statement.  This is considered a design
   flaw, so it may get fixed in a later versions of EvilCandy.

   .. code::

        $ ./evilcandy

        let a = 1;
        print(a);
        [EvilCandy] Runtime Error Symbol a not found

        # need a to be global
        global a = 1;
        print(a);
        1

        # alternatively, wrap it all in a function or a block.
        {
                let a = 1;
                print(a);
        }
        1

If a script at any level tries to access a variable that has not yet been
declared, the global-variables will be searched, even if no ``global``
declaration has been made.  (Implementation-wise, global variables are
entries in a dictionary.)  This is because the parser cannot tell if a
symbol is expected to have been added by an imported script or not.  So
if the symbol truly does not exist, it will be a runtime error instead of
a load-time error.  To catch these mistakes sooner, at parsing time,
instead of later, global variables are generally to be avoided.  See
`Importing Modules`_ below how a source-tree of EvilCandy scripts can be
run from the top level without having to add global variables.


Variable Classes
----------------

Besides storage class, variables also have their own properties,
attributes, behavior, etc., usually called "class", but which
I'll usually call "type" (a consequence of writing too much C).

The default class of variable is ``null``.  All variables that
have been declared without an initializer are set to this.
The table below lists the other main types.  More exist, but these
are the ones that can be initialized with a literal expression
or sequence of literal expressions.  Others require at least a
built-in function to create.

**Table 4**

========== ========================== =========
Type       Declaration Example        Mutable?
========== ========================== =========
integer    ``let x = 0;``             no
float      ``let x = 0.;``            no
list       ``lex x = [];``            yes
dictionary ``let x = {};``            yes
tuple      ``let x = ();``            no
string     ``let x = "";``            no
bytes      ``let x = b"";``           no
function   ``let x = function() {;}`` no
========== ========================== =========

"Mutable" is a commonly-used expression, and unfortunately so, since it
is only true from a low-level implementation point of view.  At the
high-level point of view, "mutable" classes ought to be called
"pass-by-reference" and "immutable" classes ought to be called
"pass-by-value".  There are no "pointers" in EvilCandy.  Instead we use
the abstract concept of a "handle" when discussing mutable variables.
The best way of explaining this is by example in code.

Immutable example (strings, integers, floats, bytes):

.. code-block:: js

        let a = 'hello';
        let b = a;
        b += ' world';  // will not affect a
        print(a);
        print(b);

The output will be:

.. code::

        hello
        hello world

Mutable example (dictionaries, lists):

.. code-block:: js

        let a = [0, 1, 2];
        let b = a;
        b[0] = 'not zero';  // will affect a too
        print(a);
        print(b);

The output will be:

.. code::

        ['not zero', 1, 2];
        ['not zero', 1, 2];

Note:

        Tuples and functions are 'immutable' in the sense
        that write-access to their contents are forbidden.

These variables are dynamically typed.  That is, if you declare ``x`` to
be an integer and later assign the value ``"some string"`` to it, then it
will now become a string.  This does not require you to re-declare the
variable; doing so will result in an error if it is in scope.


Integers and Floats
-------------------

All integers are stored as *signed* 64-bit values.
All floats are stored as IEEE-754 double-precision floating point
numbers.
The literal expression of integers and floats are discussed
in `Numerical Tokens`_.

When both integers and floats are used in calculations, the
result will always be float.

.. code::

        print(2 / 3);
        0
        print(2.0 / 3);
        0.66666666666666663

Lists and Tuples
----------------

Lists are rudimentary forms of numerical arrays.  These are not
efficient at managing large amounts of data.
Use bytes for that.  (The implementation of a Matrix data type
is in the "wishlist" section of my to-do list.)

Once created, lists may not be indexed outside of their bounds.
Lists have a built-in method ``.append`` that may be used to
grow the list.

Set an existing member of a list using the square-bracket notation:

.. code-block:: js

        x[3] = 2;

De-reference lists with the same kind of notation:

.. code-block:: js

        y = x[3];

In the above example, ``3`` may be a variable or more complex expression,
but it **must** evaluate to an integer.  It may not be floating point or
string.

Declare a list containing multiple entries with commas between them,
like so:

.. code-block:: js

        let x = [1, 4, 2];

Do **not** place a comma after the last variable.

:TODO: Too strict? Neither Python nor JavaScript enforces this.

Lists are mutable.  In the example:

.. code-block:: js

        let x = [1, 3, 4];
        let y = x;
        y[0] = 0;

The last line will change the contents of ``x`` as well as ``y``.

Tuples are the same as lists in every way but two:

1. Tuples expressions use parentheses instead of square brackets.

   .. code-block:: js

        let mytuple = (1, 3, 4);

2. Tuples are immutable, while lists are not

   .. code::

        let mytuple = (1, 1, 2);
        let mylist = [1, 1, 2];
        mylist[0] = 0;          // this is ok
        mytuple[0] = 0;         // this is not!

   will result in an error:

   .. code::

        [EvilCandy] Runtime Error Cannot set attribute '0' of type tuple

Dictionaries
------------

Brief rant on terminology
   A dictionary is referred to as an "object" in JavaScript.  There is a
   good reason to keep that terminology, since EvilCandy's JavaScript-like
   notation for dictionaries treats its members like class attributes.
   This is the data class for building up user-defined object classes.
   However, I chose the Python terminology, because calling one object an
   "object" to distinguish it from other "objects" is just plain confusing.
   Regardless of what you can do with it, this data type is an associative
   array, nothing more.

A dictionary is an associative array--an array where you may de-reference
it by enumeration instead of by index number.

Dictionary Literals
~~~~~~~~~~~~~~~~~~~

A dictionary may be declared in an initializer using syntax of the form::

        {
                KEY_1: VALUE_1,
                KEY_2: VALUE_2,
                ...
                KEY_n: VALUE_n
        }

as in the example:

.. code-block:: js

        let x = {
                thing: 1,
                foo: function () { bar(); }
        };

Note the lack of a comma between the last attribute and the closing
brace.  Unlike with most JavaScript interpreters, this is strictly
enforced with EvilCandy.

KEY_i may be either an identifier token or quoted text.  This could be
useful if you want keys that have non-ASCII characters or characters
that violate the rules of identifier tokens:

.. code-block:: js

        let mydict = {
                pi:  3.14159,
                '✓': 'checkmark'
        };

Take care to be consistent how Unicode combinations are entered,
or you may unwittingly use the wrong key later when trying to
retrieve the value.
An explanation of the normalization issue can be found at Unicode's
website `here <https://unicode.org/reports/tr15/>`_.)
Currently EvilCandy does not perform NFKC normalization on Unicode
characters.
Bytes expressions are not allowed for dictionary keys.

VALUE_i may be any data type the user has access to.  Since these
could be functions, dictionaries are useful for object-oriented
programming.  A function that sets up a dictionary, possibly with
closures for some of its fields, and then returns that dictionary,
is basically a class constructor, just as in JavaScript.

While expressing dictionary literals, its values and keys need
not be literals; they may be computed in runtime instead.  However,
the computed keys must be in square brackets, and they must evaluate
to a string data type:

.. code-block:: js

        let key = 'a';
        let value = 1;

        let dict1 = { key: value };
        let dict2 = { [key]: value };

        print('dict1: ', dict1);
        print('dict2: ', dict2);

will output

.. code-block::

        dict1: {'key': 1}
        dict2: {'a': 1}

Note:

        Although this makes it possible to runtime-generate keys, for
        example you could express an entry as ``[k1+k2]: val``,
        this may affect speed due to the increased probability of
        repetitive hash calculating on later dictionary lookups.


Adding Dictionary Attributes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A dictionary may be assigned an empty associative array (``{}``),
and have its attributes added later.  Unlike with lists, you do not
need a special "append" callback:

.. code-block:: js

        let x = {};

        // 'thing' does not exist yet; this will create it
        x['thing'] = 1;

        // 'thing' uses valid identifier syntax, so you may also use dot notation.
        x.thing = 2;

The associative-array notation requires the attribute key to be written
as either a quoted string (``'thing'`` in the example above),
or as a variable which evaluates to a string, like so:

.. code-block:: js

        let key = 'thing';
        x[key] = 1;

Either way, if the key's characters adhere to the rules of an identifier
token, it may still be de-referenced using dot notation.

.. code-block:: js

        x['thing'] = 1;
        // this works because 'thing' is a valid identifier name
        let y = x.thing;

Getting Dictionary Attributes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A dictionary may be de-referenced using the same kind of notation
used for setting attributes: dot notation and associative-array
notation.

.. code-block:: js

        let a = x.thing;
        let b = x["thing"];

Unlike with setting a dictionary's entries, you may not read
an entry unless it already exists.

.. code-block:: js

        let a = { 'a': 1 };
        let x = a.a;    // vailid
        let y = a.b;    // invalid! You will receive an error.

To be sure a dictionary has an entry before accessing it,
use the dictionary's built-in ``.hasattr`` method.

.. code-block:: js

        let y;
        if (a.hasattr('b')) {
                y = a.b;
        } else {
                // do some error handling
                ;
        }

Note:

        See rant above.  EvilCandy does not distinguish between an object
        class's built-in attributes and a dictionary's entries.  Compare
        this to Python's distinct ``hasattr`` and ``in`` keywords.  This
        is simultaneously one of the best and one of the most annoying
        things about JavaScript which EvilCandy nevertheless imitates.)

Dictionary Insertion Order
~~~~~~~~~~~~~~~~~~~~~~~~~~

Dictionary insertion order is not preserved, nor may its contents be
accessed with numerical subscripts.  When iterating over the members
of a dictionary, however, the iteration will be in alphabetical order
of its keys.

Is It a Class or a Dictionary?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In a word...yes.

It depends on what you want it to be. Dictionaries are the most mutable of
EvilCandy's data types.  EvilCandy permits dot notation on dictionaries
specifically for the purpose of making them be object classes, with a
user-defined set of named methods and private data.

Part of my motivation for imitating JavaScript's model of data types and
tokens (as opposed to Python's or--god forbid--PHP's or Visual Basic's)
is the beautiful elegance [#]_ with which JavaScript allows you to use
dictionaries, closures, and lambdas to invent an object class without
actually requiring a syntax dedicated to creating classes.  JavaScript's
"class" notation is superfluous, and seems to mollycoddle programmers
whose minds are locked into whatever paradigm their previous programming
language taught them.

But it comes with quirks, including some under-the-hood kluging to
make sure ``this`` refers to the correct object (see next section).

.. [#]
        I do not extend that compliment to the unreadable and frankly
        ugly conventions of JavaScript programming style.
        Its name is ``i``, not ``ThisVariableIsAnIteratorInAForLoop``!

Dictionary Inheritance: The Naïve Way
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Hand-copying one dictionary's functions to another dictionary is
not advised.  Consider the following example:

.. code-block:: js

        let a = {
                'b': function() { return this.len(); }
        };
        let c = {
                'd': a.b,
                'e': 'c has length of 2'
        };
        print(c.d());

The output will be ``1``, not ``2``, because ``c.d`` will still print the
length of ``a`` instead of ``c``.  Whenever a function (and only a function)
is retrieved from a dictionary, it is put inside a wrapper (called a
"method" in the code) that binds the function to its parent [#]_.  (This is
a little misleading, because it means that in the case of ``x = object.key``,
it will not actually be assigned the exact function stored in ``object.key``,
but rather it will contain a small object pointing to the function and the
dictionary both.  I tried to write the code so that it's functionally
equivalent, as far as the high-level user is concerned.)

Dictionaries have a built-in method called ``.purloin`` which re-claims
contained methods as its own [#]_.

.. code-block:: js

        c.purloin('d');
        print(a.b(), c.d());

The output will now be appropriately ``1 2``.

**Consider this method dangerous**.  What if ``a.b`` references
``this.x`` where ``x`` is contained in ``a`` but not in ``c``?  What if
``a.b`` is a built-in function for something that isn't a dictionary at
all?  ``purloin`` should only be used when you know exactly what the
"purloined" function is and does.  The only reason I added ``purloin`` at
all is because I'm certain that down the road in some dark corner of the
programming world, the better method for inheritance (next section) will
not be sufficient.

.. [#]
        Low-level impementation note: Checks are done to prevent cyclic
        dependency should the method be re-inserted into the same object
        that it's bound to.  In this case, the method will be destroyed
        and plain unbound function will be inserted instead.  This is a
        rare occasion, however, and the above example, which does not
        involve cyclic dependency, is more likely.

.. [#]
        Don't worry about ``a`` in this case, c.purloin() will
        not affect ``a``'s contents.

Dictionary Inheritance: The Better Way
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The pipe character ``|`` acts as a union operator when its left and
right values are both dictionaries.  In the case of

.. code:: js

        c = a | b;

``c`` will be set to a dictionary that has all of ``a``'s contents as
well as ``b``.  If there are any matching keys between the two, the
right-hand side will take precedence.  Inheritance might look like:

.. code:: js

        let new_obj = base_1() | base_2() | {
                /* ...new or overriding values... */
        };

where ``base_x()`` are base-class constructors which return dictionaries
for  ``new_obj`` to inherit, and the dictionary on the right contains
either additional values or overriding values which are specific for the
newly created.  This does not perform any in-place manipulation; the
dictionaries in the union loop will not be affected, except for the
result, ``new_obj`` [#]_.

This is also useful for selectively overriding default parameters (see
the ``makebox`` example in `Function Definition Syntax`_ below).

The ownership issue described in the previous section does not exist
here, because a function is only bound to its owner when it is
"gotten" from an object, such as when you type ``object.key`` (which
puts a temporary variable on the stack) or ``x = object.key`` (which
assigns the newly bound function ``key`` to ``x``).

Warning:
        It may be tempting to use ``purloin`` here too, as a redundant
        step, but don't.  If any of the base classes contain 'methods'
        bound to some other object out there, it's probably for a good
        reason.

.. [#]

        As an implementation note, there is a slight speed advantage to
        an in-place operation, but it is far *too* slight to justify
        itself compared to the cleanliness and consistency of treating
        all binary operators the same for every type.

Strings and Bytes
-----------------

In EvilCandy a string is a sequence of text.  Internally, they are
nulchar-terminated C strings with additional metadata.  They can be
represented by string literals (see `String Literal Tokens`_ above).

Bytes are binary data arrays whose values are unsigned, in the range
0 to 255.

Strings are intended to be thought of in a more abstract sense than
bytes.  When iterated over or accessed by subscript, bytes return an
integer and strings return a single-character string.

.. code-block:: js

        let mybytes  = b'hello';
        let mystring = 'hello';
        print(mybytes[0]);
        print(mystring[0]);

will output:

.. code-block:: js

        104
        h

Bytes and strings are both immutable.  You may read a subscript but you
may not assign a subscript.

Functions
---------

A function executes code and returns either a value or an empty variable.

In EvilCandy, **all functions are anonymous**.
The familiar JavaScript notation:

.. code-block:: js

        function foo() {...

is **not** permitted.  Instead declare a function by assigning it
to a variable:

.. code-block:: js

        let foo = function() {...

(More on this later when I get into the weeds of IIFE's, lambdas,
closures, and the like...)

The ``typeof`` Builtin Function
-------------------------------

A variable can have its type checked using the builtin ``typeof``
function.  This returns a value type string.  Depending on the
type, it will be one of the following:

**Table 5**

========== =======================
Type       ``typeof`` Return value
========== =======================
bytes      "bytes"
dictionary "dictionary"
float      "float"
function   "function"
integer    "integer"
list       "list"
null       "empty"
string     "string"
tuple      "tuple"
========== =======================

Program Flow
============

In this section, *condition* refers to a boolean truth expression.
Since program flow requires this, let's start there...

Condition Testing
-----------------

*condition* is evaluated in one of two ways:

1. Comparison between two objects

   *expr* OPERATOR *expr*

2. Testing a single object for truthiness:

   *expr*


Condition testing may be expanded with boolean operators
already mentioned (``&&``, ``||``, etc.).  The final result
will be either ``true`` or ``false``.

Comparison between two objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Comparisons have two expressions with a relational
operator between them.  The relational operators are:

**Table 6**

======== ========================
Operator Meaning
======== ========================
==       Equals
!=       Not equal to
<=       Less than or equal to
>=       Greater than or equal to
<        Less than
>        Greater than
======== ========================

If the two values are an integer and a float (in either order), then
the integer's floating point conversion will be used for the comparison.
In all other occasions where the left and right values are **different
types**, the result will be a string comparison of their type names.

Do not compare one object to ``true`` or ``false`` directly.  Instead,
use the single-object method:

Testing a single object for truthiness
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If a conditional test contains a single expression,

Boolean expressions ``true`` and ``false`` are actually integer types.
They are aliases for 1 and 0, respectively.  They were intended for
convenient assignments and return values, not for comparisons.  The
expressions ``(null == false)`` and ``(null == true)`` *both* evaluate
to ``false``!  So instead of ``if (my_variable == true)`` you should
just use ``if (my_variable)``, which means "does this expression evaluate
to 'true'?".

The following conditions result in a variable evaluating to *true*:

:FIXME: This table is what it **should** be, I need to update code (see to-do.txt)

**Table 7**

============ ==================================================
Type         Condition
============ ==================================================
empty (null) false always
integer      true if != 0
float        true if not subnormal or != 0.0
list         true if its size is greater than zero
bytes        true if its size is greater than zero
tuple        true if its size is greater than zero
dictionary   true if it has at least one entry
string       true if not the empty "" string
function     true always
============ ==================================================

``if`` statement
----------------

An ``if`` statement follows the syntax::

        if (CONDITION)
                STATEMENT

If *statement* is multi-line, it must be surrounded by braces.

If condition is true, *statement* will be executed, otherwise it will
be skipped.

``if`` ... ``else if`` ... ``else`` chain
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``if`` statement may continue likewise::

        if ( CONDITION_1 )
                STATEMENT_1
        else if ( CONDITION_2 )
                STATEMENT_2
        ...
        else
                STATEMENT_N

This is analogous to the ``switch`` statement in C and JS (but which is
not supported here).

``do`` loop
-----------

The ``do`` loop takes the form::

        do
              STATEMENT
        while ( CONDITION );

*statement* is executed the first time always, but successive executions
depend on *statement*.

``while`` loop
--------------

The ``while`` loop takes the form::

        while ( CONDITION )
                STATEMENT

``for`` loop
------------

There are two kinds of ``for`` loops.

C-Style ``for`` loop
~~~~~~~~~~~~~~~~~~~~

The statement::

        for ( STATEMENT_1; CONDITION; STATEMENT_2 )
                STATEMENT_3

is equivalent to::

        STATEMENT_1
        while ( CONDITION ) {
                STATEMENT_3
                STATEMENT_2
        }

The iteration step (the *statement_2* part of the ``for`` loop header)
is one of only two cases where a single-line expression does not end in a
semicolon; the other is with EvilCandy's notation for tiny lambdas.

You may declare the iterator in *statement_1* with ``let``, e.g.:

.. code-block:: js

        for (let i=0; i < n; i++) {...

in which case ``i`` will be visible inside the loop but not outside of
it.  However, this only works if ``i`` has not been declared yet in the
outer scope, or you will get a multiple-declaration error.  (See Scope_.)

**This is highly deprecated.** It's great for a low-level language like
C, but not so great for a high-level language like EvilCandy.  Use the
method discussed below instead.

EvilCandy-Preferred ``for`` loop
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The statement::

        for ( NEEDLE, HAYSTACK )
                STATEMENT

is equivalent to Python's

.. code-block:: python

        for NEEDLE in HAYSTACK:
                STATEMENT

*needle* must be a single-token identifier, declaring a new local
variable which will only be visible within the scope of the for loop.
This is (currently) the only occasion outside of a function definition
where an automatic variable may be declared without the ``let`` statement.

*haystack* is an iterable object, and for each iteration of the loop,
*needle* will be set to a different member of *haystack*, in order.
If *haystack* is a dictionary (and therefore not sequential), then
*needle* will be set to each member of its keys rather than its values.
Since the insertion order is not preserved for dictionaries, the order of
iteration will be alphabetical instead.

In EvilCandy, a trivial example may be the following, which prints
all the keys and values in some dictionary ``mydict``:

.. code-block:: js

        for (key, mydict) {
                print('key:', key);
                print('val:' mydict[key]);
        }

If you need to iterate over a sequence of numbers, you can use the
``range()`` built-in function to create an object which will iterate for
you.  This is based on Python's range object.  As with Python, a
``range`` object is highly compact; its members are not stored in memory,
but rather they are retrieved algorithmically upon request; considering
that only three parameters (start, stop, and step) constitute all the
necessary computation, this is actually faster in EvilCandy than its C-style for loop.
the built-in ``range()`` function takes 1 to three arguments, all integers.
The prototype is:

.. code::

        // when start and step are not provided as arguments,
        // the defaults are start=0 and step=1
        range(STOP);
        range(START, STOP);
        range(START, STOP, STEP);

For those who prefer the JavaScript-like ``.foreach`` object methods,
these exist too, but they have the overhead of frame swapping, and should
not be used in algorithmically intense scenarios.

``for`` - ``else`` combination
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Warning! Deprecated!

        'else' will be replaced by a different keyword

        Even though I dislike keyword bloat, repurposing 'else' here is
        poorly suited to EvilCandy's JS-like notation, where someone's sloppy
        neglect of braces can give rise to misleading indentation.  Consider
        something like "for...if...else".  If braces were not used, the
        'else' is the response to 'if', no matter how it was indented.
        Even more misleading is "if...for...else".

        So I will probably replace it with 'otherwise', 'orelse', or just
        'orlse', as in 'there better be no bugs in this code, orlse...'

EvilCandy's ``for`` loop has an optional following ``else`` statement,
another imitation of Python.  In the following example (cribbed and adapted
straight from an algorithm in the python.org `documentation
<https://docs.python.org/3.12/tutorial/controlflow.html#for-statements>`_):

.. code-block:: js

        // Print prime numbers from 2 to 10
        for (let n = 2; n < 10; n++) {
                for (let x = 2; x < n; x++) {
                        if ((n % x) == 0)
                                break;
                } else {
                        print(n);
                }
        }

the ``break`` statement escapes completely from the inner ``for`` loop;
but if the loop continues to iterate until failure of the ``x < n`` test,
the statement in the ``else`` block will be executed.

Scope
=====

I have already mentioned global variables, and function- and file-scope
automatic variables.  If a statement is in its block form, ie. it is
surrounded by braces ``{`` and ``}``, or if it is inside a program flow
statement like a ``for`` loop, any automatic variables declared in that
scope will be visible only until program flow leaves that scope.  The
code in these blocks still have full access to their functions' local
variables also in scope--they have not become closures--so new variables
still may not violate he namespace.

In the following example, ``x`` is only visible inside the ``if`` statement.

.. code-block:: js

        let thing = function(a, b) {
                if (b) {
                        let x = b;
                        ...
                }

                // THIS WON'T WORK!!
                let a = x;  // x no longer exists
                ...

However, automatic variables **may** supersede global variables with the
same name.  The following code is valid:

.. code-block:: js

        // at the global level
        global a = 1;

        let thing = function(b) {
                if (b) {
                        // local a takes precedence over global a
                        let a = 2;
                        ...
                } else {
                        // local a left scope and may be re-declared
                        let a = 3;
                        ...

But the following will not work, because the second declaration of ``a``
occurs while the first declaration--an automatic variable in the same
function--is still in scope:

.. code-block:: js

        let thing = function(b) {
                let a = 1;
                if (b) {
                        // THIS WON'T WORK
                        let a = 2; // local a still in scope
                        ...

Function Syntax
===============

Function Definition Syntax
--------------------------

Function definitions take the form::

        function(ARGS)
                STATEMENT

*statement* should have braces even if it's a single-line expression
(it's just good practice), but EvilCandy does not enforce that.

*args* is a group of identifiers, delimited by commas, which will be
used to identify the caller's parameters, e.g.:

.. code-block:: js

        function(x, y, z)

An *optional argument* may be designated as::

        ARG = DEFAULT

where *default* is an expression that evaluates to a default value for
the argument should one not be provided by the caller, e.g.:

.. code-block:: js

        let makebox = function(size, height, outline=false, fill=false);

This example is actually poor function design.  What if the caller wanted
to override the default for ``fill`` but not for ``outline``?  These are
not keyword arguments: **the order in which arguments are defined
matters**, for optional arguments and mandatory arguments alike.  (For
that reason, it makes no sense to place the optional arguments at the
front of the argument list.) Better practice is to always have at most
one default argument, which can substitute as a sort of keyword-arguments
list, like this:

.. code-block:: js

        let makebox = function(size, height, useropts={}) {
                let opts = {
                        /* default opts */
                        'outline': false,
                        'fill':    false,
                } | useropts;

                /* ...the function def... */
        };

Here, ``size`` and ``height`` are required arguments.  The constructor
function can also use ``opts.outline`` and ``opts.fill``, which are
either from the caller or from the declared defaults if they were not
provided.  (See `Dictionary Inheritance: The Better Way`_ above for how
the ``|`` operator is used with dictionaries.)

Caution!

        Be aware that the caller could send any data type, not necessarily
        the type defined as the default.  If the function does not want an
        exception to be raised due to operators used on improper types, it
        should check the argument's type itself.

Function Call Syntax
--------------------

The number of arguments provided must be at least as many as the number
of parameters defined in the function definition up to the last mandatory
argument defined--that is, the right-most parameter that does not have a
default value.  No error will be thrown in the case of excess arguments,
however it will result in wasted stack space.

The arguments are not type-checked.  If the wrong type was provided to
the function, that will likely be discovered soon enough while the
function itself is executing.

A function may not always return the same type.  For example, a file
object's ``read`` method will return a string if it's in text mode
or a bytes object if it's in binary mode.  If a caller ever gets into
a state where it isn't sure which type to expect, it can use the
``typeof`` builtin function to check it.

Callable Dictionaries
---------------------

A dictionary can be called like a function if it has an attribute
named ``__callable__`` which evaluates to a function handle.

For example, given the dictionary:

.. code-block:: js

        let mydict = {
                a: 1,
                b: 3,
                __callable__: function () {
                        foo(this.a, this.b);
                }
        };

then a call to ``mydict()`` is equivalent to calling
``mydict.__callable__()``.  The number and type of arguments for
``__callable__`` may be entirely user-defined.  Note that this
is a trivial example, however, and a simpler way is to use Closures_.

Lambda Functions
----------------

Normal function notation may be used for lambda functions, but if you
want to be cute and brief, special notation exists to make small lambdas
even smaller, most easily shown by example:

.. code-block:: js

    let multer = function(n) {
            return ``(x) x * n``;
    };

This is equivalent to:

.. code-block:: js

    let multer = function(n) {
            return function(x) { return x * n; };
    };

(Note: the out-of-scope use of ``n`` is explained in Closures_ below).

In both examples, the return value is technically a lambda function.
But for our purposes, *lambda notation* refers to the former case,
where the double back-quote tokens (``````) provide syntactic sugar
for a very small function.  The general form is::

        `` ( ARGS ) EXPR ``

where *expr* is only an evaluation, not a full statement.  It does not
end with a semicolon.  If a lambda requires a more complex statement,
you must add back in the braces and ``return`` statement...in which case
you are better off using the regular function notation; the `````` token
is hard to spot over more than one line.

Lambdas are useful in the way they create new functions, for example [#]_:

.. code-block:: js

        let multer = function(n) {
                return ``(x) x * n``;
        };

        let doubler = multer(2);
        let tripler = multer(3);

        let a = doubler(11);
        let b = tripler(11);

        print(a);
        print(b);

will print the following output::

        22
        33

In this example, ``multer`` was used to create a function that multiplies
its input by a value determined at the time of its instantiation.

.. [#]
        This example was adapted from
        `<https://www.w3schools.com/python/python_lambda.asp>`_

It should be noted that lambda notation is merely syntactic sugar designed
to remove visual clutter from the code.  It has no performance benefit over
normal function notation.

Closures
--------

In the previous section `Lambda Functions`_, the lambda function used
a variable ``n`` that was in its parent function scope.  This variable
will now persist until the return value (``doubler`` or ``tripler``
in the example) is deleted.  This is known as a *closure*.  Because
it is evaluated at the time of the function's creation, it can be
unique for each instantiation (note that ``doubler`` and ``tripler``
maintain their own values of ``n``).

Implicit Closure Declaration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To implicitly declare a closure, simply reference a variable in an
ancestor function's scope, as in the ``multer`` example:

.. code-block:: js

        let multer = function(n) {
                return ``(x) x * n``;
        };

Note, however, that this only pertains to automatic variables.  If the
variable is global, then a closure will not be created.  In the example:

.. code-block:: js

        global n = some_value;
        let foo = function() {
                bar(n);
        };

Since ``n`` is global, a closure will not be created, and ``foo`` will
not have unique access to its own instantiation of ``n``.

Explicit Closure Declaration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Closures may also be declared in a function's parameter heading with
the syntax::

        : NAME = VALUE

To use the ``multer`` example again:

.. code-block:: js

        let multer = function(n) {
                return ``(x, :a=n) x * a``;
        };

Here, the ``a`` of ``:a=n`` is the name given to the parameter,
and ``n`` is the value to set it to [#]_.

This is **not** an argument to the function!  Unlike with default
arguments, this value cannot be overridden by a caller's own argument,
nor does it shift the placement of the actual arguments.
For the sake of readability, however, placing explicit closure
declarations like this at the end of the parameter list is
good practice.

.. [#]
        Since it will be in a new scope, you could also reuse the
        name n for consistency, thus the declaration would be
        ``:n=n``.  I renamed it ``a`` in the example to be clearer
        what's going on.

Explicit closure declarations are a consequence of early development
when I had the stupid idea of making strings be mutable objects which
could be duplicated with a call to a built-in ``.copy`` method.  This
causes a nuance where the ``.copy`` call would in one case be called
when the closure is created and in another case be called each time
the function containing the closure is called.  But since then,
strings are now immutable, so explicit closures hardly matter.

Importing Modules
=================

Interactive Mode
================

Built-in Methods and Inheritance
================================

Built-in Methods for Dictionaries
---------------------------------

Built-in Methods for Lists
--------------------------

Built-in Methods for Strings
----------------------------

Library
=======

Io
--

Math
----

Low-Level Operation
===================

Byte Code Instructions
----------------------

Disassembly Option
------------------

:TODO: The rest of this documentation

.. : vim: set syntax=rst :

