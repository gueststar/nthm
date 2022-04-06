# Contributing to `nthm`

Thank you for your interest in contributing to `nthm`. Usual ground
rules for GPL licensed software apply. If you have a job, please check
that your employer won't try to assert any rights to your
contribution. You don't have to assign the copyright to me but you
have to agree to your contribution being GPLv3 licensed or public
domain. That's all the legalese out of the way.

`Nthm` is probably better off staying small than accumulating a raft
of features without good reason, but even the way it is it offers a
golden opportunity to anyone interested in evaluating new and better
software testing and verification methodologies on a somewhat
realistic example. I'd be happy to continue forever adding new tests
to the repository written any way you want in any language provided
they're portable and non-proprietary, especially if you were to make
it easy on me by fixing up the CMakeLists.txt file accordingly. I
don't even mind if your test says my code is crap. Please take me down
a peg.

A good way to grok the code base is to start by reading the file
`api.c`. The rest of this document contains tips for modifying or
extending it.

## Coding standards

The code follows [GNU coding
standards](https://www.gnu.org/prep/standards/standards.html) with
regard to formatting, indentation, and all that, which can be had by
the default settings in emacs for formatting C code, and which might
mean tabs rather than spaces. Each brace appears on a line by itself
and matching braces are horizontally aligned, not Egyptian style.

Code is organized into short functions each explainable in no more
than a few sentences. The explanation must be included as a comment.
Either imperative or declarative explanations are fine.
 
I'm not a stickler about much else related to formatting. Documenting
every function parameter or local variable individually isn't
necessary if its meaning is obvious, but I prefer all variables used
in a function to be declared ahead of the body.

Any user-facing changes should be accompanied by new or updated manual
pages in the usual nroff or groff format. I make this request not just
to save myself the trouble of writing them, but because giving a
coherent written account of your work will alert you to some of the
mistakes and bad decisions you'd otherwise overlook by not thinking
them through. This way, the quality of your work will surpass what's
normally attained in commercial settings where it's customary to
segregate the roles of development and documentation.

Speaking of mistakes, because I'm old and forgetful I make lots of
them, so the only way I ever have time to fix all of them is by
adhering to the following practices, which prevent many mistakes and
speed the diagnosis of others.

* Any operation that can fail, especially memory allocation, has to be
  checked, and the failure reported in some way that doesn't entail
  deliberately crashing the application that uses the library. Most
  functions have an error code parameter `int *err` for reporting
  errors, with a value of 0 indicating the lack of an error.
* If multiple errors are detected in succession, the error code should
  correspond to the first one detected. As a rule, a non-zero value of
  `*err` is rarely overwritten.
* Error codes for internal errors (those that can never happen unless
  there's a bug in the library or its API is deliberately subverted)
  should uniquely determine the point in the source code where
  they're detected. In other words, no internal error code is used in
  more than one place.
* With the exception of the `err` parameter passed to almost every
  function, every pointer has to be checked for being non-null before
  being dereferenced. The check must appear in the function that
  dereferences it, even if it's already checked in the caller.
  Anything found to be null that shouldn't be is an error that must be
  reported.
* The analogous practice for array bounds checking applies.
* Arithmetic operations have to be checked for overflow if correctness
  is at stake and handled appropriately.
* Newly allocated structures have to be cleared with `memset` prior
  to anything else. This way, pointer fields are null, conditions are
  false, and numbers are zero by default, with no need for remembering
  to update the initialization function later when the definition of
  the structure is modified to have more fields.
* No recursion is allowed. (The C memory model permits only a
  constant amount of stack space and it's separate from the heap.)
* `goto` statements are not considered harmful but may jump forward
  only.

## Data structures

If you understand the data structures, invariants, and protocols, you
understand everything you need to know for modifying the code, so here
they are in a nutshell.

### Trees

The primordial data structure is a tree of pipes with one node for
each thread managed by `nthm`. The tree is initially empty. The first
time `nthm_enter_scope` is called in the main thread of an
application, a root node of the tree is allocated for it. The root
node is treated specially as a placeholder. The first time `nthm_open`
is called after that, a descendant node is allocated and attached to
the root. Each subsequent successful call to `nthm_open` allocates one
new node and attaches it as a descendant to the one corresponding to
its caller's thread. If `nthm_open` is called without being preceded
by a call to `nthm_enter_scope`, both the root and its descendant are
allocated at that time, but subsequent calls to `nthm_open` allocate
only one node each. The root node is reclaimed whenever its last
descendant yields and is read.

Typically there is just one tree for the whole application with a root
node corresponding to the main thread. Less typically, there could be
multiple trees. Any time the application creates a thread explicitly
with `pthread_create`, subsequent calls to `nthm_open` from within
that thread grow a separate tree.

Since version 0.5.0, root trees have been stored in a globally
accessible list along with the nodes corresponding to untethered
threads. This list gives `nthm` one last chance to send kill
notifications to those that are still running after the main thread
exits if they haven't been killed or read by then.

### Thread contexts

When the application calls any `nthm` function, the `nthm` function
might need to orient itself first within the tree by ascertaining the
node corresponding to the caller's thread. We delegate that job to the
`pthreads` library by storing a pointer to the node as thread-specific
data in the static variable `cursor` using `pthread_setspecific`, and
retrieving it with `pthread_getspecific` wrapped in the
`current_context` function.

It's not valid to assume that every thread corresponds to a node in a
tree. The function `current_context` may return null for a so called
"unmanaged" thread, which could be the main thread or any thread
created in user code by `pthread_create` prior to their first call to
`nthm_open` or `nthm_enter_scope`. A null context is not in itself an
error.

### Pipe lists

Because each node in the tree can have any number of descendants, it
stores pointers to all of them in a pipe list. There are at most two
pipe lists for each scope attached to each node in the tree, and at
most two others common to all scopes of node.

* The `blockers` list refers to the descendants that are still
  running.
* The `finishers` list refers to the descendants that are finished
  running.
* The `reader` refers to at most one node whose descendant it is.
* The `pool` points to an entry in the global root pipe pool if
  the pipe is untethered or is a root placeholder.

A pipe list is a doubly linked list to enable deleting any term given
only a pointer to that term. Whereas the `next_pipe` field in a pipe
list term points to the next term in the list, the `previous_pipe`
field is a pointer to a pointer.

* For anything other than the first term in the list, the
  `previous_pipe` field points to the `next_pipe` field in its
  predecessor.
* For the first term in a list, the `previous_pipe` field points to
  whatever field in the pipe tree node points to the first term in the
  list (which may any of the `reader`, `blockers`, or `finishers`
  fields).

Using a pointer to a pointer as a `previous_pipe` field enables
correctly updating the `blockers`, `finishers` or `reader` fields
as a side effect of deleting the first term in a list, given only
a pointer to that term without knowing which list contains it or
whether it's first.

Each term in a pipe list other than the one used for the `pool` also
has a complement, which points to a term in a different pipe list
whose complement points back to it.

* The complement of a blocker or finisher is the `reader` pipe list of
  the descendant that is blocking or finished.
* The complement of a reader is a term in the `blockers` or
  `finishers` list attached to the pipe whose descendant it is.

### Scopes

Scopes are represented by a stack growing from each pipe tree node.
The top stack entry stores the list of blockers and finishers
associated with the currently innermost enclosed scope, and the
rest of the stack respresents the enclosing scopes.

Entering a scope pushes a new node on to the stack with no blockers or
finishers. Exiting a scope moves any blockers or finishers associated
with the exited scope to the global pool of root pipes and pops the
stack. The vacated blockers or finishers remain running or waiting
until they are read. If the application never reads them, then an exit
routine installed by `nthm` issues a kill notification to those that
are still running when the application exits and waits for them to
yield.

## Invariants

All threads managed by `nthm` must share a consistent view of the
states of other threads.  The state of most threads as far as `nthm`
is concerned amounts to these three conditions.

* tethered or untethered
* running or finished
* alive or dead

Threads created by the `nthm_send` function introduced in version
0.5.0 are a separate category discussed below.

It's an error for a thread to be both tethered and dead, so there are
six stable states left.  Some state changes entail sequences of
operations that aren't inherently atomic, so there needs to be some
locking to ensure mutual exclusion. (Unfamiliar territory? You'll need
to read up on concurrent programming or don't blame me if the rest of
this document doesn't register.)

### Tethered versus untethered

Any thread that has a non-null `reader` field in its pipe is tethered,
and any other thread is not. In other words, the tree node associated
with a tethered "source" thread is always a descendant of some other
node in the tree (its "drain"). When a source thread changes from
tethered to untethered, its `reader` has to be deleted along with the
corresponding `blocker` or `finisher` in the drain (as indicated by
the `complement` field in the `reader` pipe list). To ensure
consistency, both nodes have to be locked while both deletions take
place. When a pipe changes from tethered to untethered, it is also
dropped into the root pool, which is locked separately.

As a general principle, it must never happen that two threads each
holding a lock both request a lock held by the other. To avoid this
possibility, any routine locking two adjacent nodes requests a lock on
the source first and the drain second.

Tethering is more complicated than untethering because it depends on
whether the source is running or finished. If the source is running,
then it has to be pushed to the blockers of the drain, but if it's
finished, it has to be enqueued to the finishers. Another reason the
source needs to be locked for tethering is in case it finishes running
during the course of this operation. A pipe changing from untethered
to tethered has to be removed from the root pool as well.

### Running versus finished

Any thread with a non-zero `yielded` field in its pipe is finished,
and any other is considered still running. Threads always start in the
running state. They can change at most once to finished and never
subsequently back to running.

Yielding is handled by `nthm` behind the scenes as part of the
clean-up that happens in the context of a thread during the time
between the moment the application code returns from the function
supplied to `nthm_open` when it was created and the moment the thread
actually exits via `pthread_exit`. It's an error for the pipe of a
finished thread to have any descendants, so before yielding, every
thread first untethers the pipes in its enclosing scopes, kills its
blockers, and disposes of its finishers (which can be assumed by
induction to have no descendants) ignoring their results. The blockers
are also untethered in the course of being killed. What happens next
depends on whether the thread is untethered or tethered (and held that
way in any case at least temporarily by a lock).

* If the thread is untethered, it sets its `yielded` field and signals
  its `termination` condition to unblock the any thread that might be
  waiting to read from it. As a last resort, the reading thread may be
  the main one running the exit routine installed by `nthm`.
* If an untethered thread finds itself to have been killed already by
  the time it yields, then it also removes its pipe from the root pool
  and frees it.
* If the thread is tethered, it acquires a lock on its drain in
  addition to the one it already holds on its own pipe, moves its pipe
  from the drain's blockers to its finishers, sets its `yielded`
  field, and signals the drain's `progress` condition (not its own) to
  unblock the drain in case the drain was waiting to read from it.

Note that the locks are acquired source-first. Undefined behavior
results from a thread being both killed and read by the application
code.

### Alive versus dead

A thread is dead if the `killed` field in its pipe is non-zero and is
alive otherwise. Killing a thread entails acquiring a lock on its pipe
so that it can't yield while being killed if it's still running, and
also untethering it if it's presently tethered.

If the thread is still running when it's killed, then its `progress`
condition is also signaled before its pipe is unlocked and the thread
is left to finish. Signalling the `progress` condition on a running
thread when killing it helps it finish sooner by unblocking it if it
was blocked waiting to read from any of its blockers (as also noted
above).

A side effect if killing a running thread is to sweep its pipe into
the root pool.

### Sent threads

A thread created by `nthm_send` has a simpler life cycle than the
above because it is never killed or read and doesn't synchronize with
its creating thread when it yields. It participates only in the start
registration and reclamation protocols described below. There is a
pipe associated with the thread but no operations on the pipe are
available to user code, and the pipe is reclaimed when the user code
running in its context exits. Nor is there any cause for the pipe to
enter the root pool, although pipes created under it and subsequently
scope-vacated or untethered may do so normally.

### Other invariants

Sometimes it's possible to detect in advance that a living thread will
be killed or at least ignored. If the thread is tethered and its drain
is dead or finished, that thread will be killed unless it finishes
first, in which case it will be ignored. The same applies if the
drain's drain is dead or finished, and so on to the root.

It might seem optimal to distinguish between living and dead threads
by interrogating the `killed` and `yielded` fields of their pipes'
ancestors in the tree, but climbing a dynamically changing tree is a
costly operation because each drain has to be locked in turn before
its source is unlocked.  Because it's meant to be polled frequently,
the public-facing `nthm_killed` function doesn't use this criterion.
The read and select functions don't use it either because interrupting
them too eagerly might cause memory leaks when their caller can't free
the results it would have received. However, `nthm_open` and
`nthm_send` refrain from starting a new thread if they ascertain by
this condition that its caller's thread is as good as dead.

Climbing trees is more important for getting truncation to work as
intended. The `nthm_truncate` and `nthm_truncated` functions could
be simple wrappers around setting and interrogating the `truncation`
field in a pipe were it not for the need to expedite truncated threads
that are blocked on a read operation. If a drain truncates its source
before reading from it, but the source is waiting on a blocker of its
own, the drain will wait a long time unless the blocker of the source
knows that it too should finish up sooner than usual. That blocker
might also be waiting, but somewhere below it there has to be one
that's not waiting for anything and therefore has a chance to ask
whether it has been truncated. The right way to answer that question
is to check not just its own `truncation` field but that of any of its
ancestors in the tree.

## Protocols

`Nthm` uses `pthreads` conditions, signals, and locks to coordinate
reading from pipes according to the protocols implied above. The
protocol for reading depends on whether the pipe is tethered or
untethered. Implicit in these interactions is the assumption that the
application code will neither try to read from a freed pipe (a
baseline C programming skill) nor access these data structures except
via the published API.

### Untethered reading

If a pipe passed to the `nthm_read` function is untethered but the
caller runs in the context of a managed thread (that is, one that was
either created by `nthm_open` or has previously called `nthm_open` or
`nthm_enter_scope`) then it's advantageous to tether the pipe to the
caller's thread and follow the tethered reading protocol below.
Untethered reading is done only by unmanaged threads.

An untethered read requires locking the source pipe first, then
checking the pipe's `yielded` field, and then either unlocking the
pipe or atomically unlocking and waiting on the pipe's `termination`
signal (depending on whether the pipe's thread is finished or still
running). After that point, the pipe's result is readable and the pipe
can be freed.

Taking each of these steps in this order is necessary to the
correctness of the protocol. If the `yielded` field were checked
without the pipe being locked first and the decision to wait made on
that basis, it could happen that the source thread might finish and
signal `termination` before the reader starts waiting for it. The
reader would then miss the signal and wait forever. The lock precludes
this scenario because the source pipe's thread requests the same lock
before yielding as noted above. If unlocking the pipe and waiting for
the signal were not done atomically (as the `pthread_cond_wait`
semantics specifies), then the same could happen after the pipe is
unlocked but before the reader starts waiting. If you're thinking
about porting `nthm` to a `pthreads` replacement, the replacement
will have to support something like this semantics.

### Tethered reading

The advantage of tethered over untethered reading is that the thread
doing the reading (the "drain") can be interrupted if it's killed
instead of getting stuck waiting. The protocol is more complicated but
is exactly the way it is for similar reasons to those noted above.

First the drain retrieves its own pipe using `current_context`, which
must match the `reader` of the given source. The drain locks its own
pipe rather than that of the source, and then checks both the `killed`
field in its own pipe and the `yielded` field in the source's pipe. If
both of these fields are zero, the drain atomically unlocks its pipe
and waits on its `progress` condition, but if either field is
non-zero, then it just unlocks its pipe. Subsequent to this point, the
operation can safely conclude either empty-handed or with the source's
result, depending on whether the drain has been killed. In either
case, the source is then killed as detailed above.

### Start registration

A feature necessary mainly for threads created by `nthm_send`
introduced in version 0.5.0 but also to some extent by `nthm_open`
resolves a race condition that might otherwise allow the application
to exit before the thread gets a chance to start. After either routine
creates a thread, it calls the `started` function before returning to
the caller. Before passing control to the user code, the `nthm`
library code running in the context of the newly created thread calls
the `registered` function. The `started` function blocks until it
detects the that the `registered` function has been called.

This protocol may be more complicated than it seems because multiple
threads can call both functions concurrently. A count of `starters`
locked by the `starter_lock` mutex and incremented only by the
`registered` function stores the number of threads that have
registered without yet being detected. The `started` function waits
only when the count is zero.  If the `registered` function precedes
the `started` function, the latter decrements the count without
waiting. Conversely, the `registered` function doesn't send a signal
if the count is already positive, but only increments the count.

It's not a problem for a signal meant for one thread to be detected by
another if they were scheduled out of order. However, it might well be
problematic for a waiting thread not to be woken up until after
another one comes and goes, having zeroed the count with no need to
intercept the signal itself. The solution is for the `registered`
function to broadcast the `started` signal (rather than just sending
it) whenever it increments the count from zero to one as it atomically
releases the `starter_lock`, and for the `started` function to wait on
the signal as many times as needed until `starters` goes positive.

### Thread resource reclamation

When the application process exits, something has to be done about the
threads that are still running. On Linux systems, a detached thread
can keep running indefinitely, which might not be ideal, but
preemptively killing it on exit would conflict with the stated goal of
non-preemptive thread management. On the other hand, if we rely on
well behaved applications for timely thread termination, anomalous
Valgrind test results are still possible when the process exits during
the brief time interval between the application code in a thread
returning its result and the thread actually being terminated by
`nthm` library code running in its context.

To support non-preemptive thread termination while also ensuring that
all threads have terminated when the process exits, `nthm` joins all
threads prior to exiting in a way that's not visible to the
application code. Threads achieve this effect by following a
particular protocol best motivated by an out-of-order explanation.

* After the application code in each thread yields and the state of
  its pipe exposed via the API is updated accordingly, the `nthm`
  library code still running in the thread's context waits for the
  `finished` signal authorizing it to call `pthread_exit`, which it
  does only after receiving the signal.
* This signal usually comes from the next thread to yield, which
  synchronizes with its predecessor by calling `pthread_join`. The
  call to `pthread_join` blocks until it can be guaranteed that the
  predecessor thread has exited.
* The succeeeding thread then behaves just like its predecessor in
  that it waits for `finished` before calling `pthread_exit`.
* The last running thread, which has no successor, sends the
  `last_runner` signal before waiting for `finished`. The
  `last_runner` signal is awaited by the exit code of the main thread
  previously installed by `nthm` during initialization, which takes
  over by sending the `finished` signal.

A few other details hold this protocol together. To join with its
predecessor, each thread needs to know its predecessor's identifier as
given by `pthread_self`, so prior to waiting for the signal, each
thread stores its identifier in the static variable
`finishing_thread`.  However, because multiple threads may yield
concurrently, a thread may find this variable already occupied by a
concurrently yielding thread's identifier, so before storing its own
identifier, it deals with the current one by sending the `finished`
signal itself and then joining with the concurrently yielding
thread. Any number of threads may yield and store their identifiers
ahead of it, so a thread must be prepared to join with each one in
turn before getting its chance.

The protocol for the exit routine code is slightly different because
it has to ensure that it joins only with the last thread. Although it
is reached only after the application leaves its `main` routine or
explicitly calls `exit`, more pipes might still be opened. To detect
the last thread, it checks a count maintained in `running_threads` and
if necessary waits for the signal `last_thread`, which is sent by any
exiting thread before it waits for `finished`.
