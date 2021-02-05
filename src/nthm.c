/*
  nthm -- non-preemptive thread hierarchy manager

  copyright (c) 2020, 2021 Dennis Furey

  Nthm is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Nthm is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
  License for more details.

  You should have received a copy of the GNU General Public License
  along with nthm. If not, see <https://www.gnu.org/licenses/>.
*/

#include <pthread.h>
#include <nthm.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <bits/local_lim.h>
#include "nthmconfig.h"

#ifndef PTHREAD_STACK_MIN
// estimated minimum stack size to cover the overhead of creating a thread if not defined by the system
#define PTHREAD_STACK_MIN 65536
#endif

// the n-th internal error code
#define THE_IER(n) (-n)

// macro for raising an internal error without overwriting an existing error
#define IER(n) (*err = (*err ? *err : THE_IER(n)))

// arbitrary magic number for consistency checks; most positive numbers less than 2^31 would also work
#define MAGIC 1887434018

// the n-th non-magic value; as with internal errors, distinguishable values are used only to log the point of detection
#define MUGGLE(n) n

typedef struct pipe_list_struct *pipe_list;   // doubly linked list of pipes

struct pipe_list_struct
{
  nthm_pipe pipe;
  pipe_list complement;       // points to a node in another list whose complement points back to this one
  pipe_list *previous_pipe;   // points either to a field in a pipe or to the next_pipe field in its predecessor
  pipe_list next_pipe;
};

struct nthm_pipe_struct
{
  int valid;                  // holds a muggle if any pthread operation or integrity check fails, MAGIC otherwise
  int killed;                 // set either by user code or by the reader yielding without having read from the pipe
  int yielded;                // set by the thread when its result is finished being computed
  pipe_list pool;             // root neighbors if the pipe is untethered
  pipe_list reader;           // a list of at most one pipe designated to read the result from this one
  pipe_list blockers;         // a list of pipes created by this one whose results are awaited
  pipe_list finishers;        // a list of pipes created by this one whose results are available in the order they finished
  pipe_list finisher_queue;   // points to the most recently finished pipe
  pthread_cond_t termination; // signaled by the thread to indicate termination if it has no reader
  pthread_cond_t progress;    // signaled by the reader thread when this one is killed or by any blocker that terminates
  pthread_mutex_t lock;       // secures mutually exclusive access for otherwise non-atomic operations on this structure
  unsigned truncation;        // set by user code when a partial result is acceptable
  nthm_pipe children;         // pipes corresponding to enclosed scopes
  nthm_pipe parents;          // pipes corresponding to enclosing scopes
  void *result;               // returned by user code in the thread when it terminates
  int status;                 // an error code returned by user code if not overridden by other conditions
  int top;                    // non-zero means it was the first pipe to be allocated in an unmanaged thread
};

typedef struct thread_spec_struct *thread_spec;  // passed to the manager function used to start threads

struct thread_spec_struct
{
  nthm_pipe pipe;
  nthm_worker operator;
  void *operand;
};

// used to retrieve the pipe writable by the currently executing thread
static pthread_key_t cursor;

// used to initialize static storage
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

// used for storing error codes detected outside the context of the thread causing them
static int global_error = 0;

// a bit bucket for error codes the user chooses to ignore
static int ignored = 0;

// for exclusive access to the global error code
static pthread_mutex_t error_lock;

// created threads have these attributes
static pthread_attr_t thread_attribute;

// created mutexes have these attributes
static pthread_mutexattr_t mutex_attribute;

// list of untethered and top pipes to be freed on exit
static pipe_list root_pipes = NULL;

// enforces mutually exclusive access to the root pipe list
static pthread_mutex_t root_lock;

// puts another pipe into the root pipe list
static int pooled (nthm_pipe drain, int *err);

// function that reclaims static storage and synchronizes threads on exit
static void teardown (void);

// count of the number of currently running threads created by nthm_open
static uintptr_t running_threads = 0;

// when non-zero, there is a thread waiting only for a junction signal before exiting
static int ready_to_join = 0;

// the thread id of the thread that's ready to join, if any
static pthread_t joinable_thread;

// the signal awaited by the joinable thread, if any
static pthread_cond_t junction;

// signaled by a thread that's ready to join if it detects that it's the only thread left running
static pthread_cond_t last_thread;

// enforces mutually exclusive access to running_threads, ready_to_join, and joinable_thread
static pthread_mutex_t junction_lock;

// set to non-zero if the usual shutdown protocol has to be abandoned due to a pthread error
static int deadlocked = 0;

#ifdef MEMTEST                  // keep counts of allocated structures; not suitable for production code
// number of pipes in memory
static uintptr_t pipes = 0;

// number of pipe lists in memory
static uintptr_t pipe_lists = 0;

// number of thread specifications in memory
static uintptr_t thread_specs = 0;
#endif // MEMTEST




// --------------- initialization routines -----------------------------------------------------------------





static int
stack_limited_thread_type (a, err)
	  pthread_attr_t *a;
	  int *err;

	  // Initialize the attributes for all created threads. On 32-bit
	  // systems, use small stacks.
{
  if ((!a) ? IER(22) : pthread_attr_init (a) ? IER(23) : 0)
	 return 0;
#ifdef USE_SMALL_STACKS
  if (pthread_attr_setstacksize (a, PTHREAD_STACK_MIN + NTHM_STACK_MIN) ? IER(24) : 0)
	 {
		pthread_attr_destroy (a);
		return 0;
	 }
#endif
  return 1;
}







static int
error_checking_mutex_type (a, err)
	  pthread_mutexattr_t *a;
	  int *err;

	  // Initialize the attributes for all created mutexes to use error
	  // checking.
{
  if ((!a) ? IER(25) : pthread_mutexattr_init (a) ? IER(26) : 0)
	 return 0;
  if (!(pthread_mutexattr_settype (a, PTHREAD_MUTEX_ERRORCHECK) ? IER(27) : 0))
	 return 1;
  pthread_mutexattr_destroy (a);
  return 0;
}







static void
initialization_routine ()

    // Initialize all static storage or none.
{
  int err;

  err = 0;
  if (! error_checking_mutex_type (&mutex_attribute, &err))
	 goto a;
  if (pthread_mutex_init (&junction_lock, &mutex_attribute) ? (err = THE_IER(28)) : 0)
	 goto b;
  if (pthread_mutex_init (&error_lock, &mutex_attribute) ? (err = THE_IER(29)) : 0)
	 goto c;
  if (pthread_mutex_init (&root_lock, &mutex_attribute) ? (err = THE_IER(30)) : 0)
	 goto d;
  if (pthread_cond_init (&last_thread, NULL) ? (err = THE_IER(31)) : 0)
	 goto e;
  if (pthread_cond_init (&junction, NULL) ? (err = THE_IER(32)) : 0)
	 goto f;
  if (! stack_limited_thread_type (&thread_attribute, &err))
	 goto g;
  if (pthread_key_create (&cursor, NULL) ? (err = THE_IER(33)) : 0)
	 goto h;
  if (atexit (teardown) ? (! (err = THE_IER(34))) : 1)
	 return;
 h: pthread_attr_destroy (&thread_attribute);
 g: pthread_cond_destroy (&junction);
 f: pthread_cond_destroy (&last_thread);
 e: pthread_mutex_destroy (&root_lock);
 d: pthread_mutex_destroy (&error_lock);
 c: pthread_mutex_destroy (&junction_lock);
 b: pthread_mutexattr_destroy (&mutex_attribute);
 a: fprintf (stderr, "%s\n", nthm_strerror (err));
  exit (EXIT_FAILURE);
}






