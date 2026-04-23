Program Flow
============

Of course programming is more than crunching numbers in sequence.
There are also decision points, branching, and looping.
Take for instance this code to calculate π using the Leibniz
algorithm

.. math::

        \pi = 4 \cdot \sum_{k=0}^{\infty} \frac{(-1)^k}{2k + 1}

(albeit done inaccurately here due to the low number of iterations):

.. code-block:: evc-console

   evc> let result = 0;
   evc> let num = 4.0;
   evc> let den = 1;
   evc> let i = 0;
   evc> while i < 200 {
    ...    result += num / den;
    ...    den += 2;
    ...    result -= num / den;
    ...    den += 2;
    ...    i++;  // "++" means "increment by one"
    ... }
   evc> print(result);
   3.1390926574960143

The portion in parentheses, ``i < 20``, is an *expression*,
which in this instance evaluates to either true or false.
The block statement between the curly braces ``{`` ... ``}``
is repeated until ``i`` increments to 200.
This block statement is commonly referred to
as the *body* of the "while" statement.

"if" statements
---------------

EvilCandy uses ``if`` statements more or less the same way
as JavaScript or C:
They take one of two forms:

| ``if (`` *expr* ``)`` *stmt*

or

| ``if (`` *expr* ``)`` *stmt1* ``else`` *stmt2*

In the former case, if *expr* is true, execute the following
statement, otherwise skip it.
In the latter case, execute *stmt1* if true
or *stmt2* if false.

If statements may be chained the usual way::

  if (x == 1)
        action_1();
  else if (x == 2)
        action_2();
  else if (x == 3)
        action_3();
  else
        action_4();

.. note::

   Version 0.1.0 does not support ``switch`` or its many aliases, such
   as ``match`` in Python or ``case`` in bash.  Adding it to a later
   version is on the to-do list.

