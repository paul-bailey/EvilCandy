Getting Started
===============

.. highlight:: evc-console

Running EvilCandy
-----------------

EvilCandy runs in two modes: script mode and interactive mode.
To run EvilCandy in interactive mode,
simply execute ``evilcandy`` without any command-line arguments.
To run a script with EvilCandy,
you must either name it on the command line
or pass it through EvilCandy as a pipe:

.. code-block:: sh

        # execute 'myscript.evc':
        evilcandy myscript.evc

        # pass 'myscript.evc' as a pipe
        cat myscript.evc | evilcandy

        # evilcandy will expect input from stdin:
        # Send EOF (^D on most terminals) to exit.
        evilcandy

Additional options exist for EvilCandy.  See ``man (1) evilcandy``.

Except for quoted strings and comments,
all input must be ASCII.
For both script mode or interactive mode,
any non-ASCII input must be encoded as UTF-8.

Interactive Mode
----------------

When in interactive mode, each statement of code will be prompted
by the line ``evc>``.
If a statement is multi-line, each continuation line will be
prompted by an ellipses ``...``.
The code will not executed until ENTER
after a full top-level statement has been typed.
(More on what constitutes a "statement" later.)

If a statement returns a value,
and the value is not the ``null`` object,
that value will be printed to the standard output.
To print the value of ``null``, use the ``print`` function::

   evc> 123;
   123
   evc> null;
   evc> print(null);
   null

Hello World
-----------

In EvilCandy, a "hello world" program is the following line:

.. code-block:: evilcandy

   print("Hello world");

There is no need to import any IO modules into the namespace
before using the function ``print``.
It is one of a number of function names
automatically added to the global namespace
when the program initializes.
The function prints its arguments to a file,
which is the standard output if a file is not specified.

The semicolon at the end is needed.
It marks the end of a simple statement.
The other kind of statement is a block statement,
which is wrapped in curly braces ``{...}``,
and which encapsulates a sequence of statements.
EvilCandy is more strict than JavaScript about terminating
simple statements.
Some JavaScript programs will acknowledge end-of-line as end-of-statement
if the next line commences with a new statement,
but EvilCandy will throw a syntax error if that happens.

Comments
--------

EvilCandy uses three kinds of comments.

1. Multi-line comments, beginning with ``/*`` and ending with ``*/``
2. Single-line comments, beggining with ``//`` and ending with the
   end of the line.
3. Single-line comments, beginnning with ``#`` and ending with the
   end of the line.

Mixing and matching these types of comments is bad practice.
Comment 3 exists only because I want to permit the shebang syntax,
ie. having the first line be something like:

.. code-block:: bash

   #!/usr/bin/env evilcandy

so that a file can execute itself.

EvilCandy Variables
-------------------

Other than the built-in global variables
(like ``print`` discussed above),
all variables must be declared in EvilCandy,
with one of two keywords:

* Global variables:

.. code-block:: evilcandy

     global x;
     global y = 1;

* Local variables:

.. code-block:: evilcandy

     let x;
     let y = 1;

A third kind of variable, *session variables*, will be discussed
later.

If a variable is declared without an initializer,
it will be initialized to the ``null`` object::

   evc> let x;
   evc> print(x);
   null

EvilCandy is very weakly typed.
If you assign a variable to one type,
it can be re-assigned to a different type later::

   evc> let x = 1;
   evc> typeof(x);
   'integer'
   evc> x = 'hello world';
   evc> typeof(x);
   'string'

``typeof`` is another built-in function that automatically
exists in the global namespace at startup.

.. note::

   An initializer expression can refer back to the object being
   initialized.  The object will be set to ``null`` briefly
   before being assigned the initializer value.  Keep this in mind
   if you are writing tests, because it means that the following
   quirky statement will not raise a syntax error::

      evc> let x = x;
      evc> print(x);
      null

Converting variables
~~~~~~~~~~~~~~~~~~~~

To convert a number into a string, use the ``string()`` function::

   evc> 123;
   123
   evc> string(123);
   '123'

To convert a number from a float to an integer, use ``integer()``.
This will floor the result::

   evc> 12.2;
   12.2
   evc> integer(12.2);
   12

To convert a number from an integer to a float, you *could* use ``float()``,
but it's actually faster to multiply the integer by 1.0.
Arithmetic between a float and an integer will have a float for an answer::

   evc> float(12);
   12.0
   evc> 1.0 * 12;
   12.0

To convert an integer or a float to a complex number, use ``complex()`` or,
more quickly, add 0j to the number::

   evc> complex(12);
   (12+0j)
   evc> 0j + 12;
   (12+0j)

You cannot directly convert a complex number to a float or integer.
However you can convert its modulus using ``abs()``::

   evc> integer(abs(12.0+0j));
   12

Conversion from a string to an integer depends on the nature of the text.
If it contains a leading '0x', '0b', or '0o',
then the correct base must be specified::

   evc> integer('0o777', 8);
   511

