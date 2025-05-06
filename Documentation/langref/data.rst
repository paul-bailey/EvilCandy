.. _langref_identifiers:

Objects and Data
================

For the purposes of this documentation, *name* and *identifier* are
interchangeable.  I will try to be consistent and use "identifier"
where practical, but occasionally it is clearer to call something a
"named variable" instead of an "identified variable".

All data visible to the user is stored in a structure called an
**object**.  *Object* and *variable* seem like interchangeable [#]_
terms, however the casual term *variable* connotes a one-to-one
relationship between it and an identifier, but this is not the case
with an object.  C-language programmers might be able to visualize
this better by regarding of all named variables in EvilCandy as
"pointers" to some data; thus a single object may have more than one
named variable at a time.

Thus, I will use the term "variable" generally to mean an identifier
which has been declared and is currently (but perhaps not permanently)
paired with some object.

Storage Class
-------------

There are four storage classes for objects, three of which could be
thought of as "variables":

1. *automatic variables*, also called stack variables or local variables.
   These objects are destroyed by garbage collection as soon as program
   flow leaves scope and all other objects with a reference to them are
   also garbage-collected.
2. *closures*, object-variables which are created dynamically during the
   instantiation of a new function-type object.
3. *global variables*, which are stored in a global symbol table, and are
   available to all levels of program flow and all scopes.
4. *elements*, or items, stored by other objects, such as in a dictionary
   or array.  I will try to avoid the term "variable" when discussing
   these, except where they are retrieved from the containing object and
   assigned to a "variable".  "Primary variable" is a further distinction,
   which will never refer to an *element*.  This is important, because
   some syntactic rules apply differently between elements and primaries.

**Implementation Note:**
   Automatic variables are not, in the low-level implementation,
   accessed using a name for a key.  Rather, they are accessed as offsets
   from a frame pointer, cooked into the pseudo-assembly instructions at
   parsing time.  It means that automatic variables are technically much
   faster than global variables.  This speed advantage is mostly useful
   in algorithmically intense pure functions which need to repetitively
   manipulate local variables.  On most other occasions, data accesses
   are for elements as often as for primaries, and this is about the
   same speed as global-variable access.

Basic Types
-----------

.. note::

   This explains the behavior and purpose of the data types.  For a
   reference to all of their built-in methods and properties, see
   :ref:`libref_index`.

Each class of object has its own set of properties, attributes, behavior,
etc., which is often called a *class*, but I will just as frequently
refer to it as a *type*.  EvilCandy is dynamically typed: it is perfectly
legal for a variable which has previously been assigned one object to
later be assigned another object of a different class.

The data types visible to a user are summarized as follows:

========== ========================== =========
Type       Declaration Example        Mutable?
========== ========================== =========
empty      ``let x;``                 no
integer    ``let x = 0;``             no
float      ``let x = 0.;``            no
list       ``lex x = [];``            yes
dictionary ``let x = {};``            yes
tuple      ``let x = ();``            no
string     ``let x = "";``            no
bytes      ``let x = b"";``           no
function   ``let x = function() {;}`` no
file                                  yes
range                                 no
method                                no
========== ========================== =========

*Mutable* means that the object can be modified in-place.  *Immutable*
means that it cannot; if a variable is assigned to an immutable object,
it must be re-assigned to a new object in order to contain modified data.

Empty
~~~~~

There is only one instantiation of the class "empty," which can be
identified with the keyword ``null``.  Functions that do not explicitly
return a value will return this object by default.  When variables are
declared without initializers they will be set to ``null`` by default.
The expressions ``let x;`` and ``lex x = null;`` are practically
identical [#]_.

Numbers
~~~~~~~

There are three classes of numbers in EvilCandy: integers, floats, and
complex.

Bitwise operators are only permissible for integers.  Modulo operators
are permitted for integers and floats but not complex numbers.  The
other numerical operators are permitted for all three types.  When
numbers of different types are operands to a binary operator, an
intermediate conversion will take place to guarantee that the result is
the correct type: if either of the two operands are complex, then the
result will be complex; if the operands are a float or integer in either
order, then the result will be a float.

Integers
````````
All integers are stored as *signed* 64-bit values.  The highest positive
integer that can be processed by EvilCandy is 9223372036854775807.  The
lowest negative integer that can be processed by EvilCandy is
-9223372036854775808.

The Boolean expressions ``true`` and ``false`` are in fact this class
of integer, with values 1 and 0, respectively.

Floats
``````

All floats are stored as IEEE-754 double-precision [#]_ floating point
numbers.  The largest-magnitude finite value of a float in EvilCandy
is positive or negative 1.7976931348623157e+308.  The
smallest-magnitude non-zero value is 2.2250738585072014e-308.

Complex
```````

Complex numbers are stored in the C11-standard ``complex double``
type.

.. note::

   This will likely soon change such that the real and imaginary
   components are explicitly stored in double-precision floats.

Sequences
~~~~~~~~~

Sequences include any object class which can be produce another object
when requested by using numerical-array notation, for example ``x[i]``,
where ``x`` is the sequence object and ``i`` is some integer.  A negative
``i`` references from the end of the sequence, while a positive ``i``
references from the beginning.  ``x[-1]`` will retrieve the last item
in the sequence, and ``x[0]`` will retrieve the first.  Sequences may be
storing the requested data (such as with lists or tuples),
or they may create the data computationally upon request (such as with
strings or bytes).

Some sequences support array slicing, in which the sequence object will
return a new sequence of the same type, containing a subset of the
original sequence.  This notation is nearly identical to Python, where
``x[start:stop:step]`` returns all objects contained in *x* whose
index is *i* where *start* <= *i* < *stop* at *step* intervals.

The length of a sequential objects can be determined with the built-in
``length`` function [#]_, such as ``length(x)`` where ``x`` is the
sequential object in question.

Any sequence which supports the concatenation ``+`` operator also
supports the multiplication ``*`` operator, so long as the other
operand is an integer.  This is most easily explained by a quick example.
``3 * [1, 2]`` will produce the list ``[1, 2, 1, 2, 1, 2]``; ``3 * 'abc'``
will produce the string ``'abcabcabc'``.

Lists
`````

Lists are numerical arrays whose contents may be of any type, and whose
types do not need to match.

Lists are mutable.  If two variables have a handle to the same list,
then one variable's changes will affect the other variable.

Lists literals are expressed the same as with JavaScript: a set of
expressions in between square brackets, delimited from each other by
commas.

A list of size zero is expressed simply ``[]``.

Once created, lists may not be de-referenced outside of their bounds, or
an exception will occur.  To expand or shrink these bounds, use built-in
access methods like ``append`` or ``pop``.

When dereferencing a list with a slice, a new list will be created.
Thus ``x[:]`` is equivalent to ``x.copy()``.

.. note::

   JavaScript calls these "Arrays".  EvilCandy's interpreter source code
   also calls these arrays.  But one, "array" is something of a blanket
   term that could mean even non-sequential data types (such as "associative
   array"); and two, "array" implies speed to anyone used to more low-level
   programming, and EvilCandy's list datatype, much like Python's list
   datatype or JavaScript's array datatype, was not designed to be efficient
   at managing large amounts of similar data.  In fact, in all three cases
   the contents of an array may be any mix-matched type.  Therefore I will
   use Python's equally-bad term "List."

Tuples
``````

Tuples are the same as lists in every way but three:

1. Tuples expressions use parentheses instead of square brackets.
2. Tuples are immutable, while lists are not.
3. Unless a tuple has a comma, even if its length is one, it will not
   evaluate as a tuple.  Although it looks sloppy, a tuple containing
   only one expression ``x`` must be expressed as ``(x,)``.  If it is
   expressed as ``(x)`` it will evaluate to whatever ``x`` is.

Strings
```````

Internally, strings are C strings--a sequence of octets terminated by a
value of 0--and any non-ASCII characters are encoded in UTF-8, which is
backwards-compatible with ASCII.

To the user, however, strings are more abstractly a sequence of text.
A string's length is the number of encoded characters it contains, not
the number of bytes used to store it.  When de-referenced by index ``i``,
the ith *character*, not byte, will be returned as a string of size 1.

An exception to this rule is if the string's encoding cannot be
determined.  If any non-ASCII characters exist which are not properly
encoded in UTF-8, or if they are values outside the range of valid
Unicode code points, the string will be assumed to be contain only
8-bit-wide characters.

Strings are immutable objects.

Bytes
`````

Bytes objects are sequences of unsigned octets whose values range from 0
to 255, inclusively.  When dereferencing a bytes object with an index,
the result will be an integer.  When dereferencing a bytes object with a
slice, the result will be another bytes object.

Bytes are immutable objects.

Floats
``````

Do not confuse *floats* and *float*.  The former is a sequential type and
the latter is a number type.  A floats object contains a true array of
double-precision floating-point numbers.  This is due to the fact that
lists are extremely inefficient with large amounts of data.  A floats
object is faster and more compact.  It was intended for DSP and statistics.

.. note::

   The floats data type currently has minimal utility, and is more of
   a wish-list thing.

Dictionaries
~~~~~~~~~~~~

A dictionary is referred to as an "object" in JavaScript.  There are good
reasons to keep that terminology here, since EvilCandy's JavaScript-like
notation for dictionaries treats its members like class attributes.  This
is the data class for building user-defined object classes.  However, I
chose Python's terminology, because calling one object an "object" to
distinguish it from other objects is just plain confusing.

A dictionary is an associative array--an array which is dereferenced
by enumeration instead of by index number.  Or--to be specific to
EvilCandy--it is an array whose keys are strings instead of integers.
As with lists, dictionaries' length may be determined with the built-in
``length`` function, which returns the number of stored entries.

.. note::

   In the current version of EvilCandy, dictionary insertion order is not
   preserved, and iterating through a dictionary will instead be done in
   alphabetical order of its keys.  This could change in the future.

A dictionary literal expression is the same as with JavaScript: curly
braces surrounding a set of key-value pairs delimited from each other
with commas, the pairs internally delimited by semicolons.  The key
may be expressed as either a string literal or an identifier token.
If it is a string literal, it does not need to follow the rules for
identifier tokens.  However, a key which does not follow the rules for
identifier tokens may not use the dot notation to access the data; it
may only use the square-bracket associative array notation.
An empty dictionary literal is expressed as ``{}``.

Computed keys in literal expressions must be surrounded by square
brackets.  The expression ``{ key: value }`` will produce a key whose
name is ``'key'``, while the expression ``{ [key]: value }`` will
produce a key whose name is whatever ``key`` evaluates to.

Dictionaries are mutable.

**Implementation Note:**
        Computed keys could have an adverse effect on performance.
        Strings, being immutable, are hashed at most one time during
        their lifetime, and many strings, especially those from tokens
        parsed in the script, are de-duplicated and (nearly) immortalized
        at parse time.  Computing keys, on the other hand, increases the
        chance of repeated hash calculations for otherwise matching strings
        that have never been expressed literally in the source code.

Accessing Dictionary Items
``````````````````````````

When *getting* a dictionary item with a proper string-type key and
the key is not found in the dictionary, a KeyError exception will be
thrown.

When *inserting* an item with a proper string-type key, no exception
will be thrown.  If the key already exists, then the old value
associated with it will replaced by the new value being inserted.

.. _langref_data_method:

When attempting to retrieve a function from a dictionary, a
`method object <Methods_>`_ will be returned instead.  When inserting
a method object into a dictionary, if it is the same dictionary that the
method is bound to, then only the function which the method contains
will be inserted into the dictionary.  This is to prevent a cyclic
reference, which could prevent proper disposal of the dictionary.

Dictionary Operators
````````````````````

The bar character ``|`` acts as a union operator between two
dictionaries.  A new dictionary will be created containing all the
items from both operands.  If the two operands have any matching keys,
then the right-hand operand's value will be inserted into the result.
This operation will bypass the replacing of functions with methods;
the *exact* objects from the operands will be inserted into the result.

Callable Objects
~~~~~~~~~~~~~~~~

Code Objects
````````````

I am again borrowing Python terminology, this time for the arbitrary
reason that "code object" is easier to say out loud and even think about
than "executable binary."

Users cannot see these objects directly, but it is almost impossible to
write documentation which does not refer to them.  These are the binary
arrays of executable code that implements a function's definition.  They
are created statically at parse/compile time.

Functions
`````````

A function executes code and returns either a value or ``null``.

In EvilCandy, all functions are anonymous, and all function definitions
are considered evaluable expressions.  They can be defined in most
places where an expression is valid, including as function arguments.

While the code object the function executes is generated at compile time,
function objects are created dynamically at runtime.  Many functions can
use the same code object, but they may have different closures or refer
to different instantiations of ``this``.

Built-in functions are designed to appear to the user as the same class
as user functions, however they have C function pointers rather than
code objects.

**Implementation Note:** The only structural difference between a user
function and a built-in function is that a built-in function has no
closures, at least not in the same sense as user functions do, and a
built-in function calls a C function while a user function evaluates a
code object in EvilCandy's virtual machine.

Methods
```````

Method objects are intended to appear no different to the user than
function objects, however they do identify differently than the functions
they wrap, so I'll mention them here.  These are
`instantiated <langref_data_method_>`_ when a function is retrieved from
a dictionary.  This is the under-the-hood hack that enables dictionaries
to be treated like user-defined class instantiations instead of just pure
dictionaries.

Notes
-----

.. [#]

   In the interpreter's source code, I used the term *var* for "object"
   a lot.  This is an artifact of early development when immutable
   objects were internally copied back and forth rather than passed by
   reference.

.. [#]

   Practically but not perfectly.  The second statement executes all of
   the first statement, then follows it up with the redundant step of
   loading ``null`` and assigning it to a variable already pointing at
   it.  For this reason it is wasteful to declare anything ``null`` as an
   initializer.

.. [#]

   I am aware that there exists this thing called a single-precision
   float.  I am also aware that there exists a thing called a fax
   machine.  This isn't the 90s anymore.

.. [#]

   I chose JavaScript's "length" instead of Python's "len", even though
   it looks more Python-like to wrap an object than to call the object's
   own method, because I do not want users to lose access to this useful
   global function simply because they have a local variable also named
   "len".  I think Python must have been flexing their ability to resolve
   a confusing namespace when they decided to use such typical
   local-variable names like "len" and "str" to be their global built-ins.


