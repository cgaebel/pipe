pipe.c
=============

C has historically had minimal support for multithreading, and even less
for concurrency. pthreads was the first step to bringing support for
threading constructs to C, and it has served us well. However, we now
have a multitude of concurrent paradigms available to us. Very few of
these map nicely onto heavyweight threads and locking. For example, the
Actor model has no explicit locking at all, and is the logical extreme
of concurrent system design.

Let's imagine a classic problem. We want to write a simple download
manager. First, we need to get the data from a slow internet connection,
then dump that data to a slow hard disk. In classic C, waiting for one
means you can't work on the other! This is unacceptable since you're
dealing with two potentially very slow components. When the disk starts
spinning up due to unknown causes, you don't want your transfer to
pause!

To solve this problem, we can use a pipe! We create two threads. One for
receiving the data from the network, and one for dumping it to disk.
Whenever data is received, it will be pushed into the pipe instead of
directly on disk. In the disk thread, it will continually read from the
pipe, dumping any data it sees onto the disk. Since the two processes
are decoupled by a fast pipe (which is implemented with simple memcpy's),
when one thread lags behind, the other one does not need to wait. If the
producer is lagging, the consumer will block until there is data. If the
consumer is lagging, requests will just pile up in the queue to be
handled "eventually". If memory usage becomes a problem, we can limit
the size of the pipe to block the producer when things get hectic.

Then again, unix pipes can do that too. Why bother with little old
*pipe.c*? Because it can do so much more than that! No matter how many
producers or consumers you have, pipe will behave predictably (although
not necessarily fairly). Therefore, you can use pipe for serialization,
pipelining tasks, parallel processing, futures, multiplexing, and so
much more. It does this very fast, in such a way where an overwhelming
majority of the time is spent waiting for locks, memcpy-ing, and
malloc-ing. All this in under 1000 lines of C!

My true hope is that this library will spur development of concurrent
systems in all languages. Since C is so pervasive, I hope to see ideas
leaking into other languages and libraries, hopefully becoming a vital
building block of the next generation of concurrent programming.

Compatibility
--------------

*pipe.c* has been tested on...

 * GNU/Linux i386
 * GNU/Linux x86-64
 * Windows 7 x64 (Cross-compiled from GNU/Linux with mingw64)
 * Windows 7 (Compiled with mingw)

However, there's no reason it shouldn't work on any of the following
platforms. If you have successfully tested it on any of these, feel free
to let me know by dropping me an [[email:cg.wowus.cg@gmail.com]].

 * Any platform which supports pthreads (GNU/Linux, BSD, Mac)
 * Microsoft Windows NT -> Microsoft Windows 7

If you _must_ use Windows, try to only support windows Vista and
onwards. It simplifies the logic a bit and leads to a much faster
implementation of condition variables.

Supported Compilers:

Any compiler with C99 support, however, *pipe.c* has been tested with...

gcc 4.5.2
gcc 4.2.1
llvm-gcc 4.2.1
clang 2.8
icc 12.0
mingw 4.6.0

NOTE: MSVC is a piece of shit (aka, it can only handle C89). Therefore,
it is not supported. Any patches adding support for it will be ignored,
since they will prevent me from using C99 features in future updates.
Feel free to fork though!

The headers are supported by any compiler with ANSI C support and don't
do anything fancy. Therefore, if you want to use pipe.c with a compiler
such as msvc, you can compile the .c file with mingw and the rest of
your program with msvc, and link the all together without a hitch.

License
------------------

<pre>
The MIT License
Copyright (c) 2011 Clark Gaebel <cg.wowus.cg@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
</pre>

Speed Tweaking
---------------

There are a few things you can do to get the most speed out of the pipe.

 # Use a vectorizing compiler, and an SSE-enabled memcpy. It's a lot
   faster, trust me.
 # Tweak `MUTEX_SPINS` in pipe.c. Set it to a low value if you tend to
   push large quantities of data at once, and a high value if you tend to
   push small quantities of data. Feel free to set it to zero to disable
   spin locking altogether and just use the native lock implementation.
 # Turn on your compiler's link-time optimization.
 # Do profile-guided optimization.
 # Large buffers are preferable to small ones, unless your use case
   dictates otherwise. Emptying the pipe frequently tends to be faster
   and use less memory.
 # Avoid limiting the size of the pipe unless you get serious memory
   issues.
 # If you are always pushing predictable amounts of data into the pipe
   (such as 1000 elements at a time), you should consider using
   `pipe_reserve` to keep the pipe from needlessly growing and
   shrinking.
 # If you can combine a whole bunch of elements into a single struct, it
   will be faster to push and pop one large struct at a time rather than
   a push/pop of many small structs.
 # Read through the source code, and find bottlenecks! Don't forget to
   submit changes upstream, so the rest of the world can be in awe of
   your speed-hackery.

API Overview
-----------------

The API is fully documented in pipe.h

Contributing
----------------

This is github! Just fork away and send a pull request. I'll take a look
as soon as I can. You are also always free to drop me an email at
cg.wowus.cg@gmail.com. I won't ignore you. Promise!

Things I'd love to see are speed improvements (especially those which
reduce lock contention), support for more compilers, a better makefile
(the current one is pitiful), and any algorithmic improvements.

Essentially, any patches are welcome which fulfill the goals of pipe:
speed, cross-platform-ness, a simple API, and ease of reading.
