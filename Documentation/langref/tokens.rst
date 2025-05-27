
.. _langref_tokens:

Tokens
======

EvilCandy classifies its tokens largely the same way as anyone else does:
whitespace, identifiers, keywords, constants like quoted strings or
numerical expressions, operators, and other separators and delimiters.
With the exception of quoted strings and comments, all tokens, including
whitespace, must be ASCII.

Comments
--------

EvilCandy does not parse comments as tokens.  It does, however, count the
end-of-line marker so it can keep track of file position for the sake of
error reporting.

EvilCandy supports three kinds of comments:

1. Multi-line comments, beginning with ``/*`` and ending with ``*/``
2. Single-line comments, beginning with ``//`` and ending with the
   end of the line.
3. Single-line comments, beginning with ``#`` and ending with the
   end of the line [#]_.


Whitespace Tokens
-----------------

The whitespace characters are space, horizontal tab, vertical tab,
form-feed, newline, and carriage return.  Do not use non-ASCII whitespace.

EvilCandy ignores whitespace, except for some bookkeeping on the line
number to facilitate more helpful error messages.  Also some adjacent
tokens may need whitespace to separate each other.  For example, ``1- -2``
is valid (though poorly written) and means "one minus negative two",
but ``1--2`` is invalid, because ``--`` is a token meaning "decrement".

Identifier Tokens
-----------------

Identifiers are the names of variables.  They must start with a letter
or an underscore ``_``.
The remaining characters may be any combination of ASCII letters, numbers,
and underscores.
All identifiers in EvilCandy are case-sensitive.

String Literal Tokens
---------------------

String literals are wrapped by either single or double quotes.
Unicode characters are permitted within the quotes so long as they
are encoded in UTF-8.  If any non-UTF-8 characters are encountered,
for example certain Latin1 characters, then the entire string's
reported length will be the number of bytes, even if valid UTF-8
characters exist, and subscripting operations will behave accordingly.
If the entire string is valid UTF-8 (and ASCII is a subset of
'valid UTF-8'), then the reported length will be the number of decoded
characters.

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
* Hexadecimal escapes ``\xNN`` must contain exactly two hexadecimal digits.

Unsupported backslash escape sequences will result in a SyntaxError,
and the script will not be executed.

Backslash escapes which attempt to insert a nulchar, such as ``"\x00"``
or ``"\u0000"``, will be rejected.

Unicode Escapes
```````````````

String literals may contain Unicode characters, either encoded in
UTF-8, or as ASCII representations using backslash-U escapes.
The following are all valid ways to express the Greek letter β:

================== ================
Direct UTF-8       ``"β"``
lowercase u escape ``"\u03b2"``
Uppercase U escape ``"\U000003b2"``
================== ================

For the ``u`` and ``U`` escape, EvilCandy will encode the character as
UTF-8 internally.  Unicode values between U+0001 and U+10FFFF are
supported, except for certain surrogate pairs between U+D800 and U+DFFF.

Unlike lower-level languages, adjacent hex and octal escapes do not
concatenate to form a single Unicode point.  In C, ``\xCE\xB2`` or
``\316\262`` could also make the Greek letter β, but in EvilCandy,
they form two separate Unicode points at U+00CE and U+00B2.
Conversion from one to another is possible using bytes data types.

Quotation Escapes
`````````````````

Quotation marks in string literals may either be backslash-escaped or
wrapped by the other kind of valid quote.
The following two lines are syntactically identical:

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

Bytes Literals
--------------

Bytes literals express the **bytes** data type, which stores an octet
sequence whose unsigned values range from 0 to 255.

These tokens begin with ``b`` or ``B`` followed immediately by a
single or double quote, with no white-space between.

Bytes literals differ from string literals in some other important ways:

#. bytes literals must contain only printable ASCII characters;
   any non-ASCII or non-printable value must use backslash escape
   sequences.
#. Backslash escapes evaluating to zero are permitted, even in the
   middle of the token.
#. Unicode escapes are not permitted.
#. Any hex or octal backslash escapes evaluating to a value greater than
   255 are not permitted.

An example bytes literal:

.. code::

        b'a\xff\033\000b'

This expresses a byte array whose elements are, in order 97
(ASCII ``'a'``), 255 (``ff`` hex), 27 (``033`` octal), 0,
and 98 (ASCII ``'b'``).

Numerical Tokens
----------------

EvilCandy interprets three kinds of numbers--integer, float, and complex.

Literal expressions of these numbers follow the convention used by C,
except that you must not use numerical suffixes for integers or floats
Write ``12``, not ``12ul``; write ``12.0``, not ``12f``.  For complex
numbers, use only ``j`` or ``J`` as a numerical suffix for the imaginary
portion.

The following table demonstrates various ways to express the number 12:

=========== ===========================
**integer expressions**
---------------------------------------
Decimal     ``12``
Hexadecimal ``0xC``
Octal       ``014`` [#]_
Binary      ``0b1100``
----------- ---------------------------
**float expressions**
---------------------------------------
Decimal     ``12.``, ``12.000``, *etc.*
Exponential ``12e1``, ``1.2e2``, *etc.*
----------- ---------------------------
**complex expressions**
---------------------------------------
Decimal     ``12 + 0j`` [#]_
Exponential ``12e1 + 0j``
=========== ===========================

Specific rules of numerical interpretation:

* A prefix of '0x' or '0X' indicates a number in base 16 (hexadecimal),
  and it will be interpreted as an integer.
* A prefix of '0b' or '0B' indicates a number in base 2 (binary),
  and it will be interpreted as an integer.
* A number that has a period or an 'E' or 'e' at a position appropriate
  for an exponent indicates a base 10 float.
* A number with an upper or lower-case ``j`` will be interpreted as an
  imaginary component of a complex number, whose value will be
  interpreted as a base 10 float [#]_.
* A number beginning with a '0' otherwise indicates a base 8 (octal)
  number, and it will be interpreted as an integer.
* The remaining valid numerical representations--those beginning with
  '1' through '9' and continuing with '0' through '9'--indicate a base 10
  (decimal) number, and they will be interpreted as an integer.

Keyword Tokens
--------------

The following keywords are reserved for EvilCandy:

**Table 1**

============ ============ =============
Reserved Keywords
=======================================
``break``    ``continue`` ``catch``
``do``       ``else``     ``let``
``false``    ``finally``  ``for``
``function`` ``global``   ``has``
``if``       ``null``     ``return``
``this``     ``throw``    ``true``
``try``      ``while``
============ ============ =============

All keywords in EvilCandy are case-sensitive.  None are "soft"; you
cannot, for example, declare a variable named ``function``.  (Built-in
functions might be thought of as "soft keywords", however, since they
exist as global variables; local variables take precedence over global
variables.)

Operator Tokens
---------------

The following is a table of operators.  Except where obvious, they
will be discussed in later sections where appropriate.

+---------+-------------------------+
| Operator| Operation               |
+=========+=========================+
| *Binary Operators* A OPERATOR B   |
+---------+-------------------------+
| ``+``   | add, concatenation      |
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
| ``&``   | bitwise AND             |
+---------+-------------------------+
| ``|``   | bitwise OR, union       |
+---------+-------------------------+
| ``<<``  | bitwise left shift      |
+---------+-------------------------+
| ``>>``  | bitwise right shift     |
+---------+-------------------------+
| ``^``   | bitwise XOR             |
+---------+-------------------------+
| *Unary Operators* (before operand)|
+---------+-------------------------+
| ``!``   | logical NOT             |
+---------+-------------------------+
| ``~``   | bitwise NOT             |
+---------+-------------------------+
| ``-``   | negate (multiply by -1) |
+---------+-------------------------+
| *Unary Operators* (after operand) |
+---------+-------------------------+
| ``++``  | Increment by one        |
+---------+-------------------------+
| ``--``  | Decrement by one        |
+---------+-------------------------+
| *Ternary Operators* [#]_          |
+---------+-------------------------+
| ``?``   |                         |
+---------+-------------------------+
| ``:``   |                         |
+---------+-------------------------+
| *Assignment Operators*            |
+---------+-------------------------+
| ``=``   | res = rval              |
+---------+-------------------------+
| ``+=``  | res = lval ``+`` rval   |
+---------+-------------------------+
| ``-=``  | res = lval ``-`` rval   |
+---------+-------------------------+
| ``*=``  | res = lval ``*`` rval   |
+---------+-------------------------+
| ``/=``  | res = lval ``/`` rval   |
+---------+-------------------------+
| ``%=``  | res = lval ``%`` rval   |
+---------+-------------------------+
| ``&=``  | res = lval ``&`` rval   |
+---------+-------------------------+
| ``|=``  | res = lval ``|`` rval   |
+---------+-------------------------+
| ``<<=`` | res = lval ``<<`` rval  |
+---------+-------------------------+
| ``>>=`` | res = lval ``>>`` rval  |
+---------+-------------------------+
| ``^=``  | res = lval ``^`` rval   |
+---------+-------------------------+
| *Boolean operators*               |
+---------+-------------------------+
| ``==``  | Equals                  |
+---------+-------------------------+
| ``!=``  | Not equal to            |
+---------+-------------------------+
| ``<=``  | Less than or equal to   |
+---------+-------------------------+
| ``>=``  | Greater than or equal to|
+---------+-------------------------+
| ``<``   | Less than               |
+---------+-------------------------+
| ``>``   | Greater than            |
+---------+-------------------------+
| ``has`` | Contains as an element  |
+---------+-------------------------+


Notes
-----

.. [#]

        The ``#`` comment is intended to permit the shebang syntax for a
        script's first line, that is: ``#!/usr/bin/env evilcandy``

.. [#]
        The Python-style ``0o`` prefix for an octal number is not
        supported in this version.  It may be added in the future.

.. [#]
        There is no need to use decimals to "force" a complex number's
        components to be stored as floating-point values.  The 'j' suffix
        does that sufficiently enough

.. [#]
        Currently, there is no literal expression for a full real/complex
        value pair in a complex number.  An expression like ``1 + 1j``
        will actually be interpreted as two numbers: the integer 1 and
        the complex number (0 + 1j).  The addition will take place during
        runtime to convert the expression into a single complex number.
        Syntactically this is all the same thing, but speed improvements
        can be made in the future.

.. [#]
        ie. The C-like ternary operator, where ``a ? b : c`` evaluates
        to ``b`` if ``a`` is true or ``c`` if ``a`` is false.