static inline void
initialize_cursor ()

    // Static variables including the cursor have to be initialized
    // just once for the whole process.
{
  pthread_once (&once_control, initialization_routine);
}






static void
globally_throw (err)
	  int err;

	  // Safely assign the global error code.
{
  if (err ? pthread_mutex_lock (&error_lock) : 1)
	 return;
  global_error = (global_error ? global_error : err);
  pthread_mutex_unlock (&error_lock);
}





// --------------- thread contexts -------------------------------------------------------------------------





static inline nthm_pipe 
current_context ()

	  // Return a pointer to the pipe node corresponding to the
	  // currently running thread, if any.
{
  initialize_cursor ();
  return (nthm_pipe) pthread_getspecific (cursor);
}







static int
set_context (drain, err)
	  nthm_pipe drain;
	  int *err;

	  // Identify a pipe with the current thread context.
{
  int e;

  initialize_cursor ();
  if (!(e = pthread_setspecific (cursor, (void *) drain)))
	 return 1;
  *err = (*err ? *err : (e == ENOMEM) ? e : (e = EAGAIN) ? e : THE_IER(35));
  return 0;
}







static inline void
clear_context (err)
	  int *err;

	  // Identify no pipe with the current thread context.
{
  set_context (NULL, err);
}






// --------------- pipe list demolition --------------------------------------------------------------------






static int
severed (t, err)
	  pipe_list t;
	  int *err;

	  // Remove an item from a pipe list without freeing it.
{
  if ((! t) ? IER(36) : t->previous_pipe ? 0 : IER(37))
	 return 0;
  if ((*(t->previous_pipe) = t->next_pipe))
	 t->next_pipe->previous_pipe = t->previous_pipe;
  t->previous_pipe = NULL;
  t->next_pipe = NULL;
  return 1;
}








static int
freed (r, err)
	  pipe_list r;
	  int *err;

	  // Free a unit pipe list and remove the reference to it from its
	  // complement, if any.
{
  if ((!r) ? IER(38) : r->next_pipe ? IER(39) : r->previous_pipe ? IER(40) : 0)
	 return 0;
  if (r->complement)
	 {
		if ((r->complement->complement == r) ? 0 : IER(41))
		  return 0;
		r->complement->complement = NULL;
	 }
  free (r);
#ifdef MEMTEST
  pthread_mutex_lock (&error_lock);
  pipe_lists--;
  pthread_mutex_unlock (&error_lock);
#endif
  return 1;
}







static inline int
unilaterally_delisted (t, err)
	  pipe_list t;
	  int *err;

	  // Remove an item from a pipe list, free it, and remove the
	  // reference from its complement.
{
  return (severed (t, err) ? freed (t, err) : 0);
}







static nthm_pipe
popped (f, err)
	  pipe_list f;
	  int *err;

	  // Return the first pipe in a list f and bilaterally delist it.
{
  nthm_pipe p;

  if ((!f) ? IER(42) : ! unilaterally_delisted (f->complement, err))
	 return NULL;
  p = f->pipe;
  return (unilaterally_delisted (f, err) ? p : NULL);
}








static nthm_pipe
dequeued (f, q, err)
	  pipe_list f;
	  pipe_list *q;
	  int *err;

	  // Bilaterally delist and return the first pipe in a non-empty
	  // queue, or return NULL if the queue is empty.
{
  nthm_pipe p;

  if (((!f) != !*q) ? IER(43) : ! f)
	 return NULL;
  if (f != *q)
	 return popped (f, err);
  if ((p = popped (f, err)))
	 *q = NULL;
  return p;
}







static nthm_pipe
bilaterally_delisted (r, f, q, err)
	  pipe_list r;
	  pipe_list f;
	  pipe_list *q;
	  int *err;

	  // Return r->complement->pipe while also delisting both r and its
	  // complement.
{
  nthm_pipe p;
  pipe_list c;

  if ((!r) ? IER(44) : (c = r->complement) ? 0 : IER(45))
	 return NULL;
  if (c == f)
	 return dequeued (f, q, err);
  if (c != *q)
	 return popped (c, err);
  if (f ? 0 : IER(46))
	 return NULL;
  p = popped (*q, err);
  for (*q = f; (*q)->next_pipe; *q = (*q)->next_pipe);
  return p;
}







static int
bilaterally_freed (r, b, err)
	  pipe_list r;
	  pipe_list b;
	  int *err;

	  // Free a pair of complementary unit pipe lists.
{
  if ((!r) ? IER(47) : (!b) ? IER(48) : (r->complement != b) ? IER(49) : (b->complement != r) ? IER(50) : 0)
	 return 0;
  return ((freed (r, err) ? 1 : IER(51)) ? freed (b, err) : 0);
}







// --------------- pipe list construction ------------------------------------------------------------------







static pipe_list
pipe_list_of (p, err)
	  nthm_pipe p;
	  int *err;

	  // Return a newly created unit list containing only the pipe p
	  // and no complement provided there is sufficient memory.
{
  pipe_list t;

  if ((p ? 0 : IER(52)) ? 1 : (t = (pipe_list) malloc (sizeof (*t))) ? 0 : (*err = (*err ? *err : ENOMEM)))
	 return NULL;
#ifdef MEMTEST
  pthread_mutex_lock (&error_lock);
  pipe_lists++;
  pthread_mutex_unlock (&error_lock);
#endif
  bzero (t, sizeof (*t));
  t->pipe = p;
  return t;
}






static int
new_complementary_pipe_lists (r, d, b, s, err)
	  pipe_list *r;
	  nthm_pipe d;
	  pipe_list *b;
	  nthm_pipe s;
	  int *err;

	  // Allocate and initialize complementary unit pipe lists r of d
	  // and b of s if there is sufficient memory.
{
  if ((!b) ? IER(53) : (!r) ? IER(54) : !(*r = pipe_list_of (d, err)))
	 return 0;
  if ((*b = pipe_list_of (s, err)))
	 {
		(*b)->complement = *r;
		(*r)->complement = *b;
		return 1;
	 }
  if (freed (*r, err) ? 1 : IER(55))
	 *r = NULL;
  return 0;
}