If there is no such header, the base may be any value greater than one;
if no base is provided, base 10 will be assumed::

   evc> integer('777', 8);
   511
   evc> integer('777');
   777

Variable Names
~~~~~~~~~~~~~~

Variable names are case-sensitive.
They must begin with either a letter or an underscore.
The variable name's remaining characters may be letters,
numbers, or underscores.
In this documentation, I will use the term *identifier*
to mean any text that matches these rules.

One type of identifier should be avoided, except as documented:
identifiers beginning and ending with double-underscores, like
``__init__`` or ``__str__``, are reserved for special use.  These are
commonly referred to as *dunders*.  Do not assume that because your
chosen dunder name is not documented, that you could use it for a
variable name; a future version of EvilCandy might reserve it later.

As of 0.1.0, all variable names must be ASCII.

Variable Unpack Assigning
~~~~~~~~~~~~~~~~~~~~~~~~~

If the right-hand expression is a sequential or iterable object
containing the same number of items to be assigned on the
left hand side of assignment, then a limited version of
unpack-assignment is possible::

   evc> let x, y = [1, 2];
   evc> print(x);
   1
   evc> print(y);
   2

An asterisk can be used to fill in an object with excess items::

   evc> let a, *b, c = [0, 1, 2, 3];
   evc> print(a);
   0
   evc> print(b);
   [1, 2]
   evc> print(c);
   3

.. note::

   As of 0.1.0, recursive unpacking,
   e.g. ``(a,(b, c)) = x;`` is not yet supported.


More on ``print``
-----------------

``print`` can take multiple arguments::

   evc> print('earth', 'wind', 'fire');
   earth wind fire

Note how spaces delimit the arguments in the output.
This can be changed with the special keyword argument ``sep``::

   evc> print('earth', 'wind', 'fire', sep=', ');
   earth, wind, fire

Keyword arguments are very similar in EvilCandy as they are with Python.
They take the form of *key* ``=`` *value*.

.. note::

   Not all functions take keyword arguments.
   Users should take care how they call functions;
   unlike JavaScript, EvilCandy is strict about
   the number of arguments passed to functions.

To keep the output of ``print`` on the same line across
multiple calls, use the ``end`` keyword argument::

   evc> {
    ...    print('earth, ', end='');
    ...    print('wind, ', end='');
    ...    print('fire');
    ... }
   earth, wind, fire


Print Objects Other Than Strings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``print`` function has a way to print any kind of object.
In the case of strings, a string is directly printed to the output.
In other cases, the object is converted to a string representation
before printing to the output::

   evc> print('The number', 23);
   The number 23
   evc> print(5+3j);
   (5+3j)

In many cases,
the output will be printed in a way that the interpreter
reading the text back would be able to recreate
an exact copy of the object.
In other cases,
an abstract expression of the object will be printed,
usually within angle brackets::

   evc> print(print);
   <function (intl) at 0x10c7c9360>

Printing With f-strings
~~~~~~~~~~~~~~~~~~~~~~~

Text beginning with ``f'`` or ``f"`` is a format string,
also called an "f-string."
A pair of curly braces in the string wraps something to be evaluated
and inserted into the text::

   evc> let age = 20;
   evc> print(f'I wish I was {age} again');
   I wish I was 20 again

Within the curly braces, a colon ``:`` followed by printf-like
formatting instructions will specify the conversion::

   evc> let val = 511;
   evc> print(f'The register contains a value of 0x{val:08X}');
   The register contains a value of 0x000001FF

Basic Programming With EvilCandy
--------------------------------

Number Calculations
~~~~~~~~~~~~~~~~~~~

Let's start with the most basic calculations.
EvilCandy processes the usual mathematical operators,
``*`` for multiplication, ``/`` for division, ``+`` and ``-``
and so on.
``%`` is the modulo (remainder) operator.
``**`` is the power operator.

