Classes and Namespaces
======================

Namespaces
^^^^^^^^^^

Dictionaries are technically sufficient to store a namespace, but
nobody wants to type::

  y = math['cos'](x);

An alternative form of dictionary is a *namespace*, which treats its
contents as attributes rather than dictionary items.  The contents
are typically functions, but they could also contain other forms of
data.

The expression takes the form::

  namespace {
        .identifier1 = expression1,
        .identifier2 = expression2,
        /* ... */
  }

or less abstractly::

  namespace {
        .do_this = function () { /* code which does this */ },
        .do_that = function () { /* code which does that */ },
  }

As a statement, a namespace may be named or unnamed::

  namespace MyNameSpace { /* namespace items */ }

  // note the required semicolon in the anonymous case
  let MyNameSpace = namespace { /* namespace items */ };

Namespace attributes can be de-referenced using the familiar dot
notation::

  x = MyNameSpace.do_this();

In literal expressions, namespace item names must be identifiers.
They may not be strings.

.. code-block::
   :class: example-bad

   // Will not work! Names must be identifiers, not strings
   namespace MyNameSpace { ."nøn-åscii" = expression }

A namespace could store a non-identifier name, however, using
the built-in function ``setattr()``::

   setattr(MyNameSpace, "nøn-åscii", expression);

These attributes can only be retrieved with the sibling function
``getattr``::

  value = getattr(MyNameSpace, "nøn-åscii");

This will work for any hashable keys.  (In this case ``"nøn-åscii"``
is a hashable key.)  It will not work for keys which are mutable,
like lists, dictionaries, or tuples containing mutable objects.

Classes
^^^^^^^

EvilCandy is intended to be an object-oriented programming language.
So far we have looked at built-in object classes, like strings,
arrays, numbers, and dictionaries.  This section looks at user-defined
classes.

Class Expressions
-----------------

A class expression has the following minimum form::

   class () {}

At the moment it looks the same as a function expression except with a
different keyword, but as we expand it with methods and base classes, it
will begin to look very different.

Classes may be anonymous or named::

  // anonymous class declaration
  let MyClass = class() {};

  // named class declaration
  class MyClass() {}

As with functions, the named-class declaration is functionally equivalent
to the anonymous-class declaration insofar as it assigns the class to
a variable, named ``MyClass`` in this example; but the difference is
important: the named-class declaration also stores the name with the
class, making named classes far more useful than anonymous classes when
debugging.

Consider it bad programming practice to ever reassign a variable that was
assigned a class, especially if it was done so using the named-class
form.  EvilCandy currently permits it, but a future version might throw
an exception.

Class Methods
-------------

Class methods are typically defined within the bracess ``{}`` at the end
of the ``class`` expression, using the same syntax as a namespace.

Unlike regular functions and functions contained in a namespace, class
methods must always take at least one argument.  This first argument is
the instantiation of the class to operate upon.  Python programmers
should already be familiar with this format; by convention they name
the first argument ``self``, and so will this tutorial's examples.
To JavaScript programmers, this argument is analogous to the keyword
``this``, which does not exist in EvilCandy.

At instantiation time, the dunder method ``__init__`` is called, which
does nothing unless the user overloads it with their own ``__init__``
method.

Here is an example of a simple class::

  class Animal() {
      .__init__ = function(self, name) {
          self.name = name;
      },

      .speak = function(self) {
          return f'{self.name} makes a generic sound';
      }
  }

The ``__init__`` function creates a new class attribute ``name``, and
sets it to the name provided by the argument.  If the argument was
optional, you could save a default value (or, in some applications, a
start-up value) along with the methods::

  class Animal() {
      .name = 'default_name',
      .__init__ = function(self, *args) {
          if (length(args) > 0)
              self.name = args[0];
          ...

This is safe, because any modifications to an instantiation's
attributes are saved with the instantiation itself, not the class.
So ``'default_name'`` is preserved for later instantiations.

You would instantiate this class by calling it as if it was a
function, passing the arguments that will go to the ``__init__``
function:

.. code-block:: evc-console

   evc> let tom = Animal("Tom");
   evc> tom.speak();
   Tom makes a generic sound

Note that the instantiation (the ``self`` argument) is not explicitly
passed in the function call.  This is the main difference between
regular functions (including those in namespaces) and class methods.
EvilCandy inserts the instantiation argument into the first argument
slot by default.

Class Inheritance
-----------------

The parentheses following the ``class`` keyword are actually a tuple
containing inherited base classes.  The above examples show an empty
tuple, meaning that they do not inherit any base classes.  This is one
of those places in EvilCandy code where parentheses are required around
a tuple.

Let's expand on the ``Animal`` example above, by creating some new
classes which inherit it, ``Dog`` and ``Cat``::

  class Dog(Animal) {
      .speak = function(self) {
          return f'{self.name} barks: Woof! Woof!';
      }
  }

  class Cat(Animal) {
      .speak = function(self) {
          return f'{self.name} meows: Meow!';
      }
  }

These are *subclasses* of the base class ``Animal``.  Since ``Animal``
already has an ``__init__`` method, and since these subclasses do not
overload it with their own ``__init__`` methods, the original
``__init__`` function will be called, requiring the same argument
protocol.   The ``speak`` methods, on the other hand, are overloaded.

.. code-block:: evc-console

    evc> let dog_instance = Dog('Rocky');
    evc> let cat_instance = Cat('Tom');
    evc> print(dog_instance.speak());
    Rocky barks: Woof! Woof!
    evc> print(cat_instance.speak());
    Tom meows: Meow!

Inheritance Using ``super()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. admonition:: Warning

   The ``super()`` description below is correct.  However,
   while EvilCandy is in pre-alpha, do not make assumptions
   which base class is being used for "super", in the case of
   multiple inheritance.  Pre-alpha EvilCandy has not worked
   out all the method-resolution-order issues yet.

Sometimes, instead of completely overloading an inherited method,
you want to extend it instead.  Let's look at another example of
inheritance::

  class Person() {
      .__init__ = function(self, fname, lname) {
          self.firstname = fname;
          self.lastname = lname;
      },
      .display_name = function(self) {
          print(f'Name: {self.firstname} {self.lastname}');
      }
  }

  class Student(Person) {
      .__init__ = function (self, fname, lname, graduation_year) {
          super().__init__(fname, lname);
          self.graduation_year = graduation_year;
      },
      .display_details = function(self) {
          self.display_name();
          print(f'Graduation Year: {self.graduation_year}');
      }
  }

In this example, the ``__init__`` method of ``Student`` *could*
initialize ``firstname`` and ``lastname`` itself, but instead it follows
the don't-repeat-yourself principle by calling the parent class's
``__init__`` method.  This is done using the special form
``super().method_name(...)``.  For now, EvilCandy recognizes ``super``
specially only when it appears at the start of an expression and is
immediately followed by ``().`` and a method name. In other positions,
``super`` is still an ordinary identifier.

Class Private Fields
--------------------

.. note::

   This feature of EvilCandy is still minimally developed and tested.
   There are also many corner-cases which have not yet been worked out,
   so consider this section to be only *mostly* true.

A class's private fields are declared like so::

   class MyClass() {
       .some_public_method = function (self) {
           ...
       },
       private .some_private_method = function(self) {
           ...
       },
       ...
   }

When a class method is declared with ``private``, it can be accessed
only from within methods of the same class.
This works for other kinds of class data as well.

There is currently no other way to assign privacy.  For example, you
cannot create a new attribute during an ``__init__`` method and declare
the attribute as "private".

:TODO: Dicuss __str__, other dunders when they become implemented.