static int
pushed (t, b, err)
	  pipe_list t;
	  pipe_list *b;
	  int *err;

	  // Concatenate a unit list t with a list b.
{
  if ((!t) ? IER(56) : (!b) ? IER(57) : t->previous_pipe ? IER(58) : t->next_pipe ? IER(59) : 0)
	 return 0;
  if ((t->next_pipe = *(t->previous_pipe = b)))
	 (*b)->previous_pipe = &(t->next_pipe);
  return ! ! (*b = t);
}









static int
enqueued (t, f, q, err)
	  pipe_list t;
	  pipe_list *f;
	  pipe_list *q;
	  int *err;

	  // Append the unit list t to the queue that starts with f and
	  // ends with q.
{
  pipe_list *p;

  if ((!f) ? IER(60) : (!q) ? IER(61) : ((!*f) != !*q) ? IER(62) : (!*q) ? 0 : (*q)->next_pipe ? IER(63) : 0)
	 return 0;
  return ((! pushed (t, p = (*q ? &((*q)->next_pipe) : f), err)) ? 0 : (*q = *p) ? 1 : ! IER(64));
}





// --------------- tree climbing ---------------------------------------------------------------------------







static int
heritably_killed_or_yielded (source, err)
	  nthm_pipe source;
	  int *err;

	  // Detect whether source has yielded or been killed either
	  // explicitly or due to any of its drains yielding or being
	  // killed. It's necessary to lock each source, then lock its
	  // drain, and then unlock the source all the way up the tree in
	  // case any of them gets untethered.
{
  nthm_pipe drain;

  if ((!source) ? IER(65) : (source->valid != MAGIC) ? IER(66) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(67) : 0) ? (source->valid = MUGGLE(1)) : 0)
	 return 0;
  if (source->killed ? 1 : source->yielded)
	 return ((pthread_mutex_unlock (&(source->lock)) ? IER(68) : 0) ? (source->valid = MUGGLE(2)) : 1);
  for (; source->reader; source = drain)
	 {
		if ((!(drain = source->reader->pipe)) ? IER(69) : (drain->valid != MAGIC) ? IER(70) : 0)
		  break;
		if ((pthread_mutex_lock (&(drain->lock)) ? IER(71) : 0) ? (drain->valid = MUGGLE(3)) : 0)
		  break;
		if (drain->yielded ? 1 : drain->killed)
		  if ((pthread_mutex_unlock (&(source->lock)) ? IER(72) : 0) ? (source->valid = MUGGLE(4)) : 1)
			 return ((pthread_mutex_unlock (&(drain->lock)) ? IER(73) : 0) ? (drain->valid = MUGGLE(5)) : 1);
		if ((pthread_mutex_unlock (&(source->lock)) ? IER(74) : 0) ? (source->valid = MUGGLE(6)) : 0)
		  return 0;
	 }
  return ((pthread_mutex_unlock (&(source->lock)) ? IER(75) : 0) ? (! (source->valid = MUGGLE(7))) : 0);
}







static int
heritably_truncated (source, err)
	  nthm_pipe source;
	  int *err;

	  // Detect whether source has been truncated either explicitly
	  // or due to any of its drains being truncated.
{
  nthm_pipe drain;

  if ((!source) ? IER(76) : (source->valid != MAGIC) ? IER(77) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(78) : 0) ? (source->valid = MUGGLE(8)) : 0)
	 return 0;
  if (source->truncation)
	 return ((pthread_mutex_unlock (&(source->lock)) ? IER(79) : 0) ? (source->valid = MUGGLE(9)) : 1);
  for (; source->reader; source = drain)
	 {
		if ((!(drain = source->reader->pipe)) ? IER(80) : (drain->valid != MAGIC) ? IER(81) : 0)
		  break;
		if ((pthread_mutex_lock (&(drain->lock)) ? IER(82) : 0) ? (drain->valid = MUGGLE(10)) : 0)
		  break;
		if (drain->truncation ? ((pthread_mutex_unlock (&(source->lock)) ? IER(83) : 0) ? (source->valid = MUGGLE(11)) : 1) : 0)
		  return ((pthread_mutex_unlock (&(drain->lock)) ? IER(84) : 0) ? (drain->valid = MUGGLE(12)) : 1);
		if ((pthread_mutex_unlock (&(source->lock)) ? IER(85) : 0) ? (source->valid = MUGGLE(13)) : 0)
		  return 0;
	 }
  return ((pthread_mutex_unlock (&(source->lock)) ? IER(86) : 0) ? (! (source->valid = MUGGLE(14))) : 0);
}






// --------------- pipe construction -----------------------------------------------------------------------






static nthm_pipe
new_pipe (err)
	  int *err;

	  // Return a newly allocated and initialized pipe.
{
  nthm_pipe source;
  int e;

  if ((source = (nthm_pipe) malloc (sizeof (*source))) ? 0 : (*err = (*err ? *err : ENOMEM)))
	 return NULL;
  bzero (source, sizeof (*source));
  if ((e = pthread_cond_init (&(source->termination), NULL)))
	 goto a;
  if ((e = pthread_cond_init (&(source->progress), NULL)))
	 goto b;
  if ((e = pthread_mutex_init (&(source->lock), NULL)))
	 goto c;
#ifdef MEMTEST
  pthread_mutex_lock (&error_lock);
  pipes++;
  pthread_mutex_unlock (&error_lock);
#endif
  source->valid = MAGIC;
  return source;
 c: pthread_cond_destroy (&(source->progress));
 b: pthread_cond_destroy (&(source->termination));
 a: free (source);
  *err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(87));
  return NULL;
}





static int
tethered (s, d, err)
	  nthm_pipe s;
	  nthm_pipe d;
	  int *err;

	  // Tether a source s to a drain d if it isn't already. If the
	  // source has terminated, then it has to be enqueued in the
	  // drain's finishers, but if it's still running, then it has to
	  // be pushed into the blockers. Locks on both are needed, and the
	  // source is locked first.
{
  int done;
  pipe_list r, w;   // the source's reader and the drain's finisher or blocker

  if ((! d) ? IER(88) : (d->valid != MAGIC) ? IER(89) : (! s) ? IER(90) : (s->valid != MAGIC) ? IER(91) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(s->lock)) ? IER(92) : 0) ? (s->valid = MUGGLE(15)) : 0)
	 return 0;
  if ((!(s->reader)) ? (done = 0) : (s->reader->pipe == d) ? (done = 1) : ! (done = ! (*err = (*err ? *err : NTHM_NOTDRN))))
	 goto a;
  if (s->killed ? IER(93) : (pthread_mutex_lock (&(d->lock)) ? IER(94) : 0) ? (d->valid = MUGGLE(16)) : 0)
	 goto a;
  if (new_complementary_pipe_lists (&r, d, &w, s, err))
	 if (pushed (r, &(s->reader), err) ? 1 : bilaterally_freed (r, w, err) ? 0 : ! IER(95))
		if (!(done = (s->yielded ? enqueued (w, &(d->finishers), &(d->finisher_queue), err) : pushed (w, &(d->blockers), err))))
		  if (freed (w, err) ? 1 : IER(96))
			 if (! unilaterally_delisted (s->reader, err))
				s->valid = MUGGLE(17);
  if (pthread_mutex_unlock (&(d->lock)) ? IER(97) : 0)
	 d->valid = MUGGLE(18);
 a: if (pthread_mutex_unlock (&(s->lock)) ? IER(98) : 0)
	 s->valid = MUGGLE(19);
  return done;
}







