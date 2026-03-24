More EvilCandy Data Types
=========================

Tuples
------

Tuples are similar to lists, except that they are immutable.
They may be accessed the same way as a list, except that they cannot
be modified.
Tuples are unique among EvilCandy's built-in types in that
they may or may not be hashable depending on whether their
contents are all hashable.
This affects whether a tuple may be placed in a dictionary.

Tuples are expressed as a comma-delimited sequence of expressions,
encased in parentheses, such as ``(1, 2)``.
In most cases, the parentheses are optional::

  evc> 1, 2;
  (1, 2)

However, the parentheses are needed anywhere the tuple is embedded
within another comma-separated sequence, for example a function argument,
a list, or another tuple.

A tuple containing only one item must have a comma at the end of the
expression, or it will not be regarded as a tuple::

  evc> (1,);
  (1,)
  evc> (1);
  1

Single-item tuple literals must also be wrapped by parentheses::

  evc> (1,);
  (1,)
  evc> 1,;
  [EvilCandy] SyntaxError Invalid token
  in file '<stdin>' near line '1'
  Suspected error location:
          1,;
           ^

An empty tuple is expressed as just ``()``.

A tuple may also be declared with the ``tuple()`` function.
An empty tuple is created if no arguments are given.
Otherwise the argument should be a sequential object::

  evc> tuple('abc');
  ('a', 'b', 'c')

Tuples can be nested or concatenated to make new tuples::

  evc> (1, 2), (3, 4);
  ((1, 2), (3, 4))
  evc> (1, 2) + (3, 4);
  (1, 2, 3, 4)

Sets
----

Sets are a sequence of unique, immutable items.

They are expressed as a sequence of comma-delimited expressions
wrapped by curly braces::

  evc> let x = {1, 1, 2, 3, 4};
  evc> x;
  {1, 2, 3, 4}

A set may only contain unique values, so when creating the set,
the superfluous 1 was discarded.
(Note that the expression could not begin the statement above, because
``{`` at the beginning of a statement marks it as a block statement.)

"Unique" has some quirks.
Numbers of different type which are equal to each other
are not considered unique from each other::

  evc> x = {1.0, 1, 2, 3, 4};
  evc> x;
  {1.0, 2, 3, 4}

Sets may not contain unhashable (in other words, mutable) objects::

  evc> x = {[1, 2], 3};
  [EvilCandy] KeyError 'list'is unhashable

Sets may not be accessed by index number::

  evc> x;
  {1.0, 2, 3, 4}
  evc> length(x);
  4
  evc> x[1];
  [EvilCandy] TypeError Cannot get attribute '1' of type set

Sets can support various operators::

  evc> let x = {1, 2, 3, 4};
  evc> let y = {3, 4, 5};
  evc> x | y;   # union operator
  {1, 2, 3, 4, 5}
  evc> x - y;   # difference operator
  {1, 2}
  evc> x & y;   # intersection operator
  {3, 4}
  evc> x ^ y;   # exclusive OR operator
  {1, 2, 5}

Sets can be declared with the ``set()`` function.
This is necessary to declare an empty set, since ``{}`` is
considered an empty dictionary::

  evc> let x = {};
  evc> typeof(x);
  'dict'
  evc> x = set();
  evc> x;
  set()
  evc> typeof(x);
  'set'

``set()`` otherwise accepts an iterable sequence::

  evc> set('abba dabba');
  {'d', 'b', 'a', ' '}

Dictionaries
------------

A dictionary is what eggheads call *associative arrays*
and what JavaScript calls *objects*.
Although EvilCandy's dictionaries are not pure dictionaries
as in Python—they are very much the way to create user-defined
classes—I have chosen to use Python's term "dictionary",
since calling one kind of object an "object" to distinguish
it from other kinds of objects is just frustrating.

A dictionary's literal expression is a comma-delimited sequence
of key:value pairs, surrounded by curly braces, for example::

  { 'name': 'Paul', 'height': 10, 'status': 'bald' }

The key may be any hashable object::

  evc> let x = {1: 'a', (0,): 'b'};
  evc> x;
  {1: 'a', (0,): 'b'}
  evc> x = {[1]: 'a'};
  [EvilCandy] KeyError 'list' is unhashable

They are indexed according to key, not insertion number::

  evc> let x = {'a': 5, 1: 5};
  evc> x['a'];
  5
  evc> x[1]; # 1 is a key to x
  5
  evc> x[0]; # 0 is not a key to x
  [EvilCandy] TypeError Cannot get attribute '0' of type dict

If a key is a string, and it happens to follow the same rules
as an identifier, then it can be accessed using JavaScript-like
dot notation::

  evc> let x = {'a': 1, 'b': 2};
  evc> x['a'];
  1
  evc> x.a;
  1

If a key does not exist, then that member of the dictionary
cannot be read.  It *can* be written, however::

  evc> let x = {};
  evc> x.a;
  [EvilCandy] TypeError Cannot get attribute 'a' of type dict
  evc> x.a = 1;
  evc> x;
  {'a': 1}

It can also be reassigned or deleted with the ``delete`` keyword::

  evc> let x = {'a': 1, 'b': 2, 'c': 3};
  evc> x.b = 'two';
  evc> delete x.c;
  evc> x;
  {'b': 'two', 'a': 1}

Dictionaries can be defined also using the ``dict()`` function,
using keyword arguments::

  evc> dict(name='paul', status='bald');
  {'name': 'paul', 'status': 'bald'}

Pitfall Notice for JavaScript Users
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If a key expression in a dictionary literal is just an identifier,
EvilCandy will evaluate the identifier to create the key.
It will *not* convert the identifier into a string the way many
JavaScript implementations will do.  Given the following::

   let a = 'not_a';
   let x = { a: 1 };

The resultant dictionary would be::

   # JavaScript result:
   { 'a': 1 }

   # EvilCandy result:
   { 'not_a': 1 }

In JavaScript, square brackets force an identifier in the key expression
to be evaluated rather than converted into a string.  EvilCandy *always*
evaluates the key expression.  Square brackets will just cause an
exception to be thrown, since lists are unhashable.

