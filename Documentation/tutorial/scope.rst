Scope of Variables
==================

.. highlight:: evc-console

All variables declared with ``global`` are visible to every part of a
the program, in every scope.  For this reason, ``global`` should be
used sparingly.

The scope of variables declared with ``let`` depends on where it is
declared.  In interactive mode, a variable at the top-level is semi-global;
it is visible to any code typed by the user, but it is not visible to
scripts being loaded.  In script mode, such variables behave more like
local variables in a wrapper function—a function which refers to them
will result in the creation of a closure—but the effect is mostly the same.

A variable created within a function is not visible to the wrapping code::

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

A symbol declared with ``let`` in a script is invisible outside the script.
(We'll get to scripts later, but a placeholder explanation is that scripts
are treated like functions that return a value; the "name" of that value
is assigned to the variable that stores the import result.)

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
dictionary or list item.  It can, however, "reset" the variable by
replacing it with ``null``.  This may be useful if you are to remain in
scope for a while but no longer need a certain variable, whose data could
be quite large at times::

  evc> let x = list(range(1000));
  evc> delete x;
  evc> print(x);
  null