// --------------- pipe demolition -------------------------------------------------------------------------






static int
retired (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tear down a pipe that has no drain and no blockers or
	  // finishers left.
{
  if ((!source) ? IER(99) : (source->valid != MAGIC) ? IER(100) : 0)
	 return 0;
  if (source->reader ? IER(101) : source->blockers ? IER(102) : 0) 
	 return 0;
  if (source->finishers ? IER(103) : source->finisher_queue ? IER(104) : 0)
	 return 0;
  if ((pthread_cond_destroy (&(source->termination)) ? IER(105) : 0) ? (source->valid = MUGGLE(20)) : 0)
	 return 0;
  if ((pthread_cond_destroy (&(source->progress)) ? IER(106) : 0) ? (source->valid = MUGGLE(21)) : 0)
	 return 0;
  if ((pthread_mutex_destroy (&(source->lock)) ? IER(107) : 0) ? (source->valid = MUGGLE(22)) : 0)
	 return 0;
  source->valid = MUGGLE(23);
  free (source);
#ifdef MEMTEST
  pthread_mutex_lock (&error_lock);
  pipes--;
  pthread_mutex_unlock (&error_lock);
#endif
  return 1;
}







static int
untethered (source, err)
	  nthm_pipe source;
	  int *err;

	  // Separate a possibly running source from a running drain. Locks
	  // on both are needed. The source is locked first.
{
  nthm_pipe drain;
  int done;

  if ((!source) ? IER(108) : (source->valid == MAGIC) ? 0 : IER(109))
	 return 0;
  if (!(source->reader))
	 return 1;
  if ((source->reader->pipe ? 0 : IER(110)) ? (source->valid = MUGGLE(24)) : 0)
	 return 0;
  if (((drain = current_context ()) != source->reader->pipe) ? (*err = (*err ? *err : NTHM_NOTDRN)) : 0)
	 return 0;
  if ((drain->valid != MAGIC) ? IER(111) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(112) : 0) ? (source->valid = MUGGLE(25)) : (done = 0))
	 return 0;
  if ((pthread_mutex_lock (&(drain->lock)) ? IER(113) : 0) ? (drain->valid = MUGGLE(26)) : 0)
	 goto a;
  done = (source == bilaterally_delisted (source->reader, drain->finishers, &(drain->finisher_queue), err));
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(114) : 0)
	 drain->valid = MUGGLE(27);
 a: if (pthread_mutex_unlock (&(source->lock)) ? IER(115) : 0)
	 source->valid = MUGGLE(28);
  return (done ? pooled (source, err) : 0);
}








static int
killable (source, err)
	  nthm_pipe source;
	  int *err;

	  // Untether and kill a pipe, which entails retiring it if
	  // it has already yielded.
{
  if ((!source) ? IER(116) : (source->valid != MAGIC) ? IER(117) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(118) : 0) ? (source->valid = MUGGLE(29)) : 0)
	 return 0;
  source->killed = 1;
  if ((pthread_mutex_unlock (&(source->lock)) ? IER(119) : 0) ? (source->valid = MUGGLE(30)) : 0)
	 return 0;
  return untethered (source, err);
}







static int
blockers_killed (d, err)
	  nthm_pipe d;
	  int *err;

	  // This function run in the drain's context kills all blockers to
	  // the drain. The drain has to be locked long enough to get a
	  // blocker from the list without the list being mutated, but the
	  // lock has to be let off the drain temporarily before
	  // untethering the blocker.
{
  nthm_pipe blocker;
  int done;

  if ((! d) ? IER(120) : (d->valid != MAGIC) ? IER(121) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(122) : 0) ? (d->valid = MUGGLE(31)) : 0)
	 return 0;
  while (! (done = ! (d->blockers)))
	 {
		if ((blocker = d->blockers->pipe) ? 0 : (d->valid = MUGGLE(32)))
		  break;
		if ((pthread_mutex_unlock (&(d->lock)) ? IER(123) : 0) ? (d->valid = MUGGLE(33)) : ! killable (blocker, err))
		  return 0;
		if ((pthread_mutex_lock (&(d->lock)) ? IER(124) : 0) ? (d->valid = MUGGLE(34)) : 0)
		  return 0;
	 }
  return (((pthread_mutex_unlock (&(d->lock)) ? IER(125) : 0) ? (d->valid = MUGGLE(35)) : 0) ? 0 : done);
}







static void
flush (d, err)
	  nthm_pipe *d;
	  int *err;

	  // Kill both the blockers and the finishers to a drain. The pipes
	  // in the finishers queue are assumed to be flushed already. A
	  // lock is needed here in case one of the blockers finishes
	  // concurrently. If the drain has parents, then the user code has
	  // yielded from within a local scope (i.e., neglecting to exit
	  // the local scope), so kill the parents' blockers and finishers
	  // and put the pipe back into its global scope.
{
  nthm_pipe finisher, scope;

  if (d ? 0 : IER(126))
	 return;
  while ((scope = *d))
	 {
		if ((scope->valid != MAGIC) ? IER(127) : ! blockers_killed (scope, err))
		  return;
		if ((pthread_mutex_lock (&(scope->lock)) ? IER(128) : 0) ? (scope->valid = MUGGLE(36)) : 0)
		  return;
		while ((finisher = dequeued (scope->finishers, &(scope->finisher_queue), err)))
		  if (finisher->pool ? IER(129) : retired (finisher, err) ? 0 : IER(130))
			 finisher->valid = MUGGLE(37);
		if ((pthread_mutex_unlock (&(scope->lock)) ? IER(131) : 0) ? (scope->valid = MUGGLE(38)) : ! (scope->parents))
		  return;
		if (set_context (*d = scope->parents, err) ? ((scope->pool ? IER(132) : retired (scope, err) ? 0 : IER(133))) : 1)
		  return;
	 }
}






// --------------- drain-side protocol support -------------------------------------------------------------






static thread_spec
thread_spec_of (source, operator, operand, err)
	  nthm_pipe source;
	  nthm_worker operator;
	  void *operand;
	  int *err;

	  // Return a newly allocated and initialized thread_spec.
{
  thread_spec t;

  if (source ? ((t = (thread_spec) malloc (sizeof (*t))) ? 0 : (*err = (*err ? *err : ENOMEM))) : 1)
	 if (source ? (retired (source, err) ? 1 : IER(134)) : 1)
		return NULL;
  bzero (t, sizeof (*t));
#ifdef MEMTEST
  pthread_mutex_lock (&error_lock);
  thread_specs++;
  pthread_mutex_unlock (&error_lock);
#endif
  t->operator = operator;
  t->operand = operand;
  t->pipe = source;
  return t;
}







