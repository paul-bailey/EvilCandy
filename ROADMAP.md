## Pre-Alpha Checklist

### Modules/Functionality

- [ ] json
- [ ] regular expressions
  - need r-string expression type
- [ ] time/datetime
- [ ] `sys.argv`
- [ ] `sys.env` (`.path`, etc)
- [ ] os/posix
  -[ ] a way to spawn child processes
  -[ ] pipe/socketpair/dup/redirection
  -[ ] chdir/cwd
  -[ ] path parsing
- [ ] finish math, io, socket libraries with minimum features
- [ ] `input()` function

### Awkward Things To Solve

- [ ] Define behavior of `dir(SomeClass)` vs `dir(some_instance)`.
- [ ] Final decision on property types.
  - must be used in built-ins
  - maybe too slow and complicated for user-defined types
- [ ] Final decision on globals
  - vm.locals already kind of exists as global.
- [ ] cleaned up `demos` folder
- [ ] Final word on `for-else` statements

### Internals

- [ ] binary serializer
- [ ] user-defined classes have ability to override "dunder" methods
- plan for automated testing (devOps, CI/CD, or such)
- [ ] prevent duplicate/cyclic loads

### Check-it-twice stuff

- [ ] Make sure `int` and `size_t` are all properly declared
      w/r/t dictionary/array-ish objects

### Misc

- [ ] tutorial complete and accurate

## Post-Alpha

### Modules/features

- cmath library
- fill in all the built-in class methods
- support double-star in function call
- support comprehensions
- something like bytearray
- self-documentation in classes/functions... docstrings
- support nested unpacking, e.g. `(a(b,c))=x`

### Awkward Things To Solve

- Change super() so that it's a built-in function returning a class
  instance.  Do not leave it as a soft keyword.
- Gracefully handle recursion error; do not abort
- Add scope tracking/shadowing, rather than let all variables
  sit on the stack until function leaves.
- Add ClassType object for each built-in `struct type_t` so that
  users can do things like `class MyClass(float) {...`
- Dot-naming duality for dictionaries is awkward.

### Internals

- slots instead of dict lookups
- interrupt handlers for things like EINTR

### Misc

#### Long-Term Goals

- static typing
- `switch` or `match` statements

#### Wish List

- packing library, sim. to Python `struct`
- labelled statements
- parse UnicodeData.txt for Unicode ver. of `ctype.h`
- matrix/tensor module for AI stuff
- encryption library hooks for blockchain stuff

#### Idea Bucket

- `myclass.func(myinst)` <-> `myinst.func()`
- register machine instead of stack machine
