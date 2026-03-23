Program Flow
============

Of course programming is more than crunching numbers in sequence.
There are also decision points, branching, and looping.
Take for instance this code to calculate π using the Leibniz
algorithm (albeit inaccurately due to the low number of iterations):

.. code-block::

   evc> let result = 0;
   evc> let num = 4.0;
   evc> let den = 1;
   evc> let i = 0;
   evc> while (i < 200) {
    ...    result += num / den;
    ...    den += 2;
    ...    result -= num / den;
    ...    den += 2;
    ...    i++;  # "++" means "increment by one"
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
They take one of two form:

| ``if (`` *expr* ``)`` *stmt*

or

| ``if (`` *expr* ``)`` *stmt1* ``else`` *stmt2*

In the former case, if *expr* is true, execute the following
statement, otherwise skip it.
In the latter case, execute *stmt1* if true
or *stmt2* if false.

Evaluating boolean expressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
and a false value into the integer 0.

.. code-block::

   evc> !!'A';  # string length is one, so it's true
   1
   evc> !!'';   # empty string is false
   0
   evc> true;
   1
   evc> false;
   0

.. note::

   A string is *true* if its length is greater than zero,
   even if it contains only embedded nulchars:

   .. code-block::

      evc> !!'\0\0\0';
      1

"do" statements
---------------

A ``do`` statement takes the form

        | ``do`` *stmt* ``while (`` *expr* ``)``

*stmt* is always executed the first time.
It is then repeated so long as *expr* remains true.
``do {`` *stmt;* ``} while (false)`` is equivalent
to ``{`` *stmt* ``}``.
Both cases enable local variables to be declared within the braces
without cluttering up the outer namespace.
(More on variable scope later).

"for" Statements
----------------

EvilCandy's ``for`` statements resemble Python's rather than
JavaScript's verbose C-like ``for`` statements.
The Python-style ``for`` loop is inherently faster
than the traditional ``for`` statement,
due to some under-the-hood implementation details involving
the creation and destruction of objects during the iteration step.
They are also faster than JavaScript's
sequential objects' ``.foreach()`` method,
since the latter requires the overhead of a frame push
for every iteration of the loop.

Since any traditional ``for`` loop can be implemented
by a ``while`` loop...

        | ``for(`` A ``;`` B ``;`` C ``)``
        |          *stmt*

is the same as

        | A;
        | ``while (`` B ``) {``
        |          *stmt*
        |          C
        | ``}``

...there is no need for a C-like ``for`` statement.

In EvilCandy, the ``for`` statement takes the form:

        | ``for (`` *identifier* ``in`` *sequence* ``)``
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

.. code-block::

   evc> let result = 0;
   evc> let num = 4.0;
   evc> let den = 1;
   evc> for (i in range(200)) {
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
also uses an optional ``else`` clause,
so the interpreter will hold off execution until it gets
the next token;
if it's not ``else``, then the iterpreter
will assume it to be the start of the next statement
and therefore execute the ``for`` loop.
To force execution before typing the next statement
(``print`` in this example),
the enpty statement ``;`` will do the trick.
If the interpreter is parsing a script,
or if it is in interactive mode
but nested at least one layer deep in program flow,
the semicolon is not needed.

The following example is a little trivial (why calculate it if you
already know the answer?),
but it demonstrates the usefulness of ``else`` in the ``for`` loop.

.. code-block::

   evc> let result = 0;
   evc> let num = 4.0;
   evc> let den = 1;
   evc> for (i in range(200)) {
    ...     result += num / den;
    ...     den += 2;
    ...     result -= num / den;
    ...     den += 2;
    ...     # from the 'while' example above you can see
    ...     # that this 'break' will never occur.
    ...     if (abs(result - 3.14) < 0.0001)
    ...         break;
    ... } else {
    ...     print('not enough iterations');
    ... }
   not enough iterations

That is, if the program runs through the entire sequence
(``range(200)`` in this example)
without stopping the iteration early with ``break``,
then the body of ``else`` will be executed.

This also illustrates a pitfall to avoid.
Indentation is useful for human readability,
but the interpreter does not care.
**Always use braces around** ``for`` **loops**,
even for bodies containing only a single simple statement.
Otherwise indendation can be misleading.  Consider the following.

   .. code-block::

        # THIS IS BAD!
        for (x in y)
            if (x)
                do_this();
        else
            do_that();

The ``else`` in this example appears as
the ``else`` clause of the ``for`` statement,
but it is actually part of the ``if`` statement.


Functions
---------

Unless we're discussing some egg-headed CS concept of a function,
the most succinct definition of a function is in K&R:
"A function provides a convenient way to encapsulate some computation,
which can then be used without worrying about its implementation" [#]_ [#]_.

To begin with, we don't want to repeat typing the Leibniz algorithm
every time we want to execute it.
So we put it into a function as follows:

.. code-block::

   evc> let leibniz = function(n) {
    ...    let result = 0;
    ...    let num = 4.0;
    ...    let den = 1;
    ...    for (i in range(n)) {
    ...            result += num / den;
    ...            den += 2;
    ...            result -= num / den;
    ...            den += 2;
    ...    }
    ...    return result;
    ... };

The semicolon at the end of the function definition
is needed in this case because,
if you look closely, you will see that it takes the form:

.. code-block::

   let name = expression;

This is similar to JavaScript's anonymous-function notation.
However, **in EvilCandy all functions are anonymous**.
The more conventional JavaScript way to name a function

.. code-block:: js

   // JavaScript function
   function leibniz(n) {
        .....
   }

is not permitted in EvilCandy.

For this example, I've chosen an argument ``n`` to determine
how many iterations of the algorithm to use.  Clearly the
more iterations the more accurate the result.

.. code-block::

   evc> leibniz(10);
   3.0916238066678399
   evc> leibniz(10000);
   3.1415426535898248
   evc> leibniz(1000000);
   3.1415921535897242

Unlike JavaScript, the number of arguments to a function
is strictly enforced.

.. code-block::

   evc> leibniz(100, 1);
   [EvilCandy] ArgumentError Expected at most 1 args but got 2
   evc> leibniz();
   [EvilCandy] ArgumentError Expected at least 1 args but got 0

Variadic functions or functions taking keyword arguments
can use star or double-star notation, similar to Python.

.. code-block::

   evc> let foo = function(*args, **kwargs) {
    ...    print('args:', args);
    ...    print('kwargs:', kwargs);
    ... };
   evc> foo('line', 1, kw_a='a', kw_b='b');
   args: ['line', 1]
   kwargs: {'kw_a': 'a', 'kw_b': 'b'}

A sequential object can be unpacked into the argument
using the star operator.

.. code-block::

   evc> let x = [1,2,3];
   evc> foo(*x);
   args: [1, 2, 3]
   kwargs: {}
   evc> foo(x);
   args: [[1, 2, 3]]
   kwargs: {}

In the above first case, there are three arguments, 1, 2, and 3,
while in the second case, there is one argument, a list containing
1, 2, and 3.

All functions return a result.
If program flow reaches an end
without encountering the ``return`` statement,
the function will return ``null``.

.. code-block::

   evc> let do_nothing = function() {;};
   evc> do_nothing();
   evc> print(do_nothing());
   null

Nested Functions and Closures
-----------------------------

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
(For C programmers, think of this as a function with a local static
variable, except that in EvilCandy's case, the variable is initialized
not at compile time, but later, each time the function is instantiated.)
A function which returns a closure, then, is very powerful.

Take, for instance, the following example:

.. code-block::

   evc> let multer = function(n) {
    ...    return function(x) {
    ...            return x * n;
    ...    };
    ... };
   evc> let doubler = multer(2);
   evc> let tripler = multer(3);

In this example, ``multer(n)`` returns a lambda which multiplies an input
by the value specificied by ``n``.  ``doubler`` and ``tripler`` have been
assigned different instantiations of this lambda.  Now let's see what
they do:

.. code-block::

   evc> doubler(3);
   6
   evc> tripler(3);
   9

Although it's more correct to say that the lambda instantiated by and
returned from ``multer`` is a closure, it's more common to say that
``n``, the argument passed to ``multer`` and remembered by the lambda,
is the closure.  Going forward, I will use the latter, more casual,
definition of "closure".

A few things to also note about closures:

1. In EvilCandy, every script is thought of as a function at the top
   level, so any function that accesses script-scope variables will
   create a closure.  The above example could also be express in
   this way, and ``n`` will still be a closure:

   .. code-block::

      evc> let n = 2;
      evc> let doubler = function(x) {
       ...    return x * n;
       ... };
      evc> n = 3;
      evc> let tripler = function(x) {
       ...    return x * n;
       ... };
      evc> doubler(3);
      6
      evc> tripler(3);
      9

   Note also that changing ``n`` in its normal scope (setting it to 3)
   did not affect the ``n`` value of ``doubler``.  This is because
   ``n`` wasn't really "changed".  It was reassigned.  The closure
   received its own handle to the earlier ``n``, so reassigning the
   "outer" ``n`` has no effect on ``doubler``.  This has implications
   for mutable objects, which I'll get to in a minute.

2. Accessing global variables will not result in the creation of a
   closure, since every scope has access to every global variable.
   The example in note 1 above will not work if ``n`` was declared
   with ``global`` instead of ``let``.

3. A closure is writable, but it will only take effect in the enclosed
   function, not the original scope.

   .. code-block::

      evc> let n = 1;
      evc> let x = function() { n++; };
      evc> x(); # increment "enclosed" n
      evc> n;   # see no change to "outer" n
      1

   As a side-note, there are so few cases where reassigning a closure
   like this is useful, that some programming languages like Python
   do not even allow it.

4. If closure data is mutable, such as a list, then modifying (not
   reassigning) the closure data *will* effect the data in the outer scope.

   .. code-block::

      evc> let x = [];
      evc> let f = function(val) {
       ...    x.append(val);
       ... };
      evc> f(1);
      evc> x;
      [1]

   This has useful implications for creating private data when building
   user-defined classes.

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


