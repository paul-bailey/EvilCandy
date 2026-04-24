## Pre-Alpha Checklist

### Core Language (Must Be Stable)

* [x] Variables, assignment, and expressions behave correctly
* [x] Functions (definition + calls) work reliably
* [x] Control flow (`if`, `while`, `for`) is stable
* [x] Classes and instances function correctly (basic usage)
* [x] Method calls and `self` semantics are consistent
* [x] Closures behave correctly (no “nil capture” issues)

---

### Runtime Stability (Critical)

* [ ] Interpreter does **not crash** on malformed input
* [ ] Interpreter does **not crash** on runtime errors
* [ ] Deterministic execution (no undefined behavior / corruption)
* [ ] Stack/frame invariants are enforced (debug assertions OK)

---

### Error Handling & Diagnostics

* [x] Syntax errors report correct location
* [ ] Runtime errors report useful messages
* [x] Stack traces (even minimal) are available
* [ ] Clear distinction between fatal vs recoverable errors
* [ ] Graceful recursion limits (no abort)

---

### Minimal Standard Library

* [ ] Basic `io` (print/output)

  * [ ]Prevent EINTR issues (interrupt handling)
* [x] `input()` function
* [ ] Minimal `sys.argv`
* [ ] Minimal math functionality

---

### Module System

* [x] Basic `import` behavior defined and working
* [x] Modules execute only once
* [x] Prevent duplicate/cyclic loads

---

### Object Model (Minimal but Coherent)

* [ ] User-defined classes can override key “dunder” methods
* [ ] Property behavior defined *well enough for built-ins*

  * [ ] and documented
* [x] Globals behavior defined (even if temporary design)

---

### Testing & Tooling

* [x] Basic automated test suite exists (C and/or language level)
* [ ] Tests cover core language features
* [x] Fuzz testing or randomized input testing (basic)
* [x] Clean build from fresh checkout works

---

### Correctness / Safety Audits

* [ ] No obvious memory safety issues (no leaks or corruption)

---

### Minimal Documentation

* [ ] README reflects actual current behavior
* [ ] “Getting started” instructions work
* [ ] Known limitations are documented

---

## Post-Alpha

### Expanded Standard Library

* json module
* regular expressions

  * [x] `r-string` type
* time/datetime
* Minimal `sys.env` (basic environment access)
* full `os/posix` support:

  * a way to spawn a child process
  * pipes/socketpair/dup/redirection
  * path handling
  * chdir/cwd
* socket library expansion
* cmath library
* bytearray or equivalent

---

### Language Features

* comprehensions
* double-star (`**`) in function calls
* nested unpacking `(a(b,c)) = x`
* docstrings / self-documentation
* fill in all built-in class methods

---

### Language Design Decisions (Finalize After Feedback)

* Final property model
* Final globals model
* `dir(class)` vs `dir(instance)` behavior
* `super()` redesign (function vs keyword)

---

### Runtime Improvements

* Scope tracking / local variable lifetime (no full-frame retention)
* ClassType objects for built-ins

---

### Internals & Performance

* Binary serializer (bytecode format)
* Slots instead of dict lookups
* VM optimizations
* Memory layout improvements

---

### Platform & Ecosystem

* Cross-platform support (Linux, Windows)
* External module/plugin system (C integration)
* Improved REPL behavior

---

### Misc

* Clean up demos
* Complete tutorial

---

## Long-Term Goals

* Static typing (optional or hybrid)
* `switch` / `match` statements

---

## Wish List

* packing library (like Python `struct`)
* labeled statements
* Unicode support via UnicodeData.txt
* matrix/tensor module
* encryption/blockchain hooks
* make integers have arbitrary width

---

## Idea Bucket (Uncommitted)

* `myclass.func(myinst)` ↔ `myinst.func()` symmetry
* register VM instead of stack VM

