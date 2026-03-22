.. _tutorial_index:

==================
EvilCandy tutorial
==================

.. admonition:: Update May 2025

   EvilCandy is still unstable, both its implementation and its
   specification.  As for this tutorial, about a third of it is true
   right now, another third of it was true before but no longer, and
   still another third is yet to be true.

.. note::

   If you're reading the raw version of this document, you may notice
   that it uses the syntax highlighting for JavaScript everywhere.  This
   is because I have not yet written a whole new syntax highlighting
   theme for Pygments, nor am I sure it's even possible for it to work
   for you if I do.  But this is *cosmetic*.  EvilCandy and JavaScript
   have *fundamentally incompatible* differences, which this document
   should clear up.

To begin with, EvilCandy is syntactically very similar to JavaScript,
so it helps to know a lot about JavaScript going in to this.  On the
other hand, EvilCandy's syntax was *inspired* by JavaScript; it does
not actively try to emulate it.  Some ways of programming things in
JavaScript will not work if done the same way in EvilCandy.  For that
reason, this tutorial will make plenty of "compared to JavaScript" sorts
of references or examples.  The other major programming language that
inspired EvilCandy is Python, especially with respect to many of the
data types and the underlying implementation of the virtual machine.
(Confession time: Whenever I got stumped how best to implement a certain
language feature, I simply used Python's ``dis`` module to see what
instructions they used, and said "yeah, let's do that".)

This tutorial is intended to just be a walk-through,
not a throrough catalogue of the language's features.

.. toctree::
   :numbered:

   getting_started.rst

