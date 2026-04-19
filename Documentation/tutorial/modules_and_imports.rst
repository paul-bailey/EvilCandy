Modules and Imports
===================

Modules
-------

Here I should distinguish between "module" and "script".
They are technically the same thing, but for this discussion I will make
an artificial-but-useful distinction.  A *script* is the main file being
executed by EvilCandy (if it is not running in interactive mode).
A *module* is the file being *imported* by that top-level script, as well
as any file being imported by the module.

There are two good reasons to not write modules which have side
effects, like ``print()`` or ``global``.  One is conceptual:
modules should provide libraries and classes to the caller without making
unpredictable changes to the global state [#]_.  The other reason is that
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

The Import Statement
--------------------

Importing is done using the statement template ``import module_name;``, for
example::

  import math;

This example looks for a file named "math.evc" in the import path,
executes it (if it is not already cached), and assigns its return value
to a variable named ``math``.  This ``math`` variable will be local to
the scope in which the statement was executed.  The module name must be
a valid identifier (as all the standard library script names are valid
identifiers, minus their ".evc" suffixes).

The ``import`` statement declares a new variable, so the module name may
not have been a preexisting variable.  The following example is incorrect:

.. code-block:: evc-console
   :class: example-bad

   evc> let math;
   evc> import math;
   [EvilCandy] SyntaxError Redefining variable ('math')

If you prefer a different variable, you may add the clause ``as NAME``,
for example::

   import math as my_math;

The file being searched for is still "math.evc", but the variable being
declared is now ``my_math``, not ``math``.

The Import Path
---------------

The directories searched in the import path can be found in the list
``sys['import_path']``.  (``sys`` does not need to be imported.  It is in
the global namespace at startup by default.)  Paths at lower indices are
searched first.  At startup, this list is populated with two directories:
the working directory, and the directory where installed library scripts
exist.  An additional list, ``sys['breadcrumbs']``, lists the current import
depth, so that nested imports can be relative to the directory of the
imported script rather than from the current working directory.


``sys['breadcrumbs']`` should never be touched.  Removing the original
fields of ``sys['import_path']`` is also strongly discouraged.  If
you must edit the import path by adding a directory, use the list
``insert`` and ``append`` methods.  But this is still hazardous,
especially if you want to remove the additions later.  If the purpose is
just to import a specific module in an unusual directory, there is a
much better way...

The ``importfile()`` Function
-----------------------------

Rather than manipulating ``sys['import_path']``, a user can instead use
the built-in ``importfile()`` function, which takes a string as its
parameter::

   let my_module = importfile('../my_module.evc');

This can be handy for ad hoc modules, or for testing new modules without
having to install them or put them in the working directory.  This has
the added benefit that you can import files whose names are not valid
identifiers.

If the path begins with only the name (I.E. no leading absolute-
or relative-path indicators like ``.`` or ``/``), then the same path
search will be conducted as with the normal import statement.  This will
also run through the same module de-duplication logic, so do not use
``importfile()`` as a way to force a duplicate load [#]_.

Importing Large Modules
-----------------------

In its current (pre)alpha stage of development, EvilCandy does not
support directory names as the target of an ``import`` statement.
Adding this feature is certainly on the longer-term to-do list.

Notes
-----

.. [#]

   While Python scripts can use a test like ``__name__ == 'main'``
   to determine whether to run a ``main()`` function containing side
   effects, EvilCandy does not yet have that in its current (pre)alpha stage
   of development.

.. [#]

   Implementation note: I had considered permitting the passing of
   arguments to imports, but this confuses the uniqueness of a module,
   making it harder to determine whether to cache the result or not.

.. [#]

   Adding a built-in function that explicitly reloads a module is on
   the development to-do list.
