==================
EvilCandy tutorial
==================

:Author: Paul Bailey
:Date: November 2023

.. sectnum::

.. contents::
   :depth: 2

**Update** December 2023.  For now, probably until end of year, this
document is wildly out of date, since the program is changing rapidly.

.. note::
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

Running EvilCandy
-----------------

:TODO:
        Need to add the 'what is EvilCandy' kind of stuff: how to
        compile? how to execute? does it have an interactive mode?
        etc.

Source File Encoding Requirements
---------------------------------

Source files must be either ASCII or UTF-8.  Do not include byte order
marks in the file.  With the exception of quoted strings (see String_)
and Comments_, all tokens, including whitespace, must be ASCII.

Hello World
-----------

In EvilCandy, a "Hello world" program is the following line:

.. code-block:: js

        print("Hello world");

The semicolon is needed; it marks the end of the expression.
EvilCandy does not look for a function called ``main``.
It executes expressions in the order they are written,
at the top level of the file.  (A function definition is a
kind of partial expression, more on that in Expressions_).

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

Be a good citizen.  Don't mix/match type 3. with 1. and 2.  I support 3.
only because I want to make the shebang syntax permissible, ie. having
the first line be:

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

EvilCandy ignores whitespace, with three exceptions:

1. The newline character ``\n`` is accounted for, to facilitate error
   reporting (it's nice to know the line number where a program failed).

#. If a string literal spans a newline, unless that newline is escaped
   with a backslash, that newline will be a part of the string literal.
   (See `String Literal Tokens`_).

#. Some tokens may require at least one whitespace character to delimit
   them from each other.

Identifier Tokens
~~~~~~~~~~~~~~~~~

Identifiers must start with a letter or an underscore ``_``.
The remaining characters may be any combination of ASCII letters, numbers,
and underscores.
All identifiers in EvilCandy are case-sensitive.

Avoid using identifiers of the pattern "``__*__``" where '``*``' is a
wildcard, except for their use where documented in this tutorial.
EvilCandy uses this pattern for some built-in identifiers that may be
visible to the user.

Identifiers matching the pattern ``_{name}`` are built-in modules,
wherein their appurtenant load command would be ``load "{name}.evc"``.

String Literal Tokens
~~~~~~~~~~~~~~~~~~~~~

String literals are wrapped by either single or double quotes.  If the quote
must contain the quotation mark, you may either backslash-escape it, or
use the alternative quote.  The following two lines will be interpreted
exactly the same way:

.. code-block:: js

        "This is a \"string\""
        'This is a "string"'

Strings behave peculiarly around line endings.  The following
examples will all be interpreted identically (except for the manner
in which the line number is saved for error dumps):

.. code-block:: js

        "A two-line
        string"

        "A two-line\n\
        string"

        "A two-line\nstring"

        "A \
        two-line
        string"

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
UTF-8 internally.  Only Unicode values that may be encoded into UTF-8
(up to 10FFFF hexadecimal, or 1 114 111 decimal) are supported.

Octal escapes ``\NNN`` must contain one to three numerical values.
Hexadecimal escapes ``\xNN`` must contain one to two numerical values.
The best practice is to always use two digits for hexadecimal escapes
and three digits for octal escapes.  This prevents confusion between
an escaped numerical character and an adjacent numerical character that
is not to be escaped.

The following additional (hopefully familiar) backslash escapes are
supported.

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
``"\<newline>"`` do not include newline in the literal
================ =====================================

:TODO:
        support for HTML-entity escaping, like ``"\&{ldquo}"``
        would be nice.

Numerical Tokens
~~~~~~~~~~~~~~~~

EvilCandy interprets two kinds of numbers--integer and float.
See Integers_ and Floats_ how these are stored internally.

Literal expressions of these numbers follow the convention used by C.

Numerical suffixes are unsupported.
Write ``12``, not ``12ul``; write ``12.0``, not ``12f``.

The following table demonstrates various ways to express the number 12:

=========== ===========================
**integer expressions**
---------------------------------------
Decimal     ``12``
Hexadecimal ``0x12``
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
 * The remaining valid numerical representations--those begining with
   '1' through '9' and continuing with '0' through '9'--indicate a base 10
   (decimal) number, and they will be interpreted as an integer.


.. note::
        As of 12/2023, EvilCandy's assembler does not optimize compound
        statements that happen to be all literals.  ``1+2`` will be
        interpreted as two separate tokens, and the addition will be
        performed on them in the byte code at execution time.

Keyword Tokens
~~~~~~~~~~~~~~

The following keywords are reserved for EvilCandy:

**Table 1**

================ ========= ==========
Reserved Keywords
=====================================
``function``     ``let``   ``return``
``this``         ``break`` ``if``
``while``        ``else``  ``do``
``for``          ``load``  ``const``
``private`` [#]_ ``true``  ``false``
``null``
================ ========= ==========

.. [#] ``private`` is unsupported, but it's reserved in case I ever do support it.

All keywords in EvilCandy are case-sensitive

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

.. [#] For string data types, the plus operator concatenates the two strings.

.. [#] Bitwise operators are only valid when operating on integers.

.. [#] The "pre-" and "post-" of preincrement and postincrement are undefined for EvilCandy.
       Currently increment and decrement operations must be their own expressions.

.. [#]
        Although an expression of the form ``lval OP= rval`` is
        syntactically equivalent to ``lval = lval OP rval``, the former
        is slightly faster in EvilCandy due to the way it operates the
        stack..


Expressions
-----------

An expression may be:

:single-line:   *expr* ``;``
:block:         ``{`` *expr* *expr* ... ``}``

In the block case, the nested instances of *expr* must be single-line.
Nested blocks are only permitted if they're part of program-flow
statements like ``if`` or ``while``. (**TODO** I can't recall why this
is, maybe I should support it.)

Braces also define a new `Scope`_, see below.

Valid single-line expressions are:

**Table 3**

=== ======================== =============================================
1.  Empty declaration        ``let`` *identifier*
2.  Assignment               *identifier* ``=`` *value*
3.  Declaration + assignment ``let`` *identifier* ``=`` *value*
4.  Eval [#]_                *identifier* ``(`` *args* ... ``)``
5.  Eval                     ``(`` *value* ``)``
6.  Empty expression         *identifier*
7.  Program flow             ``if (`` *value* ``)`` *expr*
8.  Program flow             ``if (`` *value* ``)`` *expr* ``else`` *expr*
9.  Program flow             ``while (`` *value* ``)`` *expr*
10. Program flow             ``do`` *expr* ``while (`` *value* ``)``
11. Program flow [#]_        ``for (`` *expr* ... ``)`` *expr*
12. Return nothing           ``return``
13. Return something         ``return`` *value*
14. Break                    ``break``
15. Load [#]_                ``load``
16. Nothing [#]_
=== ======================== =============================================

.. [#] *Eval* has limitations here, see below.

.. [#]
        ``for`` loop header have the same format as C ``for`` loops:
        expression-eval-expression, delimited by semicolons between
        them, surrounded by parentheses.  The iteration step (part
        3 of the header) is one of only two cases where a single-line
        expression does not end in a semicolon; the other is with
        EvilCandy's notation for tiny lambdas.

.. [#]
        ...if I ever get around to implementing it. And when I do,
        ``load`` is only valid at the top level.  It may not be nested
        within a function or a loop statement.  It *may* be within an
        if statement, which is useful in the case of something like::

                if (!__gbl__.hasattr("myclass"))
                        load "myclass.evc";

.. [#] ie. a line that's just a semicolon ``;``

Syntax Limitations Regarding Evaluation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In Table 3, *value* means "thing that can be evaluated and stored in a
single variable". Some examples:

* Combination of literals and identifiers:

.. code-block:: js

        (1 + 2) / x

* Function definition [#]_:

.. code-block:: js

        function() {
                do_something();
        }

* List definition:

.. code-block:: js

        [ "this", "is", "a", "list" ]

* dict definition:

.. code-block:: js

        {
                _this: "is",
                a: "dictionary"
        }

.. [#]
        The "single variable" this evaluates to is a callable handle to
        the Function_.

Only limited versions of these may *begin* an expression, namely cases
4-6 in Table 3: #4: function calls with ignored return values;
%5: expressions wrapped in parentheses; and #6: ignored empty identifiers.
For a full range of *value* to be permitted, it has to be on the
right-hand side of an assignment operator, as in cases 2 and 3, or
within the parentheses of a program-flow statement, as in cases 7-11.

The parentheses exception makes IIFE's possible. Some Javascript
implementations might allow something like:

.. code-block:: js

        // bad style :(
        function(arg) {
                thing();
        }(my_arg);

but I do not, because no good programmer writes that way unless they're
trying to hide something.  Instead they write:

.. code-block:: js

        // better style :)
        (function(arg) {
                thing();
        })(my_arg);

It's only because of convention, but still the latter case makes clearer
that you're calling the anonymous function rather than just declaring it.
I merely enforce the better choice, at the cost of some complexity in my
parser.

Identifier Limitations
~~~~~~~~~~~~~~~~~~~~~~

In the declaration cases (#1 and #3 in Table 3), *identifier* must be simple;
that is, you can type:

.. code-block:: js

        let x = a;      // permissible

but not:

.. code-block:: js

        let x.y = a;    // not permissible

In all other cases of *identifier* "primary elements" notation (things
like ``this.that``, ``this['that']``, ``this(that).method[i]`` and so
on...) is allowed.

Variables
=========

Storage Class
-------------

Abstracting away how it's truly implemented, there are four storage
classes for variables:

1. *automatic* variables, those stored in what can be thought of as
   a stack.  These are destroyed by garbage collection as soon as
   program flow leaves scope.
2. *closures*, which are analogous to function-scope ``static`` variables
   in C, except that in EvilCandy, as with JS, there is a different one
   for each instantiation of a function.
3. *global* variables, which are syntactically the same thing as automatic
   variables, except that they remain in scope forever.
4. Variables that are attributes of another variable... an element of a
   list or dictionary or one of any type's built-in methods.  These are
   accessed the same way an attribute of a dictionary or list is accessed
   (more on that below).

Declaring automatic variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All automatic variables and global variable (type 3 above, not type 4)
must be declared with the ``let`` keyword:

.. code-block:: js

        let x;

The JavaScript ``var`` keyword does not exist in EvilCandy.

Types of Variables
------------------

The above example declared ``x`` and set it to be an *empty* variable.
EvilCandy is not dynamically typed; the only variable that may be changed
to a new type is an *empty* variable.  The other types are:

**Table 4**

========== ========================== =========
Type       Declaration Example        Pass-by
========== ========================== =========
integer    ``let x = 0;``             value
float      ``let x = 0.;``            value
list       ``lex x = [];``            reference
dictionary ``let x = {};``            reference
string     ``let x = "";``            reference
function   ``let x = function() {;}`` reference
========== ========================== =========

There are no "pointers" in EvilCandy.  Instead we use the abstract
concept of a "handle" when discussing pass-by-reference variables.
Handles' *contents* may be modified, but the handles themselves
may not; they may be only assigned.  For example, given a function
handle assignment:

.. code-block:: js

        let foo = function() { bar(); };

then the following will result in errors:

.. code-block:: js

        foo++;

.. code-block:: js

        foo = foo + bar;

The only time variables may be assigned using something of a different
type is when the l-value and r-value are both integers or floats.
For example:

.. code-block:: js

        let x = 1;      // integer
        let y = 1.4;    // float
        x = x + y;      // x is still integer, equals 2

is valid.  Instead of adding ``y`` to ``x`` this will add an
intermediate variable that is the value of ``y`` cast into the
type of ``x``.


Integers
--------

The literal expression of integers are discussed in `Numerical Tokens`_.

All integers are stored as *signed* 64-bit values.  In EvilCandy these
are pass-by-value always.

Floats
------

The literal expression of floats are discussed in `Numerical Tokens`_.

All floats are stored as IEEE-754 double-precision floating point
numbers.  Floats are pass-by value always.

Lists
-----

Lists are rudimentary forms of numerical arrays.  These are not
efficient at managing large amounts of data.
Lists are basically more restrictive versions of dictionaries.
There are two main differences:

1. Lists' members must all be the same type.  (There are quirks,
   however.  If a list's members are themselves lists, they need
   not be the same length or contain the same type as their sibling
   members; same goes for lists of dictionaries.)
2. Lists do not have associative indexes; ie may only be de-referenced
   numerically.

Set an existing member of a list using the square-bracket notation:

.. code-block:: js

        x[3] = 2;

De-reference lists with the same kind of notation:

.. code-block:: js

        y = x[3];

In the above example, ``3`` may be a variable, but the variable type
**must** be an integer.  It may not be floating point or string.

Declare a list with multiple entries with commas between them,
like so:

.. code-block:: js

        let x = [1, 4, 2];

Do **not** place a comma after the last variable.

Lists are pass-by reference.  In the example:

.. code-block:: js

        let x = [1, 3, 4];
        let y = x;
        y[0] = 0;

The last line will change the contents of ``x`` as well as ``y``.

Dictionaries
------------

A dictionary is referred to as an "object" in JavaScript (as well as,
unfortunately, my source code).  Here I choose more appropriate language,
since technically all of these data types have some object-like
characteristics.

A dictionary is an associative array--an array where you may de-reference
it by enumeration instead of by index number.  Unlike lists, its contents
do not need to all be the same type.

All dictionaries are pass-by reference.

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

Adding Dictionary Attributes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A dictionary may be assigned an empty associative array (``{}``),
and have its attributes added later.
It may use the dot notation, so long as the attribute key is a valid
identifier token:

.. code-block:: js

        // make sure x is defined as a dictionary
        let x = {};

        // create new element 'thing' and assign it a value
        x.thing = 1;

        // ditto, but 'foo'
        x.foo = function() { bar(); }

or it may use the associative-array notation:

.. code-block:: js

        x['thing'] = 1;

The associative-array notation requires the attribute key to be written
as either a quoted string, as in the example above, or a string variable,
like so:

.. code-block:: js

        let key = 'thing';
        x[key] = 1;

Either way, if the key's characters adhere to the rules of an identifier
token, it may still be de-referenced using dot notation.

.. code-block:: js

        x['thing'] = 1;
        // this works because 'thing' is a valid identifier name
        let y = x.thing;

Once a member has been declared and initialized to a certain type, it
may not change type again:

.. code-block:: js

        // THIS WILL NOT WORK!
        x.foo = 1;
        x.foo = "I'm a string";

You would need to explicitly delete the attribute ``foo``
(see `Built-in Methods for Dictionaries`_) and recreate it in order to
change its type.

Getting Dictionary Attributes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A dictionary may be de-referenced using the same kind of notation
used for setting attributes:

1. The dot notation, so long as a key adheres to the rules of
   an identifier token:

.. code-block:: js

        let y = x.thing;

2. Associative-array notation:

.. code-block:: js

        let y = x["thing"];

.. note::
        Example 1 is slightly faster than example 2, because array
        indexes are evaluated at runtime, even when they're expressed as
        literals, while identifers are pre-hashed during assembly.  But
        the difference is hardly noticeable.

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

.. [#]
        I do not extend that compliment to the unreadable and frankly
        ugly conventions of JavaScript programming style.
        Its name is ``i``, not ``ThisVariableIsAnIteratorInAForLoop``!

String
------

In EvilCandy a string is an object-like variable, which can be assigned
either from another string variable or from a string literal (see
`String Literal Tokens`_ above).

Unlike most high-level programming languages, strings
are pass-by-reference.  In the case:

.. code-block:: js

        let x = "Some string";
        let y = x;

any modification to ``y`` will change ``x``.  To get a duplicate, use
the builtin ``copy`` method:

.. code-block:: js

        let x = "Some string";
        let y = x.copy();
        // y and x now have handles to separate strings.

Function
--------

A function executes code and returns either a value or an empty variable.

In EvilCandy, **all functions are anonymous**.
The familiar JavaScript notation:

.. code-block:: js

        function foo() {...

will **not** work.  Instead declare a function by assigning it to a
variable:

.. code-block:: js

        let foo = function() {...

(More on this later when I get into the weeds of IIFE's, lambdas,
closures, and the like...)

The ``typeof`` Builtin Function
-------------------------------

Since things like ``x = y`` for ``x`` and ``y`` of different
types can cause syntax errors (which currently causes the program
to panic and exit() -PB 11/23), a variable can have its type checked
using the builtin ``typeof`` function.  This returns a value type
string.  Depending on the type, it will be one of the following:

**Table 5**

========== =======================
Type       ``typeof`` Return value
========== =======================
empty      "empty"
integer    "integer"
float      "float"
list       "list"
dictionary "dictionary"
string     "string"
function   "function"
========== =======================

Program Flow
============

In this section, *condition* refers to a boolean truth statement.
Since program flow requires this, let's start there...

.. warning::
        Braces around program flow statements are more strongly
        encouraged for EvilCandy, because unlike JavaScript or C,
        ``else`` is a clause to both ``if`` *and* ``for``.  If
        ``if`` is followed by ``for`` or vice-versa, and they are
        next followed by an ``else`` clause, the ``else`` will be
        a part of the latter statement, not the former, unless
        they are clearly delimited by braces.  Do not be fooled by
        the illusion of indentation, this isn't Python!

Condition Testing
-----------------

*condition* is evaluated in one of two ways:

1. Comparison between two objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

        *l-value* OPERATOR *r-value*

The relational operators are:

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

Non-numerical types (everything but ``integer`` and ``float``) should
only use ``==`` and ``!=``.  The other comparisons have undefined
results.

If the left and right values are the **same non-numerical
type**, ``==`` tests the following:

:string:        Do the contents match?
:list:          Do both variables point to the same handle?
:dictionary:    Do both variables point to the same handle?
:function:
        Do both variables point to the same *instantiation* handle?
        This is not the byte-code handle.  The executable byte code is
        common to the same class of function, but the instantiation
        handle points to metadata unique to one *instantiation* of a
        function, such as closures and optional-argument defaults.

If the left and right values are **different types** and at least one of
them is non-numerical, then ``==`` will return ``false``, and
``!=`` will return ``true``.

Comparing values of different types is useful when checking if a variable
is ``null``.  This special comparison is a little different than with
some other programming languages. The comparison ``x == null`` is
equivalent to ``typeof(x) == "empty"``.  See an example in
`A Caution About Using Optional Arguments`_.

EvilCandy does not support the ``===`` operator, which may be familiar
to JavaScript programmers.

2. Comparison of an object to some concept of "true"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are no native Boolean types for EvilCandy.  Keywords
``true`` and ``false`` are aliases for integers with values of
1 and 0, respectively; ``null`` evaluates to an empty variable.

The following conditions result in a variable
evaluating to *true*:

**Table 7**

========== ==================================================
Type       Condition
========== ==================================================
empty      false always
integer    true if != 0
float      true if ``fpclassify`` does not return ``FP_ZERO``
list       true always
dictionary true always
string     true if not the empty "" string
function   true always
========== ==================================================

``true`` and ``false`` are for convenient assignments and return values,
not for comparisons.  Never use ``if (myinteger == true)``; you shouldn't
use ``if (myinteger != false)`` either, if you are not certain
``myinteger`` is actually an integer.  Instead use just
``if (myinteger)``.

``if`` statement
----------------

An ``if`` statement follows the syntax::

        if (CONDITION)
                EXPRESSION

If *expression* is multi-line, it must be surrounded by braces.

If condition is true, *expression* will be executed, otherwise it will
be skipped.

``if`` ... ``else if`` ... ``else`` chain
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``if`` statement may continue likewise::

        if ( CONDITION_1 )
                EXPRESSION_1
        else if ( CONDITION_2 )
                EXPRESSION_2
        ...
        else
                EXPRESSION_N

This is analogous to the ``switch`` statement in C and JS (but which is
not supported here).

``do`` loop
-----------

The ``do`` loop takes the form::

        do
              EXPRESSION
        while ( CONDITION );

*expression* is executed the first time always, but successive executions
depend on *condition*.

``while`` loop
--------------

The ``while`` loop takes the form::

        while ( CONDITION )
                EXPRESSION

``for`` loop
------------

The ``for`` loop is similar to C.  The statement::

        for ( EXPR_1; CONDITION; EXPR_2 )
                STATEMENT

is equivalent to::

        EXPR_1
        while ( CONDITION ) {
                STATEMENT
                EXPR_2
        }

If you declare an iterator in *expr_1*, e.g.:

.. code-block:: js

        for (let i=0; i < n; i++) {...

then in this example ``i`` will be visible inside the loop but not
outside of it.  However, ``i`` must not be declared yet in the outer
scope or you will get a multiple-declaration error.

For those who prefer the Python-like version, use an object's
``foreach`` builtin method, described later.

``for`` - ``else`` combination
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

EvilCandy's ``for`` loop does have at least one similarity to Python:
the optional ``else`` statement after a ``for`` loop.  In the following
example (cribbed and adapted straight from an algorithm in the
python.org `documentation
<https://docs.python.org/3.12/tutorial/controlflow.html#for-statements>`_):

.. code-block:: js

        // Print prime numbers from 2 to 10
        for (let n = 2; n < 10; n++) {
                for (let x = 2; x < n; x++) {
                        if ((n % x) == 0)
                                break;
                } else {
                        print("{}".format(n));
                }
        }

the ``break`` statement escapes completely from the inner ``for`` loop;
but if the loop continues to iterate until failure of the ``x < n`` test,
the statement in the ``else`` block will be executed.

A word of caution: Even though the ``for`` loop in this example contains
only one statement, because it is an ``if`` statement, the braces are still
needed to prevent the ``else`` from being a part of the inner ``if``
statement.

Scope
=====

At any given moment, the following variables are visible, and when
they are referenced, the parser searches for them in this order:

1. All automatic variables at the current execution scope.  These
   are analogous to variables declared on a function's stack after
   the frame pointer.

#. All automatic variables in a parent function, _if_ the function
   is nested.  (This causes the creation of Closures_ in the child
   function, which have some peculiarities with the by-reference
   variables.

#. All automatic variables stored at the global scope. [#]_

#. All top-level elements of the currently running object ``this``.
   While not in a function (and sometimes while *in* a function,
   ``this`` is set to the global object ``__gbl__``.

#. All top-level children of the global object ``__gbl__``.

#. The global object ``__gbl__`` itself.

To avoid namespace confusion, you could type ``this.that`` instead
of ``that``, or ``__gbl__.thing`` instead of ``thing``, and you will
always get the right one.


.. [#]

    Both in implementation and philosophy, there's little difference
    between global-scope 'automatic' variables and child attributes of
    the global object.  Unlike function variables which are at known
    offsets from the frame pointer, global variables are stored in a
    runtime symbol table.  This is because the stack gets erased when
    leaving scope, but we want global-scope variables to remain for
    the duration of the program, assuming the script was a library
    import, not the main script.

    Theoretically, that makes global variables slower to get than
    function variables, but in testing I've been unable to see a very
    noticeable difference.

Variables may also be declared inside block statements, for even further
namespace reduction:

.. code-block:: js

        let thing = function(a, b) {
                if (b)
                        let x = b;

                // THIS WON'T WORK!!
                let a = x;  // x no longer exists
                ...

In this example, ``x`` is only visible inside the ``if`` statement.

One limitation of this is that only one automatic variable of a given
name may exist in a given scope, up to the function level, at any time.
That is, a local variable may take precedence over a global variable
of the same name, but a local variable in a block statement may not
override a variable in the containing function.

So while this will work:

.. code-block:: js

        // at the global level
        let a = 1;

        let thing = function(b) {
                if (b) {
                        // local a takes precedence over global a
                        let a = 2;
                        ...
                } else {
                        // local a left scope and may be re-declared
                        let a = 3;
                        ...

this will not:

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
                EXPRESSION

*expression* should have braces even if it's a single-line expression
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

        function(a, b, c="Hello", d=12.5)

Do not be misled by the "a=b" syntax of parameter definitions. These
are not "keyword arguments".  **The order in which arguments are passed
always matters.**  For that reason, it makes no sense to place the
optional arguments at the front of the argument list.

A Caution About Using Optional Arguments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Default values for arguments can be tricky when they are by-reference.
Consider the following constructor:

.. code-block:: js

        let MyNewObj = function(x = {}) {
                x.a = methodA;
                x.b = methodB;
                return x;
        }

It *seems* to allow a caller to choose whether to have a child inherit
the properties of MyNewObj by passing an argument, or to get a new
instantiation of the MyNewObj class altogether, by not passing an
argument.

The problem is that since the default literal ``{}`` is evaluated
only once, during the creation of the function, a handle to the
*same instantiation* will be returned to *all callers* who do not
pass an argument to MyNewObj.  The result is pure chaos.

The solution is to do this:

.. code-block:: js

        let MyNewObj = function(x = null) {
                // assign y <= either x or new instantiation
                let y = (function(x) {
                        if (x == null)
                                return {};
                        return x;
                })(x);
                y.a = methodA;
                y.b = methodB;
                return y;
        }

In this case, the literal ``{}`` is evaluated anew every time the
function is called, so a caller who does not pass an argument to
MyNewObj will always get a brand-new instantiation.

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

A function may not always return the same type.  For example, the builtin
function ``Io.open`` returns a file object upon success, and an error
string upon failure.  If this is the case (it ought to be documented,
right?), use the ``typeof`` builtin function to check it.

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
``__callable__`` may be entirely user-defined.

Lambda Functions
----------------

Normal function notation may be used for lambda functions, but if you
want to be cute and brief, special notation exists to make small
lambdas even smaller, most easily shown by example:

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
where the double backquote tokens (``````) provide syntactic sugar
for a very small function.  The general form is::

        `` ( ARGS ) EXPR ``

where *expr* is only an evaluation, with no assignments or ``return``
statement.  It does not end with a semicolon, and it is only a single
statement.  To use a multiline lambda, you must add back in the braces
and ``return`` statement...in which case you are better off using the
regular function notation; the `````` token is hard to spot over more
than one line.

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

To implicitly declare a closure, simply reference a variable in the
parent function's [#]_ scope, as in the ``multer`` example:

.. code-block:: js

        let multer = function(n) {
                return ``(x) x * n``;
        };

.. [#]
        You could also do this for grandparent, etc. but that isn't
        recommended.

Note, however, that if the function is not nested, then a closure
will not be created.  In the example:

.. code-block:: js

        // this is the global scope
        let n = 0;
        let foo = function() {
                bar(n);
        }

since ``n`` is a global variable, a closure will not be created.
and ``foo`` will not have unique access to its own copy of ``n``.

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

Closure Persistence Nuances
~~~~~~~~~~~~~~~~~~~~~~~~~~~

There's a reason I added the explicit closure declaration even though
I rarely (actually never) see it in other programming languages.

The two following examples are **not** equivalent:

Ex 1:

.. code-block:: js

        // nested inside of some function
        let hello = "Hello";
        ...
        let world = function(:a=hello.copy()) {
                foo(a + " world");
        }

Ex 2:

.. code-block:: js

        // nested inside of some function
        let hello = "Hello";
        ...
        let world = function() {
                let a = hello.copy();
                foo(a + " world");
        }

In the former example:
        A closure will be created for the return value of ``hello.copy()``.
        Even if ``hello`` changes, every call to ``world()`` will have
        predictable results.

In the latter example:
        The ``copy()`` method will be called every time ``world`` is
        called, because a closure will be created for ``hello`` only.
        So if the outer ``hello`` changes value even after ``world`` is
        created, then later calls to ``world()`` will have unpredictable
        results.  This is a problem particularly for pass-by-reference
        types like strings and dictionaries, not for integers and floats.

Importing Modules
=================

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

