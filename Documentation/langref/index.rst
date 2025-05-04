.. _langref_index:

EvilCandy Reference
===================

.. admonition:: Update May 2025

   This document specifies a language whose implementation and specification
   are both under development and may change.
   Consider this to be a preliminary draft.

This document details the syntax, semantics, and other brass tacks of
EvilCandy.  For a tutorial, see :ref:`tutorial_index`.

The organization of this document was guided by the well-written appendix
to Kernighan and Ritchie's *The C Programming Language* Second Edition. I
think it's well-suited for this since, as different as EvilCandy is from
C, it still largely derives its core semantics (by way of JavaScript) from
that language.  Besides, the interpreter itself was written in C.

This is not a formal--definitely not a *rigorous*--language specification.
I don't use any eggheaded grammar syntax in this document.  (I'm an
engineer, not a computer scientist.  I *do* things.)
The target reader is someone who has read the tutorial and messed around
with the program a bit and wants a more thorough reference to guide them
through the corner cases of the language.

This document concerns the language itself, not the library.
For that, see :ref:`libref_index`.

.. toctree::
   :numbered:

   filespec.rst
   tokens.rst
   data.rst


..
..   conventions.rst
..   filespec.rst
..   tokens.rst
..   data.rst
..   conversions.rst
..   expressions.rst
..   declarations.rst
..   statements.rst
..   scope.rst
..   paths.rst