static void
unspecify (t, err)
	  thread_spec t;
	  int *err;

	  // Free a thread specification structure.
{
  if (t ? 0 : IER(135))
	 return;
  if (t->pipe ? (! retired (t->pipe, err)) : 0)
	 IER(136);
  free (t);
#ifdef MEMTEST
  pthread_mutex_lock (&error_lock);
  thread_specs--;
  pthread_mutex_unlock (&error_lock);
#endif
}







static nthm_pipe
current_or_new_context (err)
	  int *err;

	  // Return an existing pipe associated with the current thread
	  // context or create one and push it into the root pipes.
{
  nthm_pipe drain;
  int e;

  if ((drain = current_context ()) ? 1 : ! (drain = new_pipe (err)))
	 return drain;
  drain->top = 1;
  if ((pooled (drain, err) ? set_context (drain, err) : 0) ? 0 : retired (drain, err) ? 1 : IER(137))
	 drain = NULL;
  return drain;
}







static void*
untethered_read (source, err)
	  nthm_pipe source;
	  int *err;

	  // Read from a pipe with no designated drain and therefore no
	  // opportunity for the read to be interrupted by the drain being
	  // killed. Wait on the pipe's termination signal if necessary.
{
  void *result;
  int done;

  result = NULL;
  if ((!source) ? IER(138) : (source->valid != MAGIC) ? IER(139) : (done = 0))
	 return NULL;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(140) : 0) ? (source->valid = MUGGLE(39)) : 0)
	 return NULL;
  if (source->reader ? (*err = (*err ? *err : NTHM_NOTDRN)) : 0)
	 goto a;
  if (source->yielded ? 0 : pthread_cond_wait (&(source->termination), &(source->lock)) ? IER(141) : 0)
	 goto a;
  if (source->yielded ? (! (done = 1)) : IER(142))
	 goto a;
  result = source->result;
  *err = (*err ? *err : source->status);
 a: if (pthread_mutex_unlock (&(source->lock)) ? IER(143) : 0)
	 source->valid = MUGGLE(40);
  return ((done ? killable (source, err) : 0) ? result : NULL);
}







static void*
tethered_read (source, err)
	  nthm_pipe source;
	  int *err;

	  // Read from a source whose drain is running in the current
	  // context, unless the drain is killed. The source knows it has a
	  // drain and so will signal the drain's progress rather than its
	  // own termination signal when it terminates. If the drain has
	  // other sources than the one it's trying to read, one of the
	  // others might signal it first and it may have to continue
	  // waiting, hence the loop.
{
  nthm_pipe drain;
  void *result;
  int status;
  int done;

  if ((!source) ? IER(144) : (source->valid != MAGIC) ? IER(145) : source->reader ? 0 : IER(146))
	 return NULL;
  if (((drain = current_context ()) == source->reader->pipe) ? 0 : (*err = (*err ? *err : NTHM_NOTDRN)))
	 return NULL;
  if (((!drain) ? IER(147) : (drain->valid != MAGIC) ? IER(148) : 0) ? (source->valid = MUGGLE(41)) : 0)
	 return NULL;
  if ((pthread_mutex_lock (&(drain->lock)) ? IER(149) : 0) ? (drain->valid = MUGGLE(42)) : 0)
	 return NULL;
  while (! (done = (source->yielded ? 1 : drain->killed)))
	 if (pthread_cond_wait (&(drain->progress), &(drain->lock)) ? IER(150) : 0)
		goto a;
  *err = (*err ? *err : source->yielded ? source->status : NTHM_KILLED);
  result = (source->yielded ? source->result : NULL);
 a: if (pthread_mutex_unlock (&(drain->lock)) ? IER(151) : 0)
	 drain->valid = MUGGLE(43);
  return ((done ? killable (source, err) : 0) ? result : NULL);
}






// --------------- source-side protocol support ------------------------------------------------------------






static void
untethered_yield (s, err)
	  nthm_pipe s;
	  int *err;

	  // Pipes that have no drain indicate termination by atomically
	  // setting their yielded flag and signaling their termination
	  // condition. The pipe is assumed to be locked on entry to this
	  // function and is unlocked on exit. If the thread is already
	  // killed at this point the pipe will be retired when pooled.
{
  if ((! s) ? IER(152) : (s->valid != MAGIC) ? IER(153) : 0)
	 return;
  s->yielded = 1;
  if (s->killed ? 0 : pthread_cond_signal (&(s->termination)) ? IER(154) : 0)
	 s->valid = MUGGLE(44);
  else if (s->status ? 0 : (s->status = *err))
	 *err = 0;
  if (pthread_mutex_unlock (&(s->lock)) ? IER(155) : 0)
	 s->valid = MUGGLE(45);
}







static void
tethered_yield (s, err)
	  nthm_pipe s;
	  int *err;

	  // Pipes that have a drain d indicate termination by atomically
	  // setting their yielded flag, moving themselves from the drain's
	  // blocker list to its finishers queue, and signaling the drain's
	  // progress condition. The source s is assumed to be locked on
	  // entry.
{
  pipe_list b;  // the entry in the drain's blocker list corresponding to the source
  nthm_pipe d;

  if ((! s) ? IER(156) : (s->valid != MAGIC) ? IER(157) : 0)
	 return;
  if (s->killed ? IER(158) : s->reader ? 0 : IER(159))
	 goto a;
  if ((!(d = s->reader->pipe)) ? IER(160) : (d->valid != MAGIC) ? IER(161) : 0)
	 goto a;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(162) : 0) ? (d->valid = MUGGLE(46)) : 0)
	 goto a;
  if (! severed (b = s->reader->complement, err))
	 goto b;
  if ((s->yielded = (enqueued (b, &(d->finishers), &(d->finisher_queue), err) ? 1 : freed (b, err) ? 0 : ! IER(163))))
	 if (pthread_cond_signal (&(d->progress)) ? IER(164) : 0)
		d->valid = MUGGLE(47);
 b: if (pthread_mutex_unlock (&(d->lock)) ? IER(165) : 0)
	 d->valid = MUGGLE(48);
 a: if (pthread_mutex_unlock (&(s->lock)) ? IER(166) : 0)
	 s->valid = MUGGLE(49);
}








static void
yield (source, err)
	  nthm_pipe source;
	  int *err;

	  // Lock the source to stop it changing between tethered and
	  // untethered, and then yield according to the corresponding
	  // protocol. The source has to be flushed before being allowed
	  // into its reader's finishers queue.
{
  flush (&source, err);
  if ((!source) ? IER(167) : (source->valid != MAGIC) ? IER(168) : 0)
	 return;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(169) : 0) ? (source->valid = MUGGLE(50)) : 0)
	 return;
  if (source->reader)
	 tethered_yield (source, err);
  else
	 untethered_yield (source, err);
}








