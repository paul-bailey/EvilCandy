==================
EvilCandy tutorial
==================

:Author: Paul Bailey
:Date: November 2023

**Update** December 2023.  For now, probably until end of year, this
document is wildly out of date, since the program is changing rapidly.

Reference
=========

.. [CITKNR]
        Kernighan, Brian W., and Ritchie, Dennis M.,
        *The C Programming Language: Second Edition*
        (Upper Saddle River: Prentice Hall, 1988).
        My arrangement of the sections below was modelled after
        Appendix 2 in this book.

Hello World
===========

To print a "Hello world" program::

        print("Hello world");

The semicolon is needed; this isn't Python.  A function definition
is not needed.  Program flow begins at the first line of a file.

File Encoding Requirements
==========================

Source files must be either ASCII or UTF-8.  Do not include byte order
marks in the file.  With the exception of quoted strings (see String_)
and Comments_, all tokens, including whitespace, must be ASCII.

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

Be a good citizen.  Don't mix/match type 3. with 1. and 2.  I only
included it because I want to make the hashbang syntax permissible,
ie. having the first line be:

.. code-block:: bash

        #!/usr/bin/env evilcandy

so that the file will execute as standard input to ``evilcandy``.
:Note: (Using standard input for ``evilcandy`` is not supported yet.)
Don't be shy about using comments, they won't slow down execution
time, because the file will have already been pre-scanned and
converted into byte code by the time execution begins.

Tokens
------

``evilcandy`` classifies its tokens largely the same way as anyone else does:
whitespace, identifiers, keywords, constants like quoted strings or
numerical expressions, operators, and other separators and delimiters.
Whitespace is ignored, except wherever at least one whitespace
character is needed to delimit two tokens.  (In particular, always leave
whitespace in between identifiers, numerical expressions, and string
literals.)

Identifier Tokens
~~~~~~~~~~~~~~~~~

Identifiers must start with a letter or an underscore ``_``.
The remaining characters may be any combination of letters, numbers,
and underscores.

Keyword Tokens
~~~~~~~~~~~~~~

The following keywords are reserved for ``evilcandy``:

============ ========= ==========
Reserved Keywords
=================================
``function`` ``let``   ``return``
``this``     ``break`` ``if``
``while``    ``else``  ``do``
``for``      ``load``  ``const``
``private``
============ ========= ==========

Operators
~~~~~~~~~

These are syntactic sugar for what would be function calls.  ``evilcandy``
uses the following:

