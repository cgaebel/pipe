pipe.c
=============

- brings concurrency to C
- hopefully will spill over into other languages (C is universal and
  easy to interop with)
- building block of larger frameworks (message passing, actor model, etc)
- simple, fast, re-entrant
- Lightweight (weighs in under 1000 LOC)
- portable (relies on pthreads, but can be easily modified)

C has historically had minimal support for multithreading, and even less
for concurrency. *pipe.c* aims to be the first step in fixing that
that.

Technical Details
------------------

License, cross-platform-ness, standard conformance, etc.

Common Design Patterns
-----------------------

API Overview
-----------------

Contributing
----------------

Future Work
--------------------

Using pipe as a framework for a bigger concurrency framework, possibly being a vital building block in a message-passing system.