static void
relay_race (err)
	  int *err;

	  // Join with previous threads until they're all finished or
	  // there's a lull. Then schedule the current thread to be joined
	  // with the next one to finish, and wait for its signal before
	  // exiting. If the current thread is the last one running, then
	  // signal the exit routine to join with it instead. This waiting
	  // doesn't block the user code in the current thread because it
	  // yielded before this function was called.
{
  pthread_t joiner;
  void *leak;

  if (deadlocked ? 1 : pthread_mutex_lock (&junction_lock) ? IER(170) : 0)
	 return;
  while (ready_to_join ? ready_to_join-- : 0)
	 {
		joiner = joinable_thread;
		if (pthread_cond_signal (&junction) ? IER(171) : 0)
		  goto a;
		if (pthread_mutex_unlock (&junction_lock) ? IER(172) : 0)
		  return;
		if (pthread_join (joiner, &leak) ? IER(173) : 0)
		  goto a;
		if (leak ? IER(174) : pthread_mutex_lock (&junction_lock) ? IER(175) : 0)
		  return;
	 }
  ready_to_join++;
  joinable_thread = pthread_self ();
  if (--running_threads ? 1 : pthread_cond_signal (&last_thread) ? (! IER(176)) : 1)
	 if (pthread_cond_wait (&junction, &junction_lock))
		IER(177);
 a: if (pthread_mutex_unlock (&junction_lock))
	 IER(178);
}







static void*
manager (void_pointer)
	  void *void_pointer;

	  // Used as a start routine for pthread_create, this function runs
	  // the given function in the created thread and yields when
	  // finished.
{
  thread_spec t;
  nthm_pipe s;
  int err;

  err = 0;
  if ((t = (thread_spec) void_pointer) ? 0 : (deadlocked = err = THE_IER(179)))
	 goto a;
  if (((!(s = t->pipe)) ? 1 : (s->valid != MAGIC) ? 1 : ! set_context (s, &err)) ? (deadlocked = err = THE_IER(180)) : 0)
	 goto b;
  if (pthread_mutex_lock (&junction_lock) ? (deadlocked = err = THE_IER(181)) : ! ++running_threads)
	 goto c;
  if (pthread_mutex_unlock (&junction_lock) ? (deadlocked = err = THE_IER(182)) : 0)
	 goto c;
  t->pipe = NULL;
  s->result = (t->operator) (t->operand, &(s->status));
  yield (s, &err);
 c: clear_context (&err);
 b: unspecify (t, &err);
  relay_race (&err);
 a: globally_throw (err);
  pthread_exit (NULL);
}






// --------------- root pipe list management ---------------------------------------------------------------







static int
quiescent (p, err)
	  nthm_pipe p;
	  int *err;

	  // Safely test whether a pipe is ready to be retired.
{
  int q;

  if ((! p) ? IER(183) : (p->valid != MAGIC) ? IER(184) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(p->lock)) ? IER(185) : 0) ? (p->valid = MUGGLE(51)) : 0)
	 return 0;
  q = (p->blockers ? 0 : p->killed ? p->yielded : 0);
  return (((pthread_mutex_unlock (&(p->lock)) ? IER(186) : 0) ? (p->valid = MUGGLE(52)) : 0) ? 0 : q);
}







static int
pooled (drain, err)
	  nthm_pipe drain;
	  int *err;

	  // Append a pipe to the root pool if it's not quiescent and also
	  // take the opportunity to remove quiescent pipes from the
	  // pool. Return non-zero if the pipe is consumed. The pipe might
	  // be already in the pool or might be already quiescent or both
	  // or neither.
{
  pipe_list *b;
  nthm_pipe p;

  if (pthread_mutex_lock (&root_lock) ? IER(187) : 0)
	 return 0;
  for (b = &root_pipes; *b; drain = ((p == drain) ? NULL : drain))
	 if (! quiescent (p = (*b)->pipe, err))
		b = &((*b)->next_pipe);
	 else if ((retired (p, err) ? unilaterally_delisted (*b, err) : 0) ? 0 : pthread_mutex_unlock (&root_lock) ? IER(188) : 1)
		return 0;
  if (drain ? (drain->pool ? (! IER(189)) : 1) : 0)
	 if (quiescent (drain, err) ? (retired (drain, err) ? (! ! (drain = NULL)) : ! IER(190)) : 1)
		if ((drain->pool = pipe_list_of (drain, err)) ? (*(drain->pool->previous_pipe = b) = drain->pool) : NULL)
		  drain = NULL;
  return ((pthread_mutex_unlock (&root_lock) ? IER(191) : 0) ? 0 : ! drain);
}







static int
unpooled (p, err)
	  nthm_pipe p;
	  int *err;

	  // Take a top pipe out of the root pool if it has no blockers or finishers.
{
  if ((! p) ? IER(192) : pthread_mutex_lock (&root_lock) ? IER(193) : 0)
	 return 0;
  if (p->pool ? (p->top ? (p->blockers ? 0 : ! p->finishers) : 0) : 0)
	 p = ((unilaterally_delisted (p->pool, err) ? retired (p, err) : 0) ? NULL : p);
  return (((pthread_mutex_unlock (&root_lock)) ? IER(194) : 0) ? 0 : ! p);
}






// --------------- teardown routines to be run at exit -----------------------------------------------------






static void
globally_catch (err)
	  int *err;

	  // Safely detect the global error code.
{
  if (*err ? 1 : pthread_mutex_lock (&error_lock) ? IER(195) : 0)
	 return;
  *err = global_error;
  if (pthread_mutex_unlock (&error_lock))
	 IER(196);
}






static int
reaped (q, err)
	  pipe_list *q;
	  int *err;

	  // Transfer the whole list of root pipes to q.
{
  if ((! q) ? IER(197) : pthread_mutex_lock (&root_lock) ? IER(198) : 0)
	 return 0;
  if (((*q) = root_pipes) ? ((*q)->previous_pipe = q) : NULL)
	 root_pipes = NULL;
  return (pthread_mutex_unlock (&root_lock) ? (! IER(199)) : 1);
}







static void
eradicate (err)
	  int *err;

	  // Sequentially free the root pipes.
{
  pipe_list q;

  for (q = NULL; q ? 1 : reaped (&q, err) ? (! ! q) : 0;)
	 {
		if (q->pipe ? 0 : IER(200))
		  return;
		while (q->pipe->children)
		  q->pipe = q->pipe->children;
		if (set_context (q->pipe, err) ? 0 : IER(201))
		  return;
		flush (&(q->pipe), err);
		if ((! retired (q->pipe, err)) ? IER(202) : (! unilaterally_delisted (q, err)) ? IER(203) : 0)
		  return;
	 }
}