Evaluating truth expressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Within the parentheses of the above ``if`` statement
(or the ``while`` statement, for that matter),
*expr* may be a comparison, e.g. ``a == b``, ``a <= b``,
and so on, or it may just be an object which can be
evaluated as true or false.
Any nonzero [#]_ number is true, while zero is false.
Sequential objects like lists or strings are false
if their size is zero,
true otherwise, regardless of their contents.
The ``null`` object is always false.

There is no built-in boolean data type.
The keywords ``true`` and ``false`` are simply aliases
for 1 and 0, respectively.
Do not make the mistake of evaluating ``x == true``.
Instead just let ``x`` be its own truth statement.
If your program for some reason needs a true statement
to be exactly one and a false statement to be exactly zero,
you could convert it with the logical-not ``!`` operator.
Two in a row will convert a true value into the integer 1
and a false value into the integer 0.  This is known
as "type coercion":

.. code-block:: evc-console

   evc> !!'A';  // string length is one, so it's true
   1
   evc> !!'';   // empty string is false
   0
   evc> true;
   1
   evc> false;
   0

.. note::

   A string is *true* if its length is greater than zero,
   even if it contains only embedded nulchars:

   .. code-block:: evc-console

      evc> !!'\0\0\0';
      1

Compound truth expressions
~~~~~~~~~~~~~~~~~~~~~~~~~~

Truth expressions can be combined using the boolean binary operators
``and`` and ``or``, and the unary operator ``not``.  The exclamation
mark ``!`` is also available and serves the same purpose as ``not``.

Evaluation may skip a step if a compound boolean expression produces
a known result before the end of the expression:

* In the case of ``A and B``, if ``A`` evaluates to false, then ``B``
  will not be evaluated.

* In the case of ``A or B``, if ``A`` evaluates to true, then ``B``
  will not be evaluate.

So the ordering of these statements is something to keep in mind when
speed is a concern.

:TODO: Either discuss ``&&`` and ``||`` or remove support for them.

The ``in`` operator
~~~~~~~~~~~~~~~~~~~

Except in ``for`` loops (which we'll get to in a moment), ``in`` is a
binary operator which will create a truth expression out of two objects:
``A in B`` means "B contains A as one of its contents." If B is not an
iterable type, then the result will be false.  Nor do class
attributes count as "contents" in this case.

Note that because ``not`` is a unary opertaor and ``in`` is a binary
operator, the order of operators may run counterintuitive to natural
English.  Use ``not (A in B)`` instead of ``A not in B``.  The latter
will produce a syntax error.

"do" statements
---------------

A ``do`` statement takes the form

        | ``do`` *stmt* ``while`` *expr*

*stmt* is always executed the first time.
It is then repeated so long as *expr* remains true.
``do {`` *stmt;* ``} while false`` is equivalent
to ``{`` *stmt* ``}``.
Both cases enable local variables to be declared within the braces
without cluttering up the outer namespace.
(More on variable scope later).

"break" and "continue" statements
---------------------------------

In a control loop, ``break`` quits the loop early.
``continue`` skips the remainder of the current iteration
and moves on to the next iteration::

  // determine if 127 is a prime number
  let n = 127;
  let i = 2;
  while i < n {
        // percent operated on integers means "modulo"
        if ((n % i) == 0)
                break;
        i++;
  }
  if (i == n)
        print("prime");
  else
        print("not prime");

In this example, the loop quits early if a number ``i`` is found
such that n / i will leave no remainder (n modulo i).

Here is a (rather weird) alternative to the ``while`` loop,
using ``continue``::

   while i < n {
        if ((n % i) != 0) {
                i++;
                continue;
        }
        break;
   }

As long as ``i`` is not an integer divisor of ``n``,
the ``continue`` will prevent the loop from breaking early.

"for" Statements
----------------

EvilCandy's ``for`` statements resemble Python's rather than
JavaScript's verbose C-like ``for`` statements.
The Python-style ``for`` loop is inherently faster
than the traditional ``for`` statement,
due to some under-the-hood implementation details which avoid
the creation and destruction of objects during the iteration step.
They are also faster than JavaScript's
sequential objects' ``.foreach()`` method,
since the latter requires the overhead of a frame push
for every iteration of the loop.

Since any traditional ``for`` loop can be implemented
by a ``while`` loop...

    | for (A; B; C)
    |     *stmt*

is functionally equivalent to

    | A;
    | while (B) {
    |     *stmt*
    |     C
    | }

...there is no need for a C-like ``for`` statement.

In EvilCandy, the ``for`` statement takes the form:

        | ``for`` *identifier* ``in`` *sequence*
        |         *stmt*

*sequence* may be any iterable object, like a list, or even
a string (in which case *identifier* will be set to a character
in *sequence* for every iteration).

For *sequence* to be an arbitrary iterator,
acting as the ``i`` in the Leibniz example above,
EvilCandy has a ``range`` object, which is nearly identical to Python's.
It is created with the ``range`` function which takes one to three arguments:

        | ``range(start_value, stop_value, step_size)``
        | ``range(start_value, stop_value)``
        | ``range(stop_value)``

The defaults for ``start_value`` and ``step_size`` are zero and one,
respectively.

So to repeat the Leibniz example above using a for loop,
it may look like this:

.. code-block:: evc-console

   evc> let result = 0;
   evc> let num = 4.0;
   evc> let den = 1;
   evc> for i in range(200) {
    ...     result += num / den;
    ...     den += 2;
    ...     result -= num / den;
    ...     den += 2;
    ... };
   evc> print(result);
   3.1390926574960143

The extra semi-colon at the end of the ``for`` loop
is only needed in interactive mode
while at the top level of program flow.
This is because EvilCandy's ``for`` loop
also uses an optional else-like clause ``nobreak``
(see the next example below),
so the interpreter will hold off execution until it gets
the next token;
if it's not ``nobreak``, then the interpreter
will assume it to be the start of the next statement
and therefore execute the ``for`` loop.
To force execution before typing the next statement
(``print`` in this example),
the empty statement ``;`` will do the trick.
If the interpreter is parsing a script,
or if it is in interactive mode
but nested at least one layer deep in program flow,
the semicolon is not needed.

The following example is a little trivial (why calculate it if you
already know the answer?),
but it demonstrates the usefulness of ``nobreak``
in the ``for`` loop:

.. code-block:: evc-console

   evc> let result = 0;
   evc> let num = 4.0;
   evc> let den = 1;
   evc> for i in range(200) {
    ...     result += num / den;
    ...     den += 2;
    ...     result -= num / den;
    ...     den += 2;
    ...     // from the 'while' example above you can see
    ...     // that this 'break' will never occur.
    ...     if (abs(result - 3.14) < 0.0001)
    ...         break;
    ... } nobreak {
    ...     print('not enough iterations');
    ... }
   not enough iterations

That is, if the program runs through the entire sequence
(``range(200)`` in this example)
without stopping the iteration early with ``break``,
then the body of ``nobreak`` will be executed.

Functions
---------

Function Basics
~~~~~~~~~~~~~~~

Unless we are discussing some egg-headed CS concept of a function,
the most succinct definition of a function is in K&R:
"A function provides a convenient way to encapsulate some computation,
which can then be used without worrying about its implementation" [#]_ [#]_.

To begin with, we don't want to repeat typing the Leibniz algorithm
every time we want to execute it.
So we put it into a function as follows:

.. code-block:: evc-console

   evc> leibniz = function(n) {
    ...    let result = 0;
    ...    let num = 4.0;
    ...    let den = 1;
    ...    for i in range(n) {
    ...            result += num / den;
    ...            den += 2;
    ...            result -= num / den;
    ...            den += 2;
    ...    }
    ...    return result;
    ... };

The semicolon at the end of the function definition
is needed in this case because,
if you look closely, you will see that it takes the form::

   let name = expression;

This is known as an "anonymous" function.  Treating a function definition
as just another literal expression like this can be very useful in cases
where you want to keep a definition close to its usage, such as in an
argument list.  The non-anonymous way to express a function is::

   function leibniz(n) {
        .....
   }

...in which case the semicolon is not needed at the end.  When declaring
a function in this way, the symbol name is *always* a local variable.
To make a function name be global, you have one of two choices::

   // choice 1: anonymous function assigned to global variable
   global leibniz = function(n) {
        ...
   };

   // choice 2: assign existing function to a new global variable
   function leibniz(n) {
        ...
   }
   global leibniz = leibniz

For this example, I've chosen an argument ``n`` to determine
how many iterations of the algorithm to use.  Clearly the
more iterations the more accurate the result:

.. code-block:: evc-console

   evc> leibniz(10);
   3.0916238066678399
   evc> leibniz(10000);
   3.1415426535898248
   evc> leibniz(1000000);
   3.1415921535897242

Unlike JavaScript, the number of arguments to a function
is strictly enforced:

.. code-block:: evc-console

   evc> leibniz(100, 1);
   [EvilCandy] ArgumentError Expected at most 1 args but got 2
   evc> leibniz();
   [EvilCandy] ArgumentError Expected at least 1 args but got 0

Variadic functions—functions with a variable number of arguments—can
use a star notation:

.. code-block:: evc-console

   evc> function foo(arg1, arg2, *var_args) {

In this example, arg1, and arg2 are mandatory arguments.  The third
argument, ``*var_args``, is a list containing any non-keyword arguments
exceeding the number of mandatory positional arguments.  This list may
be zero if (in this case) only two arguments were passed to the function.
A function header may not contain any more positional arguments after the
starred argument.

Functions can accept keyword arguments with double-star notation.  When
used, it must always be the last argument in the header.  (We will dicuss
keyword arguments in greater detail later.):

.. code-block:: evc-console

   evc> function foo(*args, **kwargs) {
    ...    print('Your positional args are:', args);
    ...    print('Your keyword args are:', kwargs);
    ... };

When invoking a function using keyword arguments, place the
keyword arguments at the end of the argument list, and use
the format *keyword=value*:

.. code-block:: evc-console

   evc> foo('line', 1, kw_a='a', kw_b='b');
   Your positional args are: ['line', 1]
   Your keyword args are: {'kw_a': 'a', 'kw_b': 'b'}

When invoking a function, a sequential object can be unpacked into the
argument list using the star operator:

.. code-block:: evc-console

   evc> let x = [1,2,3];
   evc> foo(*x);
   Your positional args are: [1, 2, 3]
   Your keyword args are: {}
   evc> foo(x);
   Your positional args are: [[1, 2, 3]]
   Your keyword args are: {}

In the above first case, there are three arguments, 1, 2, and 3,
while in the second case, there is one argument, a list containing
1, 2, and 3.

.. note::

   Version 0.1.0 does not support double-star unpacking during a function
   call, e.g. ``foo(**x);``.  Implementing that is on the to-do list.
   Until then, use the *keyword=value* format.

.. note::

   Version 0.1.0 does not support Python-like default arguments,
   e.g. function protos like::

     function foo(a, b=1, c=2) {...

   Implementing that is on the to-do list.
   Until then, use a proto like::

     function foo(*args) {...

   and set defaults or user-provided values based upon
   the length of ``args``.

All functions return a result.
If program flow reaches an end
without encountering the ``return`` statement,
the function will return ``null``:

.. code-block:: evc-console

   evc> let do_nothing = function() {;};
   evc> do_nothing();
   evc> print(do_nothing());
   null

Nested Functions and Closures
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Functions can be nested arbitrarily deep.  The obvious use for this
is for helper functions which do not need to be repeated anywhere.
In this case, it should be noted that function-call overhead is not
trivial, so if it takes part in a highly iterative or algorithmically
intense part of the program, it may be better not to nest a function.

But there are other uses, and here we'll talk about *lambdas* and
*closures*.  To keep things simple, I will just use "lambda" as a word
synonymous with "function handle"; a function can return a handle to
another function.  A closure is a function which encapsulates
information outside of its scope and remembers it each time it is called.

A function which returns a closure as a lambda, then, is very powerful.
Take, for instance, the following example:

.. code-block:: evc-console

   evc> function multer(n) {
    ...    return function(x) {
    ...            return x * n;
    ...    };
    ... };
   evc> let doubler = multer(2);
   evc> let tripler = multer(3);

In this example, ``multer(n)`` returns a lambda which multiplies an
argument ``x`` by the value specified by ``n``.
But ``n`` is in the parent function's scope, not the lambda's scope;
when ``multer`` returns, its value
``n`` will go out of scope.
So that ``n`` will still be available to the lambda later,
it will be stored with the lambda's own instantiation data.
Closures like these are created every time a function accesses
a variable in a higher-up scope.

``doubler`` and ``tripler`` have been
assigned different instantiations of the lambda returned by ``multer``.
Now let's see what they do:

.. code-block:: evc-console

   evc> doubler(3);
   6
   evc> tripler(3);
   9
   evc> doubler('a');
   'aa'
   evc> tripler('a');
   'aaa'

``doubler`` in this example will always multiply its input by 2
and ``tripler`` will always multiply its input by 3.

Although it's more correct to say that the lambda instantiated by and
returned from ``multer`` is a closure, it's more common to say that
``n``, the argument passed to ``multer`` and remembered by the lambda,
is the closure.  Going forward, I will use the latter, more casual,
definition of "closure".

A few things to also note about closures:

1. In EvilCandy, every script is thought of as a function at the top
   level, so any function within it that accesses script-scope variables
   will create a closure.

2. Accessing global variables will not result in the creation of a
   closure, since every scope has access to every global variable.

3. In interactive mode, local variables whose scope is at the top
   level (IE not declared inside a program flow statement, a block
   statement, or a function) can be accessed from anywhere in the
   interactive namespace (that is, anywhere you type, but not
   by any script you import), so closures are not created out of
   these variables.

4. It's generally bad practice for a wrapper function to modify a
   variable after passing it as a closure.  Consider the following:

   .. code-block:: evc-console
      :class: example-bad

      evc> function multer(n) {
       ...    let ret = function(x) {
       ...            return x * n;
       ...    };
       ...    n++;      // don't do this!
       ...    return ret;
       ... }
      evc> let doubler = multer(2);
      evc> doubler(1);  // yowza!
      3

   One eccentric exception to this rule: If you don't like class
   notation, closures are an excellent way to make dictionaries
   behave like class instantiations; a set of functions could
   manipulate common data as if it were private instantiation data.
   More on classes, as well as these kinds of pseudo-classes, in
   another part of this tutorial.

IIFEs
~~~~~

Because functions may be anonymous, and because EvilCandy treats a
function's definition like any other literal expression,
immediately-invoked function expressions (IIFEs, pronounced "iffies")
are possible.  A trivial example of an IFFE is:

.. code-block:: evc-console

   evc> function(x) { return x + 1; }(5);
   6

Common practice is to wrap the function expression in parentheses, as a
conventional way to say "this is an evaluation statement":

.. code-block:: evc-console

   evc> (function(x) { return x + 1; })(5);
   6

.. admonition:: rant
   :class: rant

   Frankly, IIFEs are an accident of how the language works, not a
   deliberate feature.
   Functions' and subroutines' most important utility is the
   prevention of repeated code.
   IIFEs do not serve this purpose;
   they do keep the namespace clean,
   but in EvilCandy *that is often not necessary*.
   Function calls in EvilCandy (and most every other scripting language)
   are non-trivial.  They aren't like C, where the stack overhead of a
   call is negligible [#]_
   and the only performance hit is due to a reduced locality of reference.
   IIFEs—as well as (while I'm ranting) the other hated JavaScript practice
   of method-chaining—were more useful back in the day, when computers were
   made of wood and sails and TCP was transported by carrier pigeons, so it
   mattered how big a JavaScript file got.
   But even though neither are needed anymore, they just won't die out.

   Given the two choices::

      // choice one:
      let x = (function() {
           // ...code that calculates a value...
           return calculated_value;
      })();

   ::

      // choice two:
      let x;
      // ...code that calculates a value...
      x = calculated_value;

   The latter is faster, due to the lack of a frame push, while the
   former is *cuter*.  It may not matter in already-slow parts of the code,
   where not much is going on algorithmically,
   but JavaScript users in particular are so addicted to this sort of thing,
   especially when using JavaScript's ``=>`` lambda notation,
   that they use it even in highly-iterative loops.

   If you care about the namespace clutter caused by adding a bunch
   of temporary variables to calculate a result,
   you could still use the second example, rewritten as::

      // choice three:
      let x;
      {
           // ...calculate a value...
           x = calculated_value;
      }

   This lacks the overhead of a function call [#]_,
   and it limits the scope of the temporary
   variables to within the braces.
   Just be sure that ``x`` itself is declared outside the braces.
   More on that when discussing variable scope.

   **tl;dr** IIFEs are considered...well, not *harmful*,
   but they are rarely useful enough to justify themselves.

Lambda Notation
~~~~~~~~~~~~~~~

The lambda returned by ``multer`` in the above example is trivially small.
An alternative way to express it is as follows:

.. code-block:: evc-console

   evc> let multer = function(n) {
    ...      return (x) => x * n;
    ... };

This is EvilCandy's *lambda notation* [#]_, although we are being very
loose with the word "lambda", which here means "a way to make
a short function look shorter."
The general form is:

| When the return value can be calculated in a single expression:
|        ``(`` *args* ``) =>`` *expr*

| When the return value requires a block expression:
|        ``(`` *args* ``) => {`` *stmts* ``}``

In the first example, *expr* is not a full statement;
it is just an evaluable expression.
In the second example,
the benefit of lambda notation—brevity—is lost,
so it's best to use normal function notation.

Unlike in JavaScript, the parentheses around the arguments are mandatory.
You have the same range of choices for arguments in a lambda expression
(keyword arguments, variadic arguments, etc.),
but if you need anything more than
just one or two ordinary positional arguments,
you are better off using normal function notation.
Lambda notation does not change how a function will execute;
it is merely a cosmetic shorthand,
so it is only useful for already-short functions.

Lambda notation is most useful in an IIFE (but see above rant),
where a simple transformation is being performed.  Due to the nature
of the notation, lambda IIFEs need to be wrapped in parentheses before
passing arguments:

.. code-block:: evc-console

   evc> let hi = 'hello';
   evc> hi = ((x) => x + '\n')(hi);
   evc> hi;
   'hello\n'

.. note::

   Do not confuse ``=>`` with ``>=``. When doing a comparison,
   the ``>`` or ``<`` is always to the *left* of the ``=``.

     | ``>=`` means "greater than or equal to".
     | ``=>`` means "lambda arrow thingy".

Notes
-----

.. [#]

   Unlike Ruby, ``0`` is **FALSE**, like it should be!!

.. [#]

   Brian W. Kernighan and Dennis M. Ritchie.
   *The C Programming Language: Second Edition*.
   Prentice Hall, Upper Saddle NJ, 1988.  Page 24.

.. [#]

   I would replace "computation" with "computation OR execution",
   since the word "function" has also become interchangeable with "subroutine".

.. [#]

   Especially on ARM architecture,
   and especially since compilers have gotten so smart
   they can perform optimizations you never dreamed of.

.. [#]

   Actually, it has no overhead at all.  It will compile the same as if
   it wasn't there.  Local-variable namespace issues are sorted out
   entirely during compile time.  By runtime, with the exception of the
   top level during interactive mode, all local variable names have been
   replaced by stack offsets.

.. [#]

   Our lambda notation is modeled after JavaScript's, which cutely
   immitates the ``λ ↦`` notation used in lambda calculus.

