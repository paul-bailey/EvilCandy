Scope of Variables
==================

.. highlight:: evc-console

Global Variables
----------------

Global variables are declared with ``global``. They are visible to
every part of the program, in every scope.  For this reason, ``global``
should be used sparingly.  Global visibility was mainly intended to let
built-in functions like ``print()`` and ``abs()`` be visible everywhere.

Session Variables
-----------------

Session variables include any variables declared with ``let`` at the top-level
scope during an interactive session.  "Top-level" in this case means
outside of *any* nested scope—function, flow-control, or block::

  evc> let x = 1;       // Session scope
  evc> {
   ...     let y = 2;   // Not session scope
   ... }

Session-scope variables can be thought of as semi-global.  They are
visible everywhere in the interactive session (and thus closures are
not created if a nested function refers back to them), but they are
*not* visible to an imported script.

Local Variables
---------------

Local variables are all those variables declared with ``let`` which are
not session variables.  This is different from some programming languages.
In EvilCandy, scripts are thought of as functions, and any function in
the script is thought of as a nested function.  So any function which
refers to a file-scope variable will create a closure.

A variable created within a function is never visible to the wrapping
code::

  evc> function myfunc(x) {
   ...    return x;
   ... }
  evc> print(x);
  [EvilCandy] NameError Symbol 'x' not found

The same is true for variables declared inside a program-flow statement::

  evc> {
   ...    let x = 1;
   ... }
  evc> print(x);
  [EvilCandy] NameError Symbol 'x' not found

A local variable overrides variables declared in a parent or global scope::

  evc> let x = 1;
  evc> function foo() {
   ...    let x = 2;
   ...    print(x);
   ... }
  evc> foo();  // will print inner x, not outer
  2

.. note::

   While variables in a descendant function can be declared to overload
   the variable in the parent function, the same is not true for different
   scopes of the same function.  This is possible:

   .. code-block:: evilcandy
      :class: example-good

        function foo() {
            {
                let x = 1;
                ...
            }
            {
                let x = 2; // declared AFTER above scope was destroyed
                ...
            }
        }

   but this is *not* possible:

   .. code-block:: evilcandy
      :class: example-bad

        function foo() {
            let x = 1;
            ...
            {
                // WON'T WORK! x is still in scope in the same function
                let x = 2;
                ...
            }
        }

   This is an "alpha" limitation, which a future version of EvilCandy
   may fix later.

Leaving Scope
-------------

.. admonition:: TODO

   Am I dumbing this down too much? Do programmers need to have explained
   to them the difference between an object and the name associated with
   it?  Am I not being technical enough by saying "variable" instead of
   "name" and "object"?

Global variables never leave scope.

When local variables leave scope, they are typically garbage-collected.
An exception is any datum which a function's return value still
references, for example a closure item.  This data will remain in
memory until that return value is also destroyed.

Resetting a Variable
--------------------

The keyword ``delete`` cannot delete variables the way it can delete a
dictionary or list item.  The variable's name will remain visible until
program flow leaves scope.  ``delete`` can, however, "reset" the variable by
replacing it with ``null``.  This may be useful if you are to remain in
scope for a while but no longer need a certain variable, whose data could
be quite large at times::

  evc> let x = list(range(1000));
  evc> delete x;
  evc> print(x);
  null


