# `nthm` -- Non-preemptive Thread Hierarchy Manager

`Nthm` (pronounced like "anthem") is a C library intended for
applications involving dynamic hierarchies of threads creating other
threads that would be difficult or impossible to plan at compile-time.
It's especially appropriate when communication between threads fits a
pattern of input data being sent to each thread on creation from the
one that creates it and output being sent back to the creating thread
on termination. While enabling some flexibility about synchronization,
the API is simpler than direct use of `pthreads` primitives and might
let some applications avoid explicit locking entirely. In any case,
`nthm` helps you write portable code that puts multiple cores to work
on heterogeneous workloads without taking too deep of a dive into
concurrent programming.

## Usage

It's easy to get started with `nthm`.

* [`nthm_open`](https://gueststar.github.io/nthm_docs/nthm_open.html)
  creates a thread running your code, passes input data to it, and
  returns a pipe.
* [`nthm_read`](https://gueststar.github.io/nthm_docs/nthm_read.html)
  gets output data from a pipe when the created thread finishes
  running.

Your code that opens a pipe doesn't have to close it or do anything
else after reading from it because `nthm` automatically reclaims the
pipe. `Nthm` also keeps track of the pipes created by each thread and
automatically reclaims any unread pipes when the thread exits. The
code you launch in a thread doesn't have to write to a pipe
explicitly. It returns normally as from a function call and the output
it returns passes through the pipe that was opened when it started.

Reading from an individual pipe blocks the reader thread if necessary
until the pipe's thread finishes. Using pipes that way would be not
much different from using ordinary function calls. However, when
multiple threads with varying workloads run concurrently, `nthm` helps
you eliminate unnecessary blocking by reading from their pipes in
whatever order they finish.

* [`nthm_select`](https://gueststar.github.io/nthm_docs/nthm_select.html)
  returns the pipe from the next thread to finish running.

Threads managed by `nthm` are more like cattle than pets (as devops
guys like to say). You can open a large number of pipes without
referring to them individually by name or even storing them in an
array because `nthm_select` knows where to find them.

Sometimes you might decide a thread has been running long enough
already and you'd like it to finish up by approximating its result in
some application-specific way.

* [`nthm_truncate`](https://gueststar.github.io/nthm_docs/nthm_truncate.html)
  tells a specific thread to truncate its output.
* [`nthm_truncate_all`](https://gueststar.github.io/nthm_docs/nthm_truncate_all.html)
  tells all locally created threads to truncate their output.
* [`nthm_truncated`](https://gueststar.github.io/nthm_docs/nthm_truncated.html)
  polled within a worker thread tells it that truncated output has been requested.

You might decide a thread's services are no longer required and kill
it instead of reading from it.

* [`nthm_kill`](https://gueststar.github.io/nthm_docs/nthm_kill.html)
  tells a specific thread to shut itself down and that its output will be ignored.
* [`nthm_kill_all`](https://gueststar.github.io/nthm_docs/nthm_kill_all.html)
  kills all threads created within the currently running thread.
* [`nthm_killed`](https://gueststar.github.io/nthm_docs/nthm_killed.html)
  polled within a thread tells it that it has been killed.

Threads are not killed preemptively. They can take as much time as
needed to shut down cleanly after getting notified (as in releasing
any resources they hold), and the thread that kills them doesn't get
blocked during that time. The usual way for a thread to detect being
killed is by polling `nthm_killed`, but if a thread can't poll
because it's currently blocked trying to read from a pipe of its own,
`nthm` unblocks it and reports the status in a return code from the
read or select call. Although all unread pipes' threads are killed
automatically when the thread that created them exits, the operation
can be invoked explicitly at any time for the sake of memory
management.

One last extra fancy thing you can do is reorganize the thread
hierarchy on the fly.

* [`nthm_untether`](https://gueststar.github.io/nthm_docs/nthm_untether.html)
  makes a thread independent of the thread that created it.
* [`nthm_tether`](https://gueststar.github.io/nthm_docs/nthm_tether.html)
  subjects an untethered thread to the currently running thread.

By untethering a thread, you can send its pipe to any other thread,
even to one not created by `nthm_open`, and read from it in the context
of the receiving thread. An untethered thread is not automatically
killed when the thread that created it exits or calls `nthm_kill_all`,
and its pipe is ignored by `nthm_select`. However, you can make an
untethered thread selectable and jointly killable by tethering it.

For full API documentation, refer to the `nthm` manual pages included
with the installation, which are also accessible
[online](https://gueststar.github.io/nthm_docs/nthm.html) and suitable
for self-hosting. For coding examples, see the test directory.

## Limitations

Although `nthm` makes pipes easy to create and tear down dynamically,
each individual pipe can be used at most once. There's no way to write
to the same pipe more than once, and reading from the same pipe more
than once will probably cause a segfault. If something needs to be
done repeatedly, `nthm` is more conducive to a style of running the
same routine with a fresh pipe each time. If you need something more
complicated than that, `nthm` isn't for you.

`Nthm` doesn't magically make your code thread-safe. If the threads
you create by `nthm_open` never share data or communicate except
through the documented API, and if the only third party libraries they
ever use are thread-safe, then you should be fine. However, if your
code uses global variables or shared data in any form, you'll have to
do your own locking. `Nthm` can be used alongside `pthreads` and other
libraries such as [Concurrency
Kit](https://github.com/concurrencykit/ck).

`Nthm` knows nothing about memory allocated in your code. If you kill
a thread or neglect to read from its pipe, `nthm` cleans up after
itself but any heap-allocated result in the pipe persists. It doesn't
matter that killing is non-preemptive. Although your code can poll
`nthm_killed` and take appropriate action as long as it's still
running, there's no remedy for being killed after exiting. Always read
from any thread that returns a pointer to something it has allocated
on the heap unless you like memory leaks.

## Installation

`Nthm` runs on GNU/Linux and maybe other Unix-like systems with git
and CMake. Optional but highly recommended for better performance are
[mimalloc](https://github.com/microsoft/mimalloc),
[jemalloc](https://github.com/jemalloc/jemalloc) or
[tcmalloc](https://github.com/google/tcmalloc). `Nthm` will be
configured for optional memory and thread safety tests that may take
about half an hour to run if the [Valgrind](https://valgrind.org)
analysis tool is detected on the host system, and will be configured
for a less thorough test suite otherwise. The following commands
install the shared library, header file, manual pages, and README
files at standard paths, normally under `/usr/local` so as not to
antagonize your distro's package manager.
```sh
git clone https://github.com/gueststar/nthm
cd nthm
mkdir build
cd build
cmake ..
make
make test             # optional
sudo make install
```

To uninstall, run `sudo make uninstall` from the original build
directory or manually remove the files listed in the build directory's
`install_manifest.txt`.

## Status

`Nthm` passed its tests the last time I checked, but it hasn't been
used in anything like a production setting. I've been able to observe
sporadic segfaults during the exit routine phase when it's linked with
`jemalloc` and memory is deliberately constrained with `ulimit -v`,
but the issue hasn't been reproducible with `tcmalloc` or `libc`
`malloc`, nor with unconstrained virtual memory. It hasn't been tested
with `mimalloc`.

Nevertheless, I'm just one guy and `nthm` could do with an independent
review in case there are any unknown bugs remaining. To relieve some
of the boredom, I've eliminated the usual suspects for memory and
thread safety issues as an invitation to whoever might feel tempted to
make a study of it. See
[CONTRIBUTING.md](https://github.com/gueststar/nthm/blob/master/CONTRIBUTING.md)
for more about the internals.

## FAQ

* Why did you write this?

    I wrote it partly to use in [an upcoming
    project](https://github.com/gueststar/cru), partly as an example
    of the kind of gigs I'm after, and partly for the
    challenge. Everybody with an opinion swears up and down that
    multithreaded code with shared data is practically impossible to
    get right except maybe by using the swearer's favorite language or
    framework.

* Why didn't you [just] use Rust|Go|C++|Zig?

    The Rust standard library and the Go runtime allocate memory
    behind my back, pretend that memory allocation can never fail, and
    then drop my stuff on the floor when it fails as if it's all my
    fault. That's great for cute little programs that can be picked up
    again with no harm done whenever they fall over and for
    microservices in a sandbox with ten times more memory than they'll
    ever need, but not so good for anything that would inconvenience
    someone or reflect badly on me by crashing. Object orientation as
    in C++ confers no benefit on a project like this one. I have high
    hopes for Zig but I have yet to understand its standard library
    documentation.

* You're [just] doing object oriented programming in C anyway even
  though you won't admit it.

    Yes, go on. There may be an epiphany in store for you if you carry
    this critique to its logical conclusion. (hint: It's all in your
    mind.)
