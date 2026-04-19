Modules and Imports
===================

Here I should distinguish between "module" and "script".
They are technically the same thing, but for this discussion I will make
an artificial-but-useful distinction.  A *script* is the main file being
executed by EvilCandy (if it is not running in interactive mode).
A *module* is the file being *imported* by that top-level script, as well
as any file being imported by the module.

There are two good reasons for not writing modules which have side
effects like printing or writing global variables.  One is conceptual:
modules should provide libraries and classes to the caller without making
unpredictable changes to the global namespace.  The other reason is that
modules are only executed once, then they are cached for the remainder of
the program session; this prevents duplicate or cyclic loads.  Therefore
any future import will not have the same side-effects.

EvilCandy does not have a "module" class per-se.  A script, and therefore
a module as well, is thought of as a function [#]_.  Its local variables, even
at file scope, are invisible to the caller.  Thus for a module to be
useful, it must return a value.  Scripts which do not have a top-level
``return`` statement will return ``null`` by default, which is of no use
to the caller.  The most useful return values are namespaces, classes, or
dictionaries.  This return value is the object being cached; future imports
of the same module during the same session will receive this same return
value.

.. note::

   While Python scripts can use a test like ``__name__ == 'main'``
   to determine whether to run a ``main()`` function containing side
   effects, EvilCandy does not yet have that in its current alpha stage
   of development.

:TODO: examples, discuss path and importfile(), how to use nested imports for larg projects

.. [#]

   Implementation note: I had considered permitting the passing of
   arguments to imports, but this confuses the uniqueness of a module,
   making it harder to determine whether to cache the result or not.