static void
synchronize (err)
	  int *err;

	  // Join with the last thread still running.
{
  void *leak;

  if (deadlocked ? 1 : pthread_mutex_lock (&junction_lock) ? IER(204) : 0)
	 return;
  if ((! running_threads) ? 0 : pthread_cond_wait (&last_thread, &junction_lock) ? IER(205) : 0)
	 goto a;
  if (pthread_cond_signal (&junction) ? IER(206) : 0)
	 goto a;
  if (pthread_mutex_unlock (&junction_lock) ? IER(207) : 0)
	 return;
  if (pthread_join (joinable_thread, &leak) ? IER(208) : leak ? IER(209) : 1)
	 return;
 a: if (pthread_mutex_unlock (&junction_lock))
	 IER(210);
}






static void
release_pthread_resources (err)
	  int *err;

	  // Release pthread related resources.
{
  if (pthread_mutexattr_destroy (&mutex_attribute))
	 IER(211);
  if (pthread_mutex_destroy (&root_lock))
	 IER(212);
  if (pthread_mutex_destroy (&junction_lock))
	 IER(213);
  if (pthread_cond_destroy (&junction))
	 IER(214);
  if (pthread_cond_destroy (&last_thread))
	 IER(215);
  if (pthread_attr_destroy (&thread_attribute))
	 IER(216);
  if (pthread_key_delete (cursor))
	 IER(217);
}







static void
teardown ()

	  // Free the root pipes and other static storage. This function is
	  // installed by atexit() in the initialization routine.
{
  int err;

  err = 0;
  eradicate (&err);
  synchronize (&err);
  release_pthread_resources (&err);
  globally_catch (&err);
  if (pthread_mutex_destroy (&error_lock))
	 err = (err ? err : THE_IER(218));
  if (err)
	 fprintf (stderr, "%s\n", nthm_strerror (err)); 
#ifdef MEMTEST
  if (pipes)
	 fprintf (stderr, "%ld unreclaimed pipe%s\n", pipes, pipes == 1 ? "" : "s");
  if (pipe_lists)
	 fprintf (stderr, "%ld unreclaimed pipe list%s\n", pipe_lists, pipe_lists == 1 ? "" : "s");
  if (thread_specs)
	 fprintf (stderr, "%ld unreclaimed thread specification%s\n", thread_specs, thread_specs == 1 ? "" : "s");
#endif
}







// --------------- public-facing API -----------------------------------------------------------------------







const char*
nthm_strerror (err)
	  int err;

	  // Return a short explanation of the given error code. This
	  // function is the only one that isn't thread safe.

#define IER_FMT "nthm-%d.%d.%d: internal error code %d"
#define POSIX_FMT "nthm: %s"

{
  static char error_buffer[80 + sizeof(IER_FMT)];

  if (err >= 0)
	 {
		errno = 0;
		sprintf (error_buffer, POSIX_FMT, strerror (err));
		if (! errno)
		  return error_buffer;
		return "nthm: undiagnosed POSIX error";
	 }
  switch (err)
	 {
	 case NTHM_UNMANT: return "nthm: inapplicable operation in an unmanaged thread";
	 case NTHM_NOTDRN: return "nthm: attempt to control a non-locally tethered pipe";
	 case NTHM_NULPIP: return "nthm: null pipe";
	 case NTHM_INVPIP: return "nthm: corrupted or invalid pipe";
	 case NTHM_KILLED: return "nthm: interrupted by a kill notification";
	 case NTHM_UNDFLO: return "nthm: scope underflow";
	 default:
		sprintf (error_buffer, IER_FMT, NTHM_VERSION_MAJOR, NTHM_VERSION_MINOR, NTHM_VERSION_PATCH, -err);
		return error_buffer;
	 }
}








nthm_pipe
nthm_open (operator, operand, err)
	  nthm_worker operator;
	  void *operand;
	  int *err;

	  // Return a pipe to a newly created thread tethered to the
	  // currently running thread.
{
  nthm_pipe source;
  thread_spec spec;
  nthm_pipe drain;
  pthread_t c;
  int e;

  err = (err ? err : &ignored);
  if (deadlocked ? IER(219) : (!(drain = current_or_new_context (err))) ? 1 : (drain->valid != MAGIC) ? IER(220) : 0)
	 return NULL;
  if (drain->yielded ? IER(221) : heritably_killed_or_yielded (drain, err) ? (*err = (*err ? *err : NTHM_KILLED)) : 0)
	 return NULL;
  if (!(spec = thread_spec_of (source = new_pipe (err), operator, operand, err)))
	 return NULL;
  if (! tethered (source, drain, err))
	 goto a;
  if (!(e = pthread_create (&c, &thread_attribute, &manager, spec)))
	 return source;
  *err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(222));
  if (! untethered (source, err))
	 IER(223);
 a: unspecify (spec, err);
  return NULL;
}








void*
nthm_read (source, err)
	  nthm_pipe source;
	  int *err;

	  // Perform a blocking read on a pipe and retire it after reading,
	  // provided the pipe is not tethered to any other thread.
{
  nthm_pipe drain;

  err = (err ? err : &ignored);
  if (deadlocked ? IER(224) : source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return NULL;
  if ((source->valid == MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return NULL;
  if (!(drain = current_context ()))
	 return untethered_read (source, err);
  return ((tethered (source, drain, err) ? 1 : ! IER(225)) ? tethered_read (source, err) : NULL);
}








int
nthm_busy (source, err)
	  nthm_pipe source;
	  int *err;

	  // Return non-zero if reading the source would have blocked.
{
  int busy;

  err = (err ? err : &ignored);
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return 0;
  if ((source->valid == MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(226) : 0) ? (source->valid = MUGGLE(53)) : 0)
	 return 0;
  busy = ! source->yielded;
  return (((pthread_mutex_unlock (&(source->lock)) ? IER(227) : 0) ? (source->valid = MUGGLE(54)) : 0) ? 0 : busy);
}









int
nthm_blocked (err)
	  int *err;

	  // Return non-zero if a call to nthm_select would have blocked.
{
  nthm_pipe drain;
  int blocked;

  err = (err ? err : &ignored);
  if ((!(drain = current_context ())) ? 1 : (drain->valid != MAGIC) ? IER(228) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(drain->lock)) ? IER(229) : 0) ? (drain->valid = MUGGLE(55)) : 0)
	 return 0;
  blocked = (drain->finishers ? 0 : ! ! (drain->blockers));
  return (((pthread_mutex_unlock (&(drain->lock)) ? IER(230) : 0) ? (drain->valid = MUGGLE(56)) : 0) ? 0 : blocked);
}









nthm_pipe
nthm_select (err)
	  int *err;

	  // Return the next readable pipe tethered to the currently
	  // running thread if any, blocking if necessary until a readable
	  // pipe is available, but with blocking interrupted if the
	  // currently running thread is killed.
{
  nthm_pipe s, d;
  int k;

  err = (err ? err : &ignored);
  if (deadlocked ? IER(231) : (!(d = current_context ())) ? 1 : (d->valid == MAGIC) ? 0 : IER(232))
	 return NULL;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(233) : 0) ? (d->valid = MUGGLE(57)) : 0)
	 return NULL;
  s = NULL;
  for (k = 0; (k = d->killed) ? 0 : (s = dequeued (d->finishers, &(d->finisher_queue), err)) ? 0 : ! ! (d->blockers);)
	 if ((pthread_cond_wait (&(d->progress), &(d->lock)) ? IER(234) : 0) ? (d->valid = MUGGLE(58)) : 0)
		break;
  if ((pthread_mutex_unlock (&(d->lock)) ? IER(235) : 0) ? (d->valid = MUGGLE(59)) : 0)
	 return NULL;
  *err = (*err ? *err : k ? NTHM_KILLED : 0);
  return s;
}