======== =======================
Binary Operators
--------------------------------
Operator Operation
======== =======================
``+``    add, concatenation [#]_
``-``    subtract
``*``    multiply
``/``    divide
``%``    modulo (remainder)
``&&``   logical AND
``||``   logical OR
``&``    bitwise AND [#]_
``|``    bitwise OR
``<<``   bitwise left shift
``>>``   bitwise right shift
``^``    bitwise XOR
======== =======================

======== =======================
Unary Operators before var
--------------------------------
Operator Operation
======== =======================
``!``    logical NOT
``~``    bitwise NOT
``-``    negate (multiply by -1)
======== =======================

======== =====================
Unary Operators after var
------------------------------
Operator Operation
======== =====================
``++``   Increment by one [#]_
``--``   Decrement by one
======== =====================

.. [#] For string data types, the plus operator concatenates the two strings.

.. [#] Bitwise operators are only valid when operating on integers.

.. [#] The "pre-" and "post-" of preincrement and postincrement are undefined for ``evilcandy``.

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

Value limitations
~~~~~~~~~~~~~~~~~

*value* here means "thing that can be evaluated and stored in a single
variable", examples:

* Combination of literals and identifiers::

        (1 + 2) / x

* Function definition::

        function() { do_something(); }

* List definition::

        [ "this", "is", "a", "list" ]

* dict definition::

        { this: "is", a: "dictionary" }

Only limited versions of these may *begin* an expression, namely cases
4-6 in the table above: function calls with ignored return values (#4),
expressions wrapped in parentheses (#5), and ignored empty identifiers
(#6).  For a full range of *value* to be permitted, it has to be on the
right-hand side of an assignment operator, as in cases 2 and 3, or
within the parentheses of a program-flow statement, as in cases 7-11.

The parentheses exception makes IIFE's possible. Some Javascript
implementations might allow something like::

        function(arg) { thing(); }(my_arg);     // :(

but I do not, because no good programmer writes that way unless they're
trying to hide something.  Instead they write::

        (function(arg) { thing(); })(my_arg);   // :)

Conventions make the latter case clearer that you're calling the
anonymous function rather than just declaring it.  I merely enforce
the better choice, at the cost of some complexity in my parser.

Identifier Limitations
~~~~~~~~~~~~~~~~~~~~~~

In the declaration cases (#1 and #3 above), *identifier* must be simple;
that is, you can type::

        let x = a;      // permissible

but not::

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
-----------------------------

All automatic variables must be declared with the ``let`` keyword::

        let x;

Types of Variables
------------------

The above example declared ``x`` and set it to be an *empty* variable.
``evilcandy`` is not dynamically typed; the only variable that may be changed
to a new type is an *empty* variable.  The other types are:

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

There are no "pointers" in ``evilcandy``.  Instead we use the abstract
concept of a "handle" when discussing pass-by-reference variables.
Handles' *contents* may be modified, but the handles themselves
may not; they may be only assigned.  For example, given a function
handle assignment::

        let foo = function() { bar(); }

then the following will result in errors::

        foo++;

::

        foo = foo + bar;

The only time variables may be assigned using something of a different
type is when the l-value and r-value are both integers or floats.
For example::

        let x = 1;      // integer
        let y = 1.4;    // float
        x = x + y;      // x is still integer, equals 2

is valid.  Instead of adding ``y`` to ``x`` this will add an
intermediate variable that is the value of ``y`` cast into the
type of ``x``.


Integers
~~~~~~~~

These may be expressed as digital, octal, or hexadecimal using the
C convention, eg. 12 can be expressed as ``12``, ``014``, or ``0xC``.
Currently ``evilcandy`` does **not** support numerical suffixes like ``12ul``.

All integers are stored as 64-bit signed values.  In ``evilcandy`` these
are pass-by-value always.

Floats
~~~~~~

These may be expressed as per the C convention, except that suffixes
like the ``f`` of ``0f`` are not allowed.  The number 12.0 may be
expressed, for example, as ``12.0``, ``12.``, ``12e1``, ``1.2e2``,
and so on.

All floats are stored as IEEE-754 double-precision floating point
numbers.  Floats are pass-by value always.

Lists
~~~~~


:TODO:
        As of 11/2022 I'm working on an object lib for more
        efficient data arrays

Lists are rudimentary forms of numerical arrays.  These are **not**
efficient at managing large amounts of data.
Lists are basically more restrictive versions of dictionaries.
There are two main differences:

1. Lists' members must all be the same type.  (There are quirks,
   however.  If a list's members are themselves lists, they need
   not be the same length or contain the same type as their sibling
   members; same goes for lists of dictionaries.)
2. Lists do not have associative indexes; ie may only be de-referenced
   numerically.

Set an existing member of a list using the square-bracket notation::

        x[3] = 2;

De-reference lists with the same kind of notation::

        y = x[3];

In the above example, ``3`` may be a variable, but the variable type
**must** be an integer.  It may not be floating point or string.

Declare a list with multiple entries with commas between them,
like so::

        let x = [1, 4, 2];

Do **not** place a comma after the last variable.

Lists are pass-by reference.  In the example::

        let x = [1, 3, 4];
        let y = x;
        y[0] = 0;

The last line will change the contents of ``x`` as well as ``y``.

:TODO:
        I'm working on a .copy callback for something like let y=x.copy;

:Note:
        In the source code the prefix ``array_`` is used in a lot of
        the functions.  This is unfortunate, because I intend "array" to
        mean a certain type of built-in library object that deals better
        with large quantities of data.  But "list" has a different
        meaning in C, and ``evilcandy`` contains some functionality dedicated
        to linked-list management, and I didn't want to confuse the two
        groups of functions.


Dictionaries
~~~~~~~~~~~~

A dictionary is referred to as an "object" in JavaScript (as well as,
unfortunately, my source code).  Here I choose more appropriate language,
since technically all of these data types have some object-like
characteristics.

A dictionary is an associative array--an array where you may de-reference
it by enumeration instead of by index number.  Unlike lists, its contents
do not need to all be the same type.

A dictionary may be declared in an initializer, using syntax very similar
to JavaScript::

        let x = {
                thing: 1,
                foo: function () { bar(); }
                // note, no comma after above last element
        };

or by assigning undeclared members using the dot notation::

        // make sure x is defined as a dictionary
        let x = {};

        // create new element 'thing' and assign it a value
        x.thing = 1;

        // ditto, but 'foo'
        x.foo = function() { bar(); }

Once a member has been declared and initialized to a certain type, it
may not change type again::

        // THIS WILL NOT WORK!
        x.foo = 1;
        x.foo = "I'm a string";

A dictionary may be de-referenced in one of two ways:

1. The dot notation, so long as a key adheres to the rules of
   an identifier token::

        let y = x.thing;

2. Associative-array notation::

        let y = x["thing"];

3. Numerical-array notation::

        let y = x[2];

Example 3 is not recommended, nor will it be noticeably faster than
example 1.

:TODO:
        As of 11/2023, between examples 1 and 2, 1 is slightly quicker,
        because of how attribute names are hashed during assembly and
        because of how array indices are evaluated.

You may assign an attribute to another variable::

        let x = y.someattribute;

In this example, if ``someattribute`` is a string, list, or object, then
any change made to ``x`` will affect ``y.someattribute``.

Dictionary constructor statements may use quotes for their keys.
This could be useful if you want keys that have non-ASCII characters
or characters that violate the rules of identifier tokens::

        let mydict = {
                a: 'The letter a',
                '✓': 'checkmark'
        };

Note, however, keys like this cannot be accessed with dot notation;
they must be accessed using the associative-array notation::

        // an error will be thrown...
        let checkmark = mydict.✓;

        // ...but this is okay
        let checkmark = mydict['✓'];

All dictionaries are pass-by reference.

String
~~~~~~

In ``evilcandy`` a string is an object-like variable, whose literal expression
is surrounded by either single or double quotes.  The usual backslash
escapes are recognized (**although** I do not yet support Unicode),
so you can escape an internal quote with ``\"``.  Or if your string
literal does not have both kinds of quotes in it, you could simply escape
it by using the other kind of quote.  The following two strings evaluate
the same way::

        "This is a \"string\""
        'This is a "string"'

Strings behave a litter weird around line endings.  The following
examples will all parse identically (save for how the line number
is saved for error dumps):

Ex 1::

        "A two-line
        string"

Ex 2::

        "A two-line\nstring"

Ex 3::

        "A two-line\n\
        string"

Ex 4::

        "A \
        two-line
        string"

Examples 2 and 3 are the clearest, but you could be even clearer
(at the expense of some functional overhead) with::

        [ "A two-line",
          "string" ].join("\n")

This becomes especially useful for long paragraphs and such.

String literals may contain Unicode, either in UTF-8, or following
familiar backslash conventions.  The following are all valid ways
to express the Greek letter β:

================== ================
Direct UTF-8       ``"β"``
lowercase u escape ``"\u03b2"``
Uppercase U escape ``"\U000003b2"``
Hexadecimal escape ``"\xce\xb2"``
Octal escape       ``"\316\262"``
================== ================

For the uppercase U escape, only Unicode values up to 10FFFF
hexadecimal (1 114 111 decimal) are supported.

.. important::
        Unlike most high-level programming languages, strings
        are pass-by-reference.  In the case::

                let x = "Some string";
                let y = x;

        any modification to ``y`` will change ``x``.  To get a duplicate, use
        the builtin ``copy`` method::

                let x = "Some string";
                let y = x.copy();
                // y and x now have handles to separate strings.

Function
~~~~~~~~

A function executes code and returns either a value or an empty variable.

In ``evilcandy``, **all functions are anonymous**.
The familiar JavaScript notation::

        function foo() {...

will **not** work.  Instead declare a function by assigning it to a
variable::

        let foo = function() {...

(More on this later when I get into the weeds of IIFE's, lambdas,
closures, and the like...)

The ``typeof`` Builtin Function
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Since things like ``x = y`` for ``x`` and ``y`` of different
types can cause syntax errors (which currently causes the program
to panic and exit() -PB 11/22), a variable can have its type checked
using the builtin ``typeof`` function.  This returns a value type
string.  Depending on the type, it will be one of the following:

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

Condition Testing
-----------------

*condition* is evaluated in one of two ways:

1. Comparison between two objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

        *l-value* *relational-operator* *r-value*

The following relational operators are:

======== ========================
Operator Meaning
======== ========================
==       Equals [#]_
<=       Less than or equal to
>=       Greater than or equal to
!=       Not equal to
<        Less than
>        Greater than
======== ========================

Do not compare values of different types.  Do not compare
functions at all.

.. [#]
    In the case of strings, the test is whether or not their contents
    match, ie. the ``==`` operator between two strings is the opposite
    result of C's ``strcmp`` function.

:TODO:
        comparison of objects are not supported yet, need
        to add ability to customize operators for objects.

2. Comparison of an object to some concept of "true"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are no native Boolean types for ``evilcandy``.  Keywords
``true`` and ``false`` are aliases for integers with values of
1 and 0, respectively; ``null`` evaluates to an empty variable.

The following conditions result in a variable by itself
evaluating to *true*:

========== ===============================
Type       Condition
========== ===============================
empty      false always
integer    != 0
float      != 0.0 [#]_
list       true always
dictionary true always
string     true if not the empty "" string
function   true always
========== ===============================

.. [#]
    Or to be precise, true if ``fpclassify`` does not return ``FP_ZERO``

``if`` Statement
~~~~~~~~~~~~~~~~

An ``if`` statement follows the syntax::

        if (CONDITION)
                EXPRESSION

If *expression* is multi-line, it must be surrounded by braces.

If condition is true, *expression* will be executed, otherwise it will
be skipped.

``if`` ... ``else if`` ... ``else`` block
-----------------------------------------

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

If you declare an iterator in *expr_1*, e.g.::

        for (let i=0; i < n; i++) {...

then in this example ``i`` will be visible inside the loop but not
outside of it.  However, ``i`` must not be declared yet in the outer
scope or you will get a multiple-declaration error.

For those who prefer the Python-like version, use an object's
``foreach`` builtin method, described later.

One similarity to Python does exist, however: the optional ``else``
statement after a ``for`` loop.  In the following example (cribbed
straight from an algorithm in the `Python.org
<https://docs.python.org/3.12/tutorial/controlflow.html#for-statements>`_
documentation:

.. code-block:: js

        // Print prime number from 2 to 10
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
namespace reduction::

        let thing = function(a, b) {
                if (b)
                        let x = b;

                // THIS WON'T WORK!!
                let a = x;  // x no longer exists
                ...

In this example, ``x`` is only visible inside the ``if`` statement.

One limitation of this is that only one automatic variable of a given
name may exist in a given scope at any time.  Since all of a
function's variables outside a block statement are still in scope,
a variable newly declared inside the block must not have already been
declared::

        let thing = function(a, b) {
                if (b) {
                        // THIS WON'T WORK
                        let a = b; // a already exists
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
used to identify the caller's parameters, e.g.::

        function(x, y, z)

An *optional argument* may be designated as::

        ARG = DEFAULT

where *default* is an expression that evaluates to a default value for
the argument should one not be provided by the caller, e.g.::

        function(a, b, c="Hello", d=12.5)

Do not be misled by the "a=b" syntax of parameter definitions.  **The
order in which arguments are passed always matters.**  For that reason,
it makes no sense to place the optional arguments at the front of the
argument list.

Function Call Syntax
--------------------

The number of functions provided must be at least as many as the number
of arguments defined in the function definition up to the last mandatory
argument defined.  More arguments may be provided than are defined,
in which case they'll be ignored and the caller would have wasted compute
cycles...

The arguments are not type-checked.  If the wrong type was provided to
the function, that will be discovered soon enough while the function
itself is executing.

A function may not always return the same type.  For example, the builtin
function Io.open returns a file object upon success, and an error string
upon failure.  If this is the case (it ought to be documented, right?),
use the ``typeof`` builtin function to check it.

Callable Dictionaries
---------------------

A dictionary can be called like a function if it has an attribute
named ``__callable__``

For example, given the dictionary::

        let mydict = {
                a: 1,
                b: 3,
                __callable__: function () { foo(this.a, this.b); }
        };

then a call to ``mydict()`` is equivalent to calling
``mydict.__callable__()``.  The number and type of arguments for
``__callable__`` are entirely user-defined.

Lambda Functions
----------------

Normal function notation may be used for lambda functions, but if you
want to be cute and brief, special notation exists for lambdas in
EvilCandy, best shown in the example::

    let multer = function(n) {
        return ``(x) x * n``;
    };

This is equivalent to::

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
statement.  To use a multiline lambda, braces are required and
``return`` is required to return a value... in which case you might
as well have used regular function notation after all.  (The ``````
token is hard to spot over more than one line.)

Lambdas are useful in the way they create new functions, for example::

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
its input to a value determined at the time of its instantiation.

Closures
--------

In the previous section `Lambda Functions`_, the lambda function used
a variable ``n`` that was in its parent function scope.  This variable
will now persist until the return value (``doubler`` or ``tripler``
in the example) are deleted.  This is known as a *closure*.  Because
it is evaluated at the time of the function's creation, it can be
unique for each instantiation (note that ``doubler`` and ``tripler``
maintain their own values of ``n``).

Implicit Closure Declaration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To implicitly declare a closure, simply reference a variable in the
parent function's [#]_ scope, as in the ``multer`` example::

        let multer = function(n) {
                return ``(x) x * n``;
        };

.. [#]
        You could also do this for grandparent, etc. but that isn't
        recommended.

Note, however, that if the function is not nested, then a closure
will not be created.  In the example::

        # this is the global scope
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

To use the ``multer`` example again::

        let multer = function(n) {
                return ``(x, :a=n) x * a``;
        };

Here, the ``a`` of ``:a=n`` is the name of the parameter (which could,
incidentally, also be called ``n`` for consistency), and ``n`` is the
value to set it to.

This is **not** an argument to the function!  Unlike with default
arguments, this value cannot be overridden by a caller's own argument,
nor does it shift the placement of the actual arguments (though for
readability, it's still best to place their declarations at the end).

Closure Persistence Nuances
~~~~~~~~~~~~~~~~~~~~~~~~~~~

There's a reason I added the explicit closure declaration even though
I rarely (actually never) see it in other programming languages.

The two following examples are **not** equivalent:

Ex 1::

        // nested inside of some function
        let hello = "Hello";
        ...
        let world = function(:a=hello.copy()) {
                bar(a + " world");
        }

Ex 2::

        let hello = "Hello";
        ...
        let world = function() {
                let a = hello.copy();
                bar(a + " world");
        }

In the former example:
        A closure will be created for the return value of ``hello.copy()``.
        Even if ``hello`` changes, every call to ``world`` will have
        predictable results.

In the latter example:
        A closure will be created for ``hello`` only.  So if the ``hello``
        changes value even after ``world`` is created, then later calls
        to ``world()`` will have undesired results.  This is not a problem
        for floats and integers, which are pass-by-value.  This is mainly
        an issue for strings.

Built-in Methods
================

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

:TODO: The rest of this documentation

.. : vim: set syntax=rst :

