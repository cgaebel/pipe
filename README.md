pipe.c
=============

- brings concurrency to C
- hopefully will spill over into other languages (C is universal and
  easy to interop with)
- building block of larger frameworks (message passing, actor model, etc)
- simple, fast, re-entrant
- Lightweight (weighs in under 1000 LOC)
- portable (relies on pthreads, but can be easily modified)
- analogous to unix pipes

C has historically had minimal support for multithreading, and even less
for concurrency. *pipe.c* aims to be the first step in fixing that
that.

Compatibility
--------------

*pipe.c* has been tested on...

GNU/Linux i386
GNU/Linux x86\_64

Windows 7 x64 (Cross-compiled from GNU/Linux with mingw64)
Windows 7 (Compiled with mingw)

However, there's no reason it shouldn't work on any of the following
platforms. If you have successfully tested it on any of these, feel free
to let me know by dropping me an [[email:cg.wowus.cg@gmail.com]].

Any platform which supports pthreads (GNU/Linux, BSD, Mac)
Microsoft Windows NT -> Microsoft Windows 7

If you _must_ use Windows, try to only support windows Vista and
onwards. It simplifies the logic a bit and leads to a much faster
implementation of condition variables.

Supported Compilers:

Any compiler with C99 support, however, *pipe.c* has been tested with...

gcc 4.5.2
clang 2.8
icc 12.0

NOTE: MSVC is a piece of shit (aka, it can only handle C89). Therefore,
it is not supported. Any patches adding support for it will be ignored,
since they will prevent me from using C99 features in future updates.
Feel free to fork though!

Technical Details
------------------

License, cross-platform-ness, standard conformance, etc.

Common Design Patterns
-----------------------

Speed Tweaking
---------------

API Overview
-----------------

Contributing
----------------

Future Work
--------------------

Using pipe as a framework for a bigger concurrency framework, possibly being a vital building block in a message-passing system.