void
nthm_truncate (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tell a pipe to truncate its output.
{
  unsigned bumped;

  err = (err ? err : &ignored);
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid == MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return;
  if ((!(source->reader)) ? 0 : (source->reader->pipe == current_context ()) ? 0 : (*err = (*err ? *err : NTHM_NOTDRN)))
	 return;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(236) : 0) ? (source->valid = MUGGLE(60)) : 0)
	 return;
  if ((bumped = source->truncation + 1))  // don't increment if it was already the maximum value
	 source->truncation = bumped;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(237) : 0)
	 source->valid = MUGGLE(61);
}








void
nthm_truncate_all (err)
	  int *err;

	  // Tell all threads tethered to the current thread to truncate
	  // their output.
{
  nthm_pipe drain;
  unsigned bumped;

  err = (err ? err : &ignored);
  if ((!(drain = current_context ())) ? 1 : (drain->valid != MAGIC) ? IER(238) : 0)
	 return;
  if ((pthread_mutex_lock (&(drain->lock)) ? IER(239) : 0) ? (drain->valid = MUGGLE(62)) : 0)
	 return;
  if ((bumped = drain->truncation + 1))
	 drain->truncation = bumped;
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(240) : 0)
	 drain->valid = MUGGLE(63);
}









int
nthm_truncated (err)
	  int *err;

	  // An introspective predicate polled by user code running in a
	  // source pipe indicates that the source is free to return a
	  // partial result to the drain indicative of its current
	  // progress.
{
  nthm_pipe source;

  err = (err ? err : &ignored);
  return ((source = current_context ()) ? heritably_truncated (source, err) : (*err = (*err ? *err : NTHM_UNMANT)));
}








void
nthm_kill (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tell a pipe to pack it in and make it disappear.
{
  err = (err ? err : &ignored);
  if ((!source) ? (*err = (*err ? *err : NTHM_NULPIP)) : 0)
	 return;
  if ((source->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : 0)
	 return;
  if (! killable (source, err))
	 IER(241);
}








void
nthm_kill_all (err)
	  int *err;

	  // Tell all pipes tethered to the current one to pack it in.
{
  nthm_pipe drain, parent;

  err = (err ? err : &ignored);
  if (!(drain = current_context ()))
	 return;
  if ((drain->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : 0)
	 return;
  parent = drain->parents;
  drain->parents = NULL;
  flush (&drain, err);
  if (drain ? 1 : ! IER(242))
	 drain->parents = parent;
}









int
nthm_killed (err)
	  int *err;

	  // An introspective predicate polled by user code running in a
	  // source pipe indicates that any result it returns ultimately
	  // will be ignored.
{
  nthm_pipe source;
  int dead;

  err = (err ? err : &ignored);
  if (!(source = current_context ()) ? 1 : (source->valid != MAGIC) ? IER(243) : 0)
	 return 1;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(244) : 0) ? (source->valid = MUGGLE(64)) : 0)
	 return 1;
  dead = source->killed;
  return (((pthread_mutex_unlock (&(source->lock)) ? IER(245) : 0) ? (source->valid = MUGGLE(65)) : 0) ? 1 : dead);
}









void
nthm_untether (source, err)
	  nthm_pipe source;
	  int *err;

	  // Emancipate a source from its drain so that it will not be
	  // reclaimed automatically when the drain exits and will remain
	  // readable by others.
{
  err = (err ? err : &ignored);
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : 0)
	 return;
  if (! untethered (source, err))
	 IER(246);
}








void
nthm_tether (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tether an untethered source to the currently running thread so
	  // that it will be taken into account by nthm_select.
{
  nthm_pipe drain;

  err = (err ? err : &ignored);
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : !(drain = current_or_new_context (err)))
	 return;
  if ((drain->valid != MAGIC) ? IER(247) : drain->yielded ? IER(248) : 0)
	 return;
  if (heritably_killed_or_yielded (drain, err))
	 *err = (*err ? *err : NTHM_KILLED);
  else if (! tethered (source, drain, err))
	 IER(249);
}









void
nthm_enter_scope (err)
	  int *err;

	  // Restrict attention to pipes opened subsequently.
{
  nthm_pipe child, parent;

  err = (err ? err : &ignored);
  if (deadlocked ? IER(250) : (!(parent = current_or_new_context (err))) ? 1 : (parent->valid != MAGIC) ? IER(251) : 0)
	 return;
  if (parent->yielded ? IER(252) : heritably_killed_or_yielded (parent, err) ? (*err = (*err ? *err : NTHM_KILLED)) : 0)
	 return;
  if (parent->children ? IER(253) : ! (child = new_pipe (err)))
	 return;
  if (set_context (child, err) ? 0 : retired (child, err) ? 1 : IER(254))
	 return;
  child->parents = parent;
  parent->children = child;
}









void
nthm_exit_scope (err)
	  int *err;

	  // Resume previous attention span. Any pipes opened since the
	  // scope was created continue untethered.
{
  nthm_pipe parent, child;
  pipe_list orphans;

  err = (err ? err : &ignored);
  if (((child = current_context ()) ? (parent = child->parents) : 0) ? 0 : (*err = (*err ? *err : NTHM_UNDFLO)))
	 return;
  if ((child->valid != MAGIC) ? IER(255) : child->children ? IER(256) : (child != parent->children) ? IER(257) : 0)
	 return;
  child->parents = parent->children = NULL;
  do
	 {
		if ((pthread_mutex_lock (&(child->lock)) ? IER(258) : 0) ? (child->valid = MUGGLE(66)) : 0)
		  break;
		orphans = (child->finishers ? child->finishers : child->blockers);
		if ((pthread_mutex_unlock (&(child->lock)) ? IER(259) : 0) ? (child->valid = MUGGLE(67)) : 0)
		  break;
	 } while (orphans ? untethered (orphans->pipe, err) : 0);
  if (retired (child, err) ? 1 : IER(260))
	 if (unpooled (parent, err) ? 1 : set_context (parent, err) ? 0 : ! IER(261))
		clear_context (err);
}