EvilCandy also has a number of bitwise operators.
Do not confuse these with logical operators.
Unless you are used to C or assembly,
chances are you are looking for the logical operators,
which are used in truth statements.
Bitwise operators, on the other hand,
perform binary operations on integers.
(They are also used with sets, which we'll discuss later.)

The bitwise operators are:

  | ``^`` for exclusive OR
  | ``|`` for inclusive OR
  | ``&`` for AND
  | ``~`` for NOT
  | ``<<`` for left shift
  | ``>>`` for right shift

Parentheses are for grouping.
Do not think you are being too fussy with parentheses.
The order of operations is very inconsistent
from one programming language to another,
so parenthesize any time you are unsure.

Division between integers is floored::

   evc> 3 / 2;
   1

When arithmetic is performed between numbers of different types,
the result will be the type with a higher class priority.
These priorities are: complex > float > integer::

   evc> 3 / 2.0;        // answer will be float
   1.5
   evc> 3 + (1.0 + 4j); // answer will be complex
   (4+4j)

Integers are limited to values that can be stored in
a signed 64-bit value.  Operations which take an integer
out of those boundaries will result in an exception::

   evc> 200**200;
   [EvilCandy] NumberError boundary error for ** operator

String Processing
~~~~~~~~~~~~~~~~~

A *string* is an array of sequential characters,
or Unicode points.
Its literal expression in EvilCandy is as text
wrapped by single or double quotes.
If quotes exist within the text, they must be escaped by a backslash \\.
Alternatively, if only one kind of quote exists in the text,
you could wrap it with the other kind.

.. code-block:: evilcandy

   // Escaped quote
   let y = 'Grabthar\'s hammer';

   // Alternative quote used, slightly more readable
   let x = "Grabthar's hammer";

Backslashes themselves must be escaped,
simply by typing two backslashes instead of one,
or the interpreter will think they are escaping the next character.

Correct example. The interpreter will complete the statement:

.. code-block::
   :class: example-good

   evc> '\\';
   '\\'

Incorrect example.
The user has pressed ENTER, but
the interpreter thinks that the second quote, semicolon, and newline
are part of the original string:

.. code-block:: none
   :class: example-bad

   evc> '\';
   ...

Strings may be concatenated.
If two strings are *literals*, that is,
they are typed out literally rather than
assigned to a variable,
they may exist side by side::

   evc> 'hello ' 'world';
   'hello world'

Otherwise they may be concatenated with the ``+`` operator::

   evc> let s = 'hello ';
   evc> s + 'world';
   'hello world'

String literals may wrap multiple lines::

   evc> 'This is a line.
    ... This is another line.';
   'This is a line.\nThis is another line.'

.. note::

   Wrapping lines in this way tends to render the code difficult to read,
   since it messes up indentation.  A better way is this::

      evc> 'This is a line.\n'
       ... 'This is another line.';
      'This is a line.\nThis is another line.'

   or, if speed is not a concern::

      evc> '\n'.join([
       ...    'This is a line.',
       ...    'This is another line.'
       ... ]);
      'This is a line.\nThis is another line.'

   This will be explained in more detail later.

Strings can be repeated if multiplied by an integer::

   evc> 3 * 'well'; // my favorite story
   'wellwellwell'

Strings can be indexed according to character, starting from index zero.
Slicing is also possible with strings.
A negative number indexes from the end::

   evc> 'hello world!'[-1];
   '!'
   evc> 'abcde'[0:5:2];
   'ace'
   evc> 'abc'[1];
   'b'

Out-of-range indices will result in an exception,
but an out-of-range slice will be handled (kind of) gracefully::

   evc> 'abc'[10];
   [EvilCandy] IndexError Subscript '10' out of range
   evc> 'abc'[10:];
   ''

String lengths can be determined by the global ``length()``
function::

   evc> length('abc');
   3

Lists
~~~~~

Javascript calls these arrays.
I have chosen the Python term "list",
because I do not want to mislead you into thinking these are
fast and efficient ways to process large amounts of data.

A list is a sequence of objects,
expressed literally between square brackets ``[`` and ``]``,
and delimited by commas.
Their contents may be of any type,
and may contain mixed types::

   evc> let x = ['a', 'b', 'c'];
   evc> x;
   ['a', 'b', 'c']
   evc> let y = ['some_text', 1, 12.7, ['another', 'list']];
   evc> y;
   ['some_text', 1, 12.7, ['another', 'list']]

Lists may be concatenated with the ``+`` operator,
or repeated by multiplying by an integer.
This will always result in the creation of a new list::

   evc> let x = ['a', 'b', 'c'];
   evc> x + [1, 2, 3];
   ['a', 'b', 'c', 1, 2, 3]
   evc> x * 3;
   ['a', 'b', 'c', 'a', 'b', 'c', 'a', 'b', 'c']

You may append an item to a list using the class's built-in ``append()`` method::

   evc> let x = ['Peter', 'Paul'];
   evc> x.append('Mary');
   evc> x;
   ['Peter', 'Paul', 'Mary']

You may delete an item in a list using either the ``.remove()`` method
or the ``.pop()`` method.  ``remove`` removes the first item matching
the argument, and ``pop`` removes the item at the index specified by the
argument.  You may also delete a list item with the ``delete`` keyword::

   evc> let x = [1, 2, 3, 4, 5];
   evc> x.pop(1);
   2
   evc> x;
   [1, 3, 4, 5]
   evc> x.remove(4);
   evc> x;
   [1, 3, 5]
   evc> delete x[1];
   evc> x;
   [1, 5]

Indexing and slicing are the same for strings as for arrays,
except that unlike with strings, arrays can be assigned by slices::

   evc> let x = [1, 'two', 3, 'four', 5];
   evc> x[0:5:2] = ['one', 'three', 'five'];
   evc> x;
   ['one', 'two', 'three', 'four', 'five']

