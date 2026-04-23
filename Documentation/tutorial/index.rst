.. _tutorial_index:

==================
EvilCandy tutorial
==================

To begin with, EvilCandy is syntactically very similar to JavaScript,
so it helps to know a lot about JavaScript going in to this.  On the
other hand, EvilCandy's syntax was *inspired* by JavaScript; it does
not actively try to emulate it.  Some ways of programming things in
JavaScript will not work if done the same way in EvilCandy.  For that
reason, this tutorial will make plenty of "compared to JavaScript" sorts
of references or examples.  The other major programming language that
inspired EvilCandy is Python, especially with respect to many of the
data types and the underlying implementation of the virtual machine [#]_.

This tutorial is intended to just be a walk-through,
not a throrough catalogue of the language's features.

.. toctree::
   :numbered:

   getting_started.rst
   program_flow.rst
   data_types.rst
   scope.rst
   classes_and_namespaces.rst
   modules_and_imports.rst

.. [#]

   Confession time: Whenever I got stumped how best to implement a certain
   language feature, I simply used Python's ``dis`` module to see what
   instructions they used, and said "yeah, let's do that".

