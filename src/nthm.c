/*
  nthm -- non-preemptive thread hierarchy manager

  copyright (c) 2020 Dennis Furey

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
#define PIPE_MAGIC 1887434018

// the n-th non-magic value; as with internal errors, distinguishable values are used only to log the point of detection
#define PIPE_MUGGLE(n) n

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
  int valid;                  // holds a muggle if any pthread operation or integrity check fails, PIPE_MAGIC otherwise
  int killed;                 // set either by user code or by the reader yielding without having read from the pipe
  int yielded;                // set by the thread when its result is finished being computed
  pipe_list reader;           // a list of at most one pipe designated to read the result from this one
  pipe_list blockers;         // a list of pipes created by this one whose results are awaited
  pipe_list finishers;        // a list of pipes created by this one whose results are available in the order they finished
  pipe_list finisher_queue;   // points to the most recently finished pipe
  pthread_cond_t termination; // signaled by the thread to indicate termination if it has no reader
  pthread_cond_t progress;    // signaled by the reader thread when this one is killed or by any blocker that terminates
  pthread_mutex_t lock;       // secures mutually exclusive access for otherwise non-atomic operations on this structure
  unsigned truncation;        // set by user code when a partial result is acceptable
  void *result;               // returned by user code in the thread when it terminates
  int status;                 // an error code returned by user code if not overridden by other conditions
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

// for exclusive access to the global error code
static pthread_mutex_t error_lock;

// created threads have these attributes
static pthread_attr_t thread_attribute;

// created mutexes have these attributes
static pthread_mutexattr_t mutex_attribute;

// to be freed on exit
static pipe_list root_pipes = NULL;

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
static pthread_mutex_t thread_reclamation_lock;

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
  if ((!a) ? IER(21) : pthread_attr_init (a) ? IER(22) : 0)
	 return 0;
#ifdef USE_SMALL_STACKS
  if (pthread_attr_setstacksize (a, PTHREAD_STACK_MIN + NTHM_STACK_MIN) ? IER(23) : 0)
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
  if ((!a) ? IER(24) : pthread_mutexattr_init (a) ? IER(25) : 0)
	 return 0;
  if (!(pthread_mutexattr_settype (a, PTHREAD_MUTEX_ERRORCHECK) ? IER(26) : 0))
	 return 1;
  pthread_mutexattr_destroy (a);
  return 0;
}







static void
initialization_routine ()

    // Initialize all static storage or bust.
{
  int err;

  err = 0;
  if (! error_checking_mutex_type (&mutex_attribute, &err))
	 {
		fprintf (stderr, "%s\n", nthm_strerror (err));
		exit (EXIT_FAILURE);
	 }
  if (pthread_mutex_init (&error_lock, &mutex_attribute))
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		fprintf (stderr, "%s\n", nthm_strerror (THE_IER(27)));
		exit (EXIT_FAILURE);
	 }
  if (pthread_mutex_init (&thread_reclamation_lock, &mutex_attribute))
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		pthread_mutex_destroy (&error_lock);
		fprintf (stderr, "%s\n", nthm_strerror (THE_IER(28)));
		exit (EXIT_FAILURE);
	 }
  if (pthread_cond_init (&junction, NULL))
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		pthread_mutex_destroy (&error_lock);
		pthread_mutex_destroy (&thread_reclamation_lock);
		fprintf (stderr, "%s\n", nthm_strerror (THE_IER(29)));
		exit (EXIT_FAILURE);
	 }
  if (pthread_cond_init (&last_thread, NULL))
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		pthread_mutex_destroy (&error_lock);
		pthread_mutex_destroy (&thread_reclamation_lock);
		pthread_cond_destroy (&junction);
		fprintf (stderr, "%s\n", nthm_strerror (THE_IER(30)));
		exit (EXIT_FAILURE);
	 }
  if (! stack_limited_thread_type (&thread_attribute, &err))
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		pthread_mutex_destroy (&error_lock);
		pthread_mutex_destroy (&thread_reclamation_lock);
		pthread_cond_destroy (&junction);
		pthread_cond_destroy (&last_thread);
		fprintf (stderr, "%s\n", nthm_strerror (THE_IER(31)));
		exit (EXIT_FAILURE);
	 }
  if (pthread_key_create (&cursor, NULL))
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		pthread_mutex_destroy (&error_lock);
		pthread_mutex_destroy (&thread_reclamation_lock);
		pthread_cond_destroy (&junction);
		pthread_cond_destroy (&last_thread);
		pthread_attr_destroy (&thread_attribute);
		fprintf (stderr, "%s\n", nthm_strerror (THE_IER(32)));
		exit (EXIT_FAILURE);
	 }
  if (atexit (teardown))
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		pthread_mutex_destroy (&error_lock);
		pthread_mutex_destroy (&thread_reclamation_lock);
		pthread_cond_destroy (&junction);
		pthread_cond_destroy (&last_thread);
		pthread_attr_destroy (&thread_attribute);
		pthread_key_delete (cursor);
		fprintf (stderr, "%s\n", nthm_strerror (THE_IER(33)));
		exit (EXIT_FAILURE);
	 }
}






static inline void
initialize_cursor ()

    // The key used by all threads has to be initialized just once for the
    // whole process.
{
  pthread_once (&once_control, initialization_routine);
}






static void
globally_throw (err)
	  int err;

	  // Safely assign the global error code.
{
  if (! err)
	 return;
  pthread_mutex_lock (&error_lock);
  if (! global_error)
	 global_error = err;
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
  *err = (*err ? *err : (e == ENOMEM) ? e : (e = EAGAIN) ? e : THE_IER(34));
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
  if ((!t) ? IER(35) : (!(t->previous_pipe)) ? IER(36) : 0)
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
  if ((!r) ? IER(37) : r->next_pipe ? IER(38) : r->previous_pipe ? IER(39) : 0)
	 return 0;
  if (r->complement)
	 {
		if ((r->complement->complement == r) ? 0 : IER(40))
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

  if ((!f) ? IER(41) : ! unilaterally_delisted (f->complement, err))
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

  if (((!f) != !*q) ? IER(42) : ! f)
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

  if ((!r) ? IER(43) : (c = r->complement) ? 0 : IER(44))
	 return NULL;
  if (c == f)
	 return dequeued (f, q, err);
  if (c != *q)
	 return popped (c, err);
  if (f ? 0 : IER(45))
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
  if ((!r) ? IER(46) : (!b) ? IER(47) : (r->complement != b) ? IER(48) : (b->complement != r) ? IER(49) : 0)
	 return 0;
  return (freed (r, err) ? freed (b, err) : 0);
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

  if (p ? 0 : IER(50))
	 return NULL;
  if ((t = (pipe_list) malloc (sizeof (*t))) ? 0 : (*err = (*err ? *err : ENOMEM)))
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
  if ((!b) ? IER(51) : (!r) ? IER(52) : !(*r = pipe_list_of (d, err)))
	 return 0;
  if ((*b = pipe_list_of (s, err)))
	 {
		(*b)->complement = *r;
		(*r)->complement = *b;
		return 1;
	 }
  if (freed (*r, err))
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
  if ((!t) ? IER(53) : (!b) ? IER(54) : t->previous_pipe ? IER(55) : t->next_pipe ? IER(56) : 0)
	 return 0;
  t->previous_pipe = b;
  if ((t->next_pipe = *b))
	 (*b)->previous_pipe = &(t->next_pipe);
  *b = t;
  return 1;
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
  if ((!f) ? IER(57) : (!q) ? IER(58) : ((!*f) != !*q) ? IER(59) : (!*q) ? 0 : (*q)->next_pipe ? IER(60) : 0)
	 return 0;
  if (*q)
	 return  ((! pushed (t, &((*q)->next_pipe), err)) ? 0 : (*q = (*q)->next_pipe) ? 1 : ! IER(61));
  return ((! pushed (t, f, err)) ? 0 : (*q = *f) ? 1 : ! IER(62));
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

  if ((!source) ? IER(63) : (source->valid != PIPE_MAGIC) ? IER(64) : 0)
	 return 0;
  if (pthread_mutex_lock (&(source->lock)) ? IER(65) : 0)
	 {
		source->valid = PIPE_MUGGLE(1);
		return 0;
	 }
  if (source->killed ? 1 : source->yielded)
	 {
		if (pthread_mutex_unlock (&(source->lock)) ? IER(66) : 0)
		  source->valid = PIPE_MUGGLE(2);
		return 1;
	 }
  while (source->reader)
	 {
		if ((!(drain = source->reader->pipe)) ? IER(67) : (drain->valid != PIPE_MAGIC) ? IER(68) : 0)
		  {
			 pthread_mutex_unlock (&(source->lock));
			 source->valid = PIPE_MUGGLE(3);
			 return 0;
		  }
		if (pthread_mutex_lock (&(drain->lock)) ? IER(69) : 0)
		  {
			 if (pthread_mutex_unlock (&(source->lock)) ? IER(70) : 0)
				source->valid = PIPE_MUGGLE(4);
			 drain->valid = PIPE_MUGGLE(5);
			 return 0;
		  }
		if (drain->yielded ? 1 : drain->killed)
		  {
			 if (pthread_mutex_unlock (&(source->lock)) ? IER(71) : 0)
				source->valid = PIPE_MUGGLE(6);
			 if (pthread_mutex_unlock (&(drain->lock)) ? IER(72) : 0)
				drain->valid = PIPE_MUGGLE(7);
			 return 1;
		  }
		if (pthread_mutex_unlock (&(source->lock)) ? IER(73) : 0)
		  {
			 source->valid = PIPE_MUGGLE(8);
			 return 0;
		  }
		source = drain;
	 }
  if (pthread_mutex_unlock (&(source->lock)) ? IER(74) : 0)
	 source->valid = PIPE_MUGGLE(9);
  return 0;
}







static int
heritably_truncated (source, err)
	  nthm_pipe source;
	  int *err;

	  // Detect whether source has been truncated either explicitly
	  // or due to any of its drains being truncated.
{
  nthm_pipe drain;

  if ((!source) ? IER(75) : (source->valid != PIPE_MAGIC) ? IER(76) : 0)
	 return 0;
  if (pthread_mutex_lock (&(source->lock)) ? IER(77) : 0)
	 {
		source->valid = PIPE_MUGGLE(10);
		return 0;
	 }
  if (source->truncation)
	 {
		if (pthread_mutex_unlock (&(source->lock)) ? IER(78) : 0)
		  source->valid = PIPE_MUGGLE(11);
		return 1;
	 }
  while (source->reader)
	 {
		if ((!(drain = source->reader->pipe)) ? IER(79) : (drain->valid != PIPE_MAGIC) ? IER(80) : 0)
		  {
			 pthread_mutex_unlock (&(source->lock));
			 source->valid = PIPE_MUGGLE(12);
			 return 0;
		  }
		if (pthread_mutex_lock (&(drain->lock)) ? IER(81) : 0)
		  {
			 if (pthread_mutex_unlock (&(source->lock)) ? IER(82) : 0)
				source->valid = PIPE_MUGGLE(13);
			 drain->valid = PIPE_MUGGLE(14);
			 return 0;
		  }
		if (drain->truncation)
		  {
			 if (pthread_mutex_unlock (&(source->lock)) ? IER(83) : 0)
				source->valid = PIPE_MUGGLE(15);
			 if (pthread_mutex_unlock (&(drain->lock)) ? IER(84) : 0)
				drain->valid = PIPE_MUGGLE(16);
			 return 1;
		  }
		if (pthread_mutex_unlock (&(source->lock)) ? IER(85) : 0)
		  {
			 source->valid = PIPE_MUGGLE(17);
			 return 0;
		  }
		source = drain;
	 }
  if (pthread_mutex_unlock (&(source->lock)) ? IER(86) : 0)
	 source->valid = PIPE_MUGGLE(18);
  return 0;
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
  if (e = pthread_cond_init (&(source->termination), NULL))
	 {
		*err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(87));
		free (source);
		return NULL;
	 }
  if (e = pthread_cond_init (&(source->progress), NULL))
	 {
		*err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(88));
		pthread_cond_destroy (&(source->termination));
		free (source);
		return NULL;
	 }
  if (e = pthread_mutex_init (&(source->lock), NULL))
	 {
		*err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(89));
		pthread_cond_destroy (&(source->termination));
		pthread_cond_destroy (&(source->progress));
		free (source);
		return NULL;
	 }
#ifdef MEMTEST
  pthread_mutex_lock (&error_lock);
  pipes++;
  pthread_mutex_unlock (&error_lock);
#endif
  source->valid = PIPE_MAGIC;
  return source;
}







static int
tethered (source, drain, err)
	  nthm_pipe source;
	  nthm_pipe drain;
	  int *err;

	  // Tether a source to a drain if it isn't already. If the source
	  // has terminated, then it has to be enqueued in the drain's
	  // finishers, but if it's still running, then it has to be pushed
	  // into the blockers. Locks on both are needed, and the source
	  // is locked first.
{
  int done;
  pipe_list new_reader, new_writer;

  if (drain ? 0 : IER(90))
	 return 0;
  if ((drain->valid != PIPE_MAGIC) ? IER(91) : (!source) ? IER(92) : (source->valid != PIPE_MAGIC) ? IER(93) : 0)
	 return 0;
  if (source->reader ? (source->reader->pipe == drain) : 0)
	 return 1;
  if (source->reader ? (source->reader->pipe != drain) : 0)
	 return ! (*err = (*err ? *err : NTHM_NOTDRN));
  if (! new_complementary_pipe_lists (&new_reader, drain, &new_writer, source, err))
	 return 0;
  if (pthread_mutex_lock (&(source->lock)) ? IER(94) : 0)
	 {
		source->valid = (bilaterally_freed (new_reader, new_writer, err) ? PIPE_MUGGLE(19) : PIPE_MUGGLE(20));
		return 0;
	 }
  if (source->killed ? IER(95) : 0)
	 {
		if (pthread_mutex_unlock (&(source->lock)))
		  source->valid = PIPE_MUGGLE(21);
		return (bilaterally_freed (new_reader, new_writer, err) ? 0 : 0);
	 }
  if (pthread_mutex_lock (&(drain->lock)) ? IER(96) : 0)
	 {
		if (pthread_mutex_unlock (&(source->lock)))
		  source->valid = PIPE_MUGGLE(22);
		drain->valid = (bilaterally_freed (new_reader, new_writer, err) ? PIPE_MUGGLE(23) : PIPE_MUGGLE(24));
		return 0;
	 }
  if (! pushed (new_reader, &(source->reader), err))
	 {
		if (pthread_mutex_unlock (&(source->lock)) ? IER(97) : 0)
		  source->valid = PIPE_MUGGLE(25);
		if (pthread_mutex_unlock (&(drain->lock)) ? IER(98) : 0)
		  drain->valid = PIPE_MUGGLE(26);
		return (bilaterally_freed (new_reader, new_writer, err) ? 0 : 0);
	 }
  if (source->yielded)
	 done = enqueued (new_writer, &(drain->finishers), &(drain->finisher_queue), err);
  else
	 done = pushed (new_writer, &(drain->blockers), err);
  if (done ? 0 : unilaterally_delisted (source->reader, err))
	 source->valid = (freed (new_writer, err) ? PIPE_MUGGLE(27) : PIPE_MUGGLE(28));
  else if (! done)
	 source->valid = (freed (new_writer, err) ? PIPE_MUGGLE(29) : PIPE_MUGGLE(30));
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(99) : 0)
	 drain->valid = PIPE_MUGGLE(31);
  if (pthread_mutex_unlock (&(source->lock)) ? IER(100) : 0)
	 source->valid = PIPE_MUGGLE(32);
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
  if ((!source) ? IER(101) : (source->valid != PIPE_MAGIC) ? IER(102) : 0)
	 return 0;
  if (source->reader ? IER(103) : source->blockers ? IER(104) : 0) 
	 return 0;
  if (source->finishers ? IER(105) : source->finisher_queue ? IER(106) : 0)
	 return 0;
  source->valid = PIPE_MUGGLE(33);
  if (pthread_cond_destroy (&(source->termination)) ? IER(107) : 0)
	 return 0;
  if (pthread_cond_destroy (&(source->progress)) ? IER(108) : 0)
	 return 0;
  if (pthread_mutex_destroy (&(source->lock)) ? IER(109) : 0)
	 return 0;
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
  int dangling;

  if ((!source) ? IER(110) : (source->valid == PIPE_MAGIC) ? 0 : IER(111))
	 return 0;
  if (!(source->reader))
	 return 1;
  if (((drain = current_context ()) != source->reader->pipe) ? (*err = (*err ? *err : NTHM_NOTDRN)) : 0)
	 return 0;
  if ((!drain) ? IER(112) : (drain->valid != PIPE_MAGIC) ? IER(113) : 0)
	 return 0;
  if (pthread_mutex_lock (&(source->lock)) ? IER(114) : 0)
	 {
		source->valid = PIPE_MUGGLE(34);
		return 0;
	 }
  if (pthread_mutex_lock (&(drain->lock)) ? IER(115) : 0)
	 {
		if (pthread_mutex_unlock (&(source->lock)) ? IER(116) : 0)
		  source->valid = PIPE_MUGGLE(35);
		drain->valid = PIPE_MUGGLE(36);
		return 0;
	 }
  if ((source == bilaterally_delisted (source->reader, drain->finishers, &(drain->finisher_queue), err)) ? 0 : IER(117))
	 source->valid = drain->valid = PIPE_MUGGLE(37);
  dangling = (source->killed ? source->yielded : 0);
  if (pthread_mutex_unlock (&(source->lock)) ? IER(118) : 0)
	 source->valid = PIPE_MUGGLE(38);
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(119) : 0)
	 drain->valid = PIPE_MUGGLE(39);
  if (dangling ? ! retired (source, err) : 0)
	 source->valid = PIPE_MUGGLE(40);
  return 1;
}







static int
killable (source, err)
	  nthm_pipe source;
	  int *err;

	  // Untether and kill a pipe, which entails either retiring it if
	  // it has already yielded or signaling its progress otherwise.
{
  if ((!source) ? IER(120) : (source->valid != PIPE_MAGIC) ? IER(121) : 0)
	 return 0;
  if (pthread_mutex_lock (&(source->lock)) ? IER(122) : 0)
	 {
		source->valid = PIPE_MUGGLE(41);
		return 0;
	 }
  source->killed = 1;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(123) : 0)
	 {
		source->valid = PIPE_MUGGLE(42);
		return 0;
	 }
  return untethered (source, err);
}







static int
blockers_killed (drain, err)
	  nthm_pipe drain;
	  int *err;

	  // This function run in the drain's context kills all blockers to
	  // the drain. The drain has to be locked long enough to get a
	  // blocker from the list without the list being mutated, but the
	  // lock has to be let off the drain temporarily before
	  // untethering the blocker.
{
  nthm_pipe blocker;

  if ((!drain) ? IER(124) : (drain->valid != PIPE_MAGIC) ? IER(125) : 0)
	 return 0;
  if (pthread_mutex_lock (&(drain->lock)) ? IER(126) : 0)
	 {
		drain->valid = PIPE_MUGGLE(43);
		return 0;
	 }
  while (drain->blockers)
	 if (!(blocker = drain->blockers->pipe))
		{
		  drain->valid = PIPE_MUGGLE(44);
		  return 0;
		}
	 else if (pthread_mutex_unlock (&(drain->lock)) ? IER(127) : ! killable (blocker, err))
		{
		  drain->valid = PIPE_MUGGLE(45);
		  return 0;
		}
	 else if (pthread_mutex_lock (&(drain->lock)) ? IER(128) : 0)
		{
		  drain->valid = PIPE_MUGGLE(46);
		  return 0;
		}
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(129) : 0)
	 {
		drain->valid = PIPE_MUGGLE(47);
		return 0;
	 }
  return 1;
}







static void
flush (drain, err)
	  nthm_pipe drain;
	  int *err;

	  // Kill both the blockers and the finishers to a drain. The pipes
	  // in the finishers queue are assumed to be flushed already. A
	  // lock is needed here in case one of the blockers finishes
	  // concurrently.
{
  nthm_pipe finisher;

  if ((!drain) ? 1 : (drain->valid != PIPE_MAGIC) ? IER(130) : ! blockers_killed (drain, err))
	 return;
  if (pthread_mutex_lock (&(drain->lock)) ? IER(131) : 0)
	 {
		drain->valid = PIPE_MUGGLE(48);
		return;
	 }
  while ((finisher = dequeued (drain->finishers, &(drain->finisher_queue), err)))
	 {
		if (! retired (finisher, err))
		  finisher->valid = PIPE_MUGGLE(49);
	 }
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(132) : 0)
	 {
		drain->valid = PIPE_MUGGLE(50);
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

  if ((t = (thread_spec) malloc (sizeof (*t))) ? 0 : (*err = (*err ? *err : ENOMEM)))
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






static inline void
unspecify (t)
	  thread_spec t;

	  // Free a thred specification structure.
{
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
  pipe_list t;
  nthm_pipe drain;
  int e;

  if ((drain = current_context ()))
	 return drain;
  if (!(drain = new_pipe (err)))
	 return NULL;
  if (!(t = pipe_list_of (drain, err)))
	 return (retired (drain, err) ? NULL : NULL);
  if (! pushed (t, &root_pipes, err))
	 return ((freed (t, err) ? retired (drain, err) : retired (drain, err)) ? NULL : NULL);
  if (set_context (drain, err))
	 return drain;
  return ((unilaterally_delisted (root_pipes, err) ? retired (drain, err) : retired (drain, err)) ? NULL : NULL);
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
  int status;

  if ((!source) ? IER(133) : (source->valid != PIPE_MAGIC) ? IER(134) : 0)
	 return NULL;
  if (pthread_mutex_lock (&(source->lock)) ? IER(135) : 0)
	 {
		source->valid = PIPE_MUGGLE(51);
		return NULL;
	 }
  if ((source->reader) ? (*err = (*err ? *err : NTHM_NOTDRN)) : 0)
	 {
		if (pthread_mutex_unlock (&(source->lock)) ? IER(136) : 0)
		  source->valid = PIPE_MUGGLE(52);
		return NULL;
	 }
  if (source->yielded ? 0 : pthread_cond_wait (&(source->termination), &(source->lock)) ? IER(137) : 0)
	 {
		pthread_mutex_unlock (&(source->lock));
		source->valid = PIPE_MUGGLE(53);
		return NULL;
	 }
  if (pthread_mutex_unlock (&(source->lock)) ? IER(138) : 0)
	 {
		source->valid = PIPE_MUGGLE(54);
		return NULL;
	 }
  if (source->yielded ? 0 : IER(139))
	 return (killable (source, err) ? NULL : NULL);
  result = source->result;
  status = source->status;
  if (retired (source, err))
	 *err = (*err ? *err : status);
  return result;
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

  if ((!source) ? IER(140) : (source->valid != PIPE_MAGIC) ? IER(141) : source->reader ? 0 : IER(142))
	 return NULL;
  if (((drain = current_context ()) == source->reader->pipe) ? 0 : (*err = (*err ? *err : NTHM_NOTDRN)))
	 return NULL;
  if ((!drain) ? IER(143) : (drain->valid != PIPE_MAGIC) ? IER(144) : 0)
	 {
		source->valid = PIPE_MUGGLE(55);
		return NULL;
	 }
  if (pthread_mutex_lock (&(drain->lock)) ? IER(145) : 0)
	 {
		drain->valid = PIPE_MUGGLE(56);
		return NULL;
	 }
  while ((source->yielded ? 1 : drain->killed) ? 0 : pthread_cond_wait (&(drain->progress), &(drain->lock)) ? IER(146) : 0)
	 {
		pthread_mutex_unlock (&(drain->lock));
		drain->valid = PIPE_MUGGLE(57);
		return NULL;
	 }
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(147) : 0)
	 {
		drain->valid = PIPE_MUGGLE(58);
		return NULL;
	 }
  status = (source->yielded ? source->status : NTHM_KILLED);
  result = (source->yielded ? source->result : NULL);
  if (killable (source, err))
	 *err = (*err ? *err : status);
  return result;
}







// --------------- source-side protocol support ------------------------------------------------------------






static void
untethered_yield (source, err)
	  nthm_pipe source;
	  int *err;

	  // Pipes that have no drain indicate termination by atomically
	  // setting their yielded flag and signaling their termination
	  // condition. The pipe is assumed to be locked on entry and is
	  // unlocked on exit. If the thread is already killed at this
	  // point there's no need to do anything because the pipe is about
	  // to be retired by the killable function.
{
  int dangling;

  if ((!source) ? IER(148) : (source->valid != PIPE_MAGIC) ? IER(149) : 0)
	 return;
  source->yielded = 1;
  if ((dangling = source->killed) ? 0 : pthread_cond_signal (&(source->termination)) ? IER(150) : 0)
	 source->valid = PIPE_MUGGLE(59);
  source->status = (*err ? *err : source->status);
  *err = 0;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(151) : 0)
	 source->valid = PIPE_MUGGLE(60);
  if (dangling ? (! retired (source, err)) : 0)
	 source->valid = PIPE_MUGGLE(61);
}







static void
tethered_yield (source, err)
	  nthm_pipe source;
	  int *err;

	  // Pipes that have a drain indicate termination by atomically
	  // setting their yielded flag, moving themselves from the drain's
	  // blocker list to its finishers queue, and signaling the drain's
	  // progress condition. The source is assumed to be locked on
	  // entry and is unlocked prior to unlocking the drain.
{
  pipe_list blocker;
  nthm_pipe drain;

  if ((!source) ? IER(152) : (source->valid != PIPE_MAGIC) ? IER(153) : (!(source->reader)) ? IER(154) : 0)
	 return;
  if ((!(drain = source->reader->pipe)) ? IER(155) : (drain->valid != PIPE_MAGIC) ? IER(156) : 0)
	 {
		source->valid = (pthread_mutex_unlock (&(source->lock)) ? PIPE_MUGGLE(62) : PIPE_MUGGLE(63));
		return;
	 }
  if (pthread_mutex_lock (&(drain->lock)) ? IER(157) : 0)
	 {
		if (pthread_mutex_unlock (&(source->lock)))
		  source->valid = PIPE_MUGGLE(64);
		drain->valid = PIPE_MUGGLE(65);
		return;
	 }
  if (!(source->killed))
	 {
		source->yielded = 1;
		blocker = source->reader->complement;
		if (! severed (blocker, err))
		  drain->valid = PIPE_MUGGLE(66);
		else
		  {
			 if (! enqueued (blocker, &(drain->finishers), &(drain->finisher_queue), err))
				drain->valid = (freed (blocker, err) ? PIPE_MUGGLE(67) : PIPE_MUGGLE(68));
			 else if (pthread_cond_signal (&(drain->progress)) ? IER(158) : 0)
				drain->valid = PIPE_MUGGLE(69);
		  }
	 }
  source->status = (*err ? *err : source->status);
  *err = 0;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(159) : 0)
	 source->valid = PIPE_MUGGLE(70);
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(160) : 0)
	 drain->valid = PIPE_MUGGLE(71);
}








static void
yield (source, err)
	  nthm_pipe source;
	  int *err;

	  // Lock the source to stop it changing between tethered and
	  // untethered, and then yield according to the corresponding
	  // protocol. The pipe has to be flushed before being allowed into
	  // the reader's finishers queue.
{
  flush (source, err);
  if ((!source) ? IER(161) : (source->valid != PIPE_MAGIC) ? IER(162) : 0)
	 return;
  if (pthread_mutex_lock (&(source->lock)) ? IER(163) : 0)
	 {
		source->valid = PIPE_MUGGLE(72);
		return;
	 }
  if (source->reader)
	 tethered_yield (source, err);
  else
	 untethered_yield (source, err);
}






static void
prepare_for_exit (err)
	  int *err;

	  // Join with previous threads until they're all finished or
	  // there's a lull, then schedule the current thread to be joined
	  // with the next one to finish, and wait for its signal before
	  // exiting. If the current thread is the last one running, then
	  // signal the exit routine to join with it instead. This waiting
	  // doesn't block the user code in the current thread because it
	  // has already returned its result.
{
  pthread_t joining_thread;
  void *retval;

  if (pthread_mutex_lock (&thread_reclamation_lock) ? IER(164) : 0)
	 return;
  while (ready_to_join)
	 {
		ready_to_join = 0;
		joining_thread = joinable_thread;
		if (pthread_cond_signal (&junction) ? IER(165) : 0)
		  {
			 pthread_mutex_unlock (&thread_reclamation_lock);
			 return;
		  }
		if (pthread_mutex_unlock (&thread_reclamation_lock) ? IER(166) : 0)
		  return;
		if (pthread_join (joining_thread, &retval) ? IER(167) : 0)
		  {
			 pthread_mutex_unlock (&thread_reclamation_lock);
			 return;
		  }
		if (pthread_mutex_lock (&thread_reclamation_lock) ? IER(168) : 0)
		  return;
	 }
  ready_to_join = 1;
  joinable_thread = pthread_self ();
  if (! --running_threads)
	 if (pthread_cond_signal (&last_thread) ? IER(169) : 0)     // wake up the exit routine
		{
		  pthread_mutex_unlock (&thread_reclamation_lock);
		  return;
		}
  if (pthread_cond_wait (&junction, &thread_reclamation_lock) ? IER(170) : 0)
	 {
		pthread_mutex_unlock (&thread_reclamation_lock);
		return;
	 }
  if (pthread_mutex_unlock (&thread_reclamation_lock))
	 IER(171);
}








static void*
manager (void_pointer)
	  void *void_pointer;

	  // Used as a start routine for pthread_create, this function runs
	  // the given function in the created thread and yields when
	  // finished.
{
  nthm_pipe source;
  thread_spec spec;
  int err;

  err = 0;
  if (!(spec = (thread_spec) void_pointer) ? 1 : (!(source = spec->pipe)) ? 1 : (source->valid != PIPE_MAGIC))
	 {
		globally_throw (THE_IER(172));
		pthread_exit (NULL);
	 }
  if (pthread_mutex_lock (&thread_reclamation_lock))
	 {
		globally_throw (THE_IER(173));
		pthread_exit (NULL);
	 }
  running_threads++;
  if (pthread_mutex_unlock (&thread_reclamation_lock))
	 {
		running_threads--;                  // maybe a race but already hosed at this point
		globally_throw (THE_IER(174));
		pthread_exit (NULL);
	 }
  source->result = (set_context (source, &err) ? (spec->operator) (spec->operand, &err) : NULL);
  unspecify (spec);
  yield (source, &err);
  clear_context (&err);
  prepare_for_exit (&err);
  globally_throw (err);
  pthread_exit (NULL);
}





// --------------- teardown routines to be run at exit -----------------------------------------------------






static void
globally_catch (err)
	  int *err;

	  // Safely detect the global error code.
{
  if (*err ? 1 : pthread_mutex_lock (&error_lock) ? IER(175) : 0)
	 return;
  *err = global_error;
  pthread_mutex_unlock (&error_lock);
}






static void
eradicate (err)
	  int *err;

	  // Free the root pipes.
{
  while (root_pipes)
	 {
		flush (root_pipes->pipe, err);
		if (! retired (root_pipes->pipe, err))
		  IER(176);
		if (! unilaterally_delisted (root_pipes, err))
		  IER(177);
	 }
}







static void
synchronize (err)
	  int *err;

	  // Join with the last thread still running.
{
  void *retval;

  if (pthread_mutex_lock (&thread_reclamation_lock) ? IER(178) : 0)
	 return;
  if ((! running_threads) ? 0 : pthread_cond_wait (&last_thread, &thread_reclamation_lock) ? IER(179) : 0)
	 {
		pthread_mutex_unlock (&thread_reclamation_lock);
		return;
	 }
  if (pthread_cond_signal (&junction) ? IER(180) : 0)
	 {
		pthread_mutex_unlock (&thread_reclamation_lock);
		return;
	 }
  if (pthread_mutex_unlock (&thread_reclamation_lock) ? IER(181) : 0)
	 return;
  if (! ready_to_join)                 // can happen only if no threads were ever created
	 return;
  if (pthread_join (joinable_thread, &retval))
	 IER(182);
}






static void
release_pthread_resources (err)
	  int *err;

	  // Release pthread related resources.
{
  if (pthread_mutexattr_destroy (&mutex_attribute))
	 IER(183);
  if (pthread_mutex_destroy (&error_lock))
	 IER(184);
  if (pthread_mutex_destroy (&thread_reclamation_lock))
	 IER(185);
  if (pthread_cond_destroy (&junction))
	 IER(186);
  if (pthread_cond_destroy (&last_thread))
	 IER(187);
  if (pthread_attr_destroy (&thread_attribute))
	 IER(188);
  if (pthread_key_delete (cursor))
	 IER(189);
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
  globally_catch (&err);
  release_pthread_resources (&err);
  if (err)
	 fprintf (stderr, "%s\n", nthm_strerror (err)); 
#ifdef MEMTEST
  if (pipes)
	 fprintf (stderr, "%u unreclaimed pipe%s\n", pipes, pipes == 1 ? "" : "s");
  if (pipe_lists)
	 fprintf (stderr, "%u unreclaimed pipe list%s\n", pipe_lists, pipe_lists == 1 ? "" : "s");
  if (thread_specs)
	 fprintf (stderr, "%u unreclaimed thread specification%s\n", thread_specs, thread_specs == 1 ? "" : "s");
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
		  {
			 pthread_mutex_unlock (&error_lock);
			 return error_buffer;
		  }
		return "nthm: undiagnosed POSIX error";
	 }
  switch (err)
	 {
	 case NTHM_UNMANT: return "nthm: inapplicable in an unmanaged thread";
	 case NTHM_NOTDRN: return "nthm: prohibited outside the drain thread";
	 case NTHM_NULPIP: return "nthm: null pipe";
	 case NTHM_INVPIP: return "nthm: corrupted or invalid pipe";
	 case NTHM_KILLED: return "nthm: interrupted by a kill notification";
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

  if ((!(drain = current_or_new_context (err))) ? 1 : (drain->valid != PIPE_MAGIC) ? IER(190) : 0)
	 return NULL;
  if (drain->yielded ? IER(191) : heritably_killed_or_yielded (drain, err) ? (*err = (*err ? *err : NTHM_KILLED)) : 0)
	 return NULL;
  if ((!(source = new_pipe (err))) ? 1 : ! (spec = thread_spec_of (source, operator, operand, err)))
	 return (retired (source, err) ? NULL : NULL);
  if (! tethered (source, drain, err))
	 {
		unspecify (spec);
		return (retired (source, err) ? NULL : NULL);
	 }
  if (! (e = pthread_create (&c, &thread_attribute, &manager, spec)))
	 return source;
  unspecify (spec);
  *err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(192));
  if (untethered (source, err) ? (! retired (source, err)) : 0)
	 source->valid = PIPE_MUGGLE(73);
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
  void *result;
  int e;

  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return NULL;
  if ((source->valid == PIPE_MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return NULL;
  if (!(drain = current_context ()))
	 return untethered_read (source, err);
  if (! tethered (source, drain, err))
	 return NULL;
  result = tethered_read (source, err);
  return result;
}






int
nthm_busy (source, err)
	  nthm_pipe source;
	  int *err;

	  // Return non-zero if reading the source would have blocked.
{
  int busy;

  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return 0;
  if ((source->valid == PIPE_MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return 0;
  if (pthread_mutex_lock (&(source->lock)) ? IER(193) : 0)
	 {
		source->valid = PIPE_MUGGLE(74);
		return 0;
	 }
  busy = ! source->yielded;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(194) : 0)
	 {
		source->valid = PIPE_MUGGLE(75);
		return 0;
	 }
  return busy;
}





int
nthm_blocked (err)
	  int *err;

	  // Return non-zero if a call to nthm_select would have blocked.
{
  nthm_pipe drain;
  int blocked;

  if ((!(drain = current_context ())) ? 1 : (drain->valid != PIPE_MAGIC) ? IER(195) : 0)
	 return 0;
  if (pthread_mutex_lock (&(drain->lock)) ? IER(196) : 0)
	 {
		drain->valid = PIPE_MUGGLE(76);
		return 0;
	 }
  blocked = (drain->finishers ? 0 : ! ! (drain->blockers));
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(197) : 0)
	 {
		drain->valid = PIPE_MUGGLE(77);
		return 0;
	 }
  return blocked;
}







nthm_pipe
nthm_select (err)
	  int *err;

	  // Return the next readable pipe tethered to the currently
	  // running thread if any, blocking if necessary until a readable
	  // pipe is available, but with blocking interrupted if the
	  // currently running thread is killed.
{
  nthm_pipe source, drain;
  int done, dead;

  if ((!(drain = current_context ())) ? 1 : (drain->valid == PIPE_MAGIC) ? 0 : IER(198))
	 return NULL;
  if (pthread_mutex_lock (&(drain->lock)) ? IER(199) : 0)
	 {
		drain->valid = PIPE_MUGGLE(78);
		return NULL;
	 }
  source = NULL;
  dead = done = 0;
  while (! done)
	 if ((dead = drain->killed))
		done = 1;
	 else if ((source = dequeued (drain->finishers, &(drain->finisher_queue), err)) ? 1 : !(drain->blockers))
		done = 1;
	 else if (pthread_cond_wait (&(drain->progress), &(drain->lock)) ? IER(200) : 0)
		{
		  pthread_mutex_unlock (&(drain->lock));
		  drain->valid = PIPE_MUGGLE(79);
		  return NULL;
		}
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(201) : 0)
	 {
		drain->valid = PIPE_MUGGLE(80);
		return NULL;
	 }
  *err = (*err ? *err : dead ? NTHM_KILLED : 0);
  return (dead ? NULL : source);
}







void
nthm_truncate (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tell a pipe to truncate its output.
{
  unsigned bumped;

  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid == PIPE_MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return;
  if ((!(source->reader)) ? 0 : (source->reader->pipe == current_context ()) ? 0 : (*err = (*err ? *err : NTHM_NOTDRN)))
	 return;
  if (pthread_mutex_lock (&(source->lock)) ? IER(202) : 0)
	 {
		source->valid = PIPE_MUGGLE(81);
		return;
	 }
  if (bumped = source->truncation + 1)  // don't increment if it was already the maximum value
	 source->truncation = bumped;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(203) : 0)
	 source->valid = PIPE_MUGGLE(82);
}








void
nthm_truncate_all (err)
	  int *err;

	  // Tell all threads tethered to the current thread to truncate
	  // their output.
{
  nthm_pipe drain;
  unsigned bumped;

  if ((!(drain = current_context ())) ? 1 : (drain->valid != PIPE_MAGIC) ? IER(204) : 0)
	 return;
  if (pthread_mutex_lock (&(drain->lock)) ? IER(205) : 0)
	 {
		drain->valid = PIPE_MUGGLE(83);
		return;
	 }
  if ((bumped = drain->truncation + 1))
	 drain->truncation = bumped;
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(206) : 0)
	 drain->valid = PIPE_MUGGLE(84);
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

  return ((source = current_context ()) ? heritably_truncated (source, err) : 0);
}









void
nthm_kill (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tell a pipe to pack it in and make it disappear.
{
  if ((!source) ? (*err = (*err ? *err : NTHM_NULPIP)) : 0)
	 return;
  if ((source->valid != PIPE_MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : 0)
	 return;
  if (! killable (source, err))
	 IER(207);
}







void
nthm_kill_all (err)
	  int *err;

	  // Tell all pipes tethered to the current one to pack it in.
{
  flush (current_context (), err);
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

  if (!(source = current_context ()) ? 1 : (source->valid != PIPE_MAGIC) ? IER(208) : 0)
	 return 0;
  if (pthread_mutex_lock (&(source->lock)) ? IER(209) : 0)
	 {
		source->valid = PIPE_MUGGLE(85);
		return 0;
	 }
  dead = source->killed;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(210) : 0)
	 {
		source->valid = PIPE_MUGGLE(86);
		return 0;
	 }
  return dead;
}








void
nthm_untether (source, err)
	  nthm_pipe source;
	  int *err;

	  // Emancipate a source from its drain so that it will not be
	  // reclaimed automatically when the drain exits and will remain
	  // readable by others.
{
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid != PIPE_MAGIC) ? (*err = (*err ? *err :  NTHM_INVPIP)) : 0)
	 return;
  if (! untethered (source, err))
	 IER(211);
}







void
nthm_tether (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tether an untethered source to the currently running thread so
	  // that it will be taken into account by nthm_select.
{
  nthm_pipe drain;

  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid != PIPE_MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : !(drain = current_or_new_context (err)))
	 return;
  if ((drain->valid != PIPE_MAGIC) ? IER(212) : 0)
	 return;
  if (drain->yielded ? IER(213) : heritably_killed_or_yielded (drain, err))
	 *err = (*err ? *err : NTHM_KILLED);
  else if (! tethered (source, drain, err))
	 IER(214);
}
