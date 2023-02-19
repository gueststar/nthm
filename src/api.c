/*
  nthm -- non-preemptive thread hierarchy manager

  copyright (c) 2020-2023 Dennis Furey

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

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include "protocol.h"
#include "plumbing.h"
#include "pool.h"
#include "sync.h"
#include "context.h"
#include "pipes.h"
#include "scopes.h"
#include "errs.h"

// used to initialize static storage
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

// a bit bucket for error codes the user chooses to ignore
static int ignored_error;

// all threads are created with this attribute
static pthread_attr_t thread_attribute;

// unrecoverable pthread errors are communicated from other threads this way as a last resort
static int *deadlocked;

// non-zero if initialization is successful
static int initialized = 0;

// an error returned by the initialization routine
static int initial_error = 0;

// to be done when any user code calls a published API routine

#define API_ENTRY_POINT(x)                                                                  \
  if (err ? NULL : (err = &ignored_error))                                                   \
    ignored_error = 0;                                                                        \
  pthread_once (&once_control, initialization);                                                \
  if (initialized ? 0 : (*err = (*err ? *err : initial_error ? initial_error : THE_IER(23))))   \
    return x


// --------------- initialization and teardown -------------------------------------------------------------







static void
teardown ()

	  // Free all static storage. This function is installed by
	  // atexit () in the initialization routine.
{
  _nthm_close_pool ();
  _nthm_close_sync ();      // only one thread runs after this point unless there were unrecoverable pthread errors
  _nthm_close_context ();
  _nthm_close_pipes ();     // check for memory leaks
  _nthm_close_pipl ();
  _nthm_close_scopes ();
  _nthm_globally_throw (pthread_attr_destroy (&thread_attribute) ? THE_IER(24) : 0);
  _nthm_close_errs ();
}






static void
initialization ()

	  // Attempt initialization.
{
  deadlocked = _nthm_deadlocked ();
  if (! _nthm_open_errs (&initial_error))
	 return;
  if (! _nthm_open_pipes (&initial_error))
	 goto a;
  if (! _nthm_open_context (&initial_error))
	 goto b;
  if (! _nthm_open_sync (&initial_error))
	 goto c;
  if (! _nthm_open_pool (&initial_error))
	 goto d;
  if (! _nthm_stack_limited_thread_type (&thread_attribute, &initial_error))
	 goto e;
  if (atexit (teardown) ? (initial_error = (initial_error ? initial_error : THE_IER(25))) : 0)
	 goto f;
  initialized = 1;
  return;
 f: pthread_attr_destroy (&thread_attribute);
 e: _nthm_close_pool ();
 d: _nthm_close_sync ();
 c: _nthm_close_context ();
 b: _nthm_close_pipes ();
 a: _nthm_close_errs ();
}







// --------------- public-facing API -----------------------------------------------------------------------








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

#define READ_WRITE 0
#define NO_MUTATOR NULL

  API_ENTRY_POINT(NULL);
  if (*err)
	 return NULL;
  if (*deadlocked ? IER(26) : (!(drain = _nthm_current_or_new_context (err))) ? 1 : (drain->valid != MAGIC) ? IER(27) : 0)
	 return NULL;
  if (drain->yielded ? IER(28) : _nthm_heritably_killed_or_yielded (drain, err) ? (*err = (*err ? *err : NTHM_KILLED)) : 0)
	 return NULL;
  if (!(spec = _nthm_thread_spec_of (source = _nthm_new_pipe (err), operator, NO_MUTATOR, operand, READ_WRITE, err)))
	 return NULL;
  if (! _nthm_tethered (source, drain, err))
	 goto a;
  if ((e = pthread_create (&c, &thread_attribute, &_nthm_manager, spec)) ? 0 : _nthm_started (err))
	 return source;
  *err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(29));
  if (! _nthm_untethered (source, err))
	 IER(30);
 a: _nthm_unspecify (spec, err);
  return NULL;
}








int
nthm_send (mutator, operand, err)
	  nthm_slacker mutator;
	  void *operand;
	  int *err;

	  // Start a thread whose pipe is not user-readable, but that will
	  // be reclaimed automatically when it yields, and will
	  // synchronize with the main application thread at exit.
{
  thread_spec spec;
  nthm_pipe d;
  pthread_t c;
  int e;

#define WRITE_ONLY 1
#define NO_OPERATOR NULL

  API_ENTRY_POINT(0);
  if (*deadlocked ? IER(31) : 0)
	 return 0;
  if ((!(d = _nthm_current_context ())) ? 0 : (d->valid != MAGIC) ? IER(32) : d->yielded ? IER(33) : 0)
	 return 0;
  if ((! d) ? 0 : _nthm_heritably_killed_or_yielded (d, err) ? (*err = (*err ? *err : NTHM_KILLED)) : 0)
	 return 0;
  if (!(spec = _nthm_thread_spec_of (_nthm_new_pipe (err), NO_OPERATOR, mutator, operand, WRITE_ONLY, err)))
	 return 0;
  if ((e = pthread_create (&c, &thread_attribute, &_nthm_manager, spec)) ? 0 : _nthm_started (err))
	 return 1;
  *err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(34));
  _nthm_unspecify (spec, err);
  return 0;
}









void *
nthm_read (source, err)
	  nthm_pipe source;
	  int *err;

	  // Perform a blocking read on a pipe and retire it after reading,
	  // provided the pipe is not tethered to any other thread.
{
  nthm_pipe drain;

  API_ENTRY_POINT(NULL);
  if (*deadlocked ? IER(35) : source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return NULL;
  if ((source->valid == MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return NULL;
  if (! (drain = _nthm_current_context ()))
	 return _nthm_untethered_read (source, err);
  if (_nthm_tethered (source, drain, err))
	 return _nthm_tethered_read (source, err);
  return NULL;
}








int
nthm_busy (source, err)
	  nthm_pipe source;
	  int *err;

	  // Return non-zero if reading the source would have blocked.
{
  int busy;

  API_ENTRY_POINT(0);
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return 0;
  if ((source->valid == MAGIC) ? 0 : (*err = (*err ? *err : NTHM_INVPIP)))
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(36) : 0) ? (source->valid = MUGGLE(1)) : 0)
	 return 0;
  busy = ! source->yielded;
  return ((pthread_mutex_unlock (&(source->lock)) ? IER(37) : 0) ? (!(source->valid = MUGGLE(2))) : busy);
}









int
nthm_blocked (err)
	  int *err;

	  // Return non-zero if a call to nthm_select would have blocked.
{
  nthm_pipe drain;
  scope_stack e;
  int blocked;

  API_ENTRY_POINT(0);
  if ((!(drain = _nthm_current_context ())) ? 1 : (drain->valid != MAGIC) ? IER(38) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(drain->lock)) ? IER(39) : 0) ? (drain->valid = MUGGLE(3)) : (blocked = 0))
	 return 0;
  if (!(((e = drain->scope) ? 0 : IER(40)) ? (drain->valid = MUGGLE(4)) : 0))
	 blocked = (e->finishers ? 0 : ! ! (e->blockers));
  return (((pthread_mutex_unlock (&(drain->lock)) ? IER(41) : 0) ? (drain->valid = MUGGLE(5)) : 0) ? 0 : blocked);
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
  scope_stack e;
  int k;

  API_ENTRY_POINT(NULL);
  s = NULL;
  if (*deadlocked ? IER(42) : (!(d = _nthm_current_context ())) ? 1 : (d->valid == MAGIC) ? 0 : IER(43))
	 goto a;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(44) : 0) ? (d->valid = MUGGLE(6)) : (k = 0))
	 goto a;
  if (((e = d->scope) ? 0 : IER(45)) ? (d->valid = MUGGLE(7)) : 0)
	 goto b;
  for (; (k = d->killed) ? 0 : (s = _nthm_dequeued (&(e->finishers), &(e->finisher_queue), err)) ? 0 : ! ! (e->blockers);)
	 if ((pthread_cond_wait (&(d->progress), &(d->lock)) ? IER(46) : 0) ? (d->valid = MUGGLE(8)) : 0)
		break;
 b: if ((pthread_mutex_unlock (&(d->lock)) ? IER(47) : 0) ? (d->valid = MUGGLE(9)) : 0)
	 goto a;
  *err = (*err ? *err : k ? NTHM_KILLED : 0);
 a: return s;
}








void
nthm_truncate (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tell a pipe to truncate its output.
{
  unsigned bumped;
  nthm_pipe drain;

  API_ENTRY_POINT();
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : ! (drain = _nthm_current_context ()))
	 return;
  if (_nthm_drained_by (source, drain, err) ? 0 : (*err = (*err ? *err : NTHM_NOTDRN)))
	 return;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(48) : 0) ? (source->valid = MUGGLE(10)) : 0)
	 return;
  if (!((source->scope ? 0 : IER(49)) ? (source->valid = MUGGLE(11)) : 0))
	 if ((bumped = source->scope->truncation + 1))
		source->scope->truncation = bumped;
  if (pthread_mutex_unlock (&(source->lock)) ? IER(50) : 0)
	 source->valid = MUGGLE(12);
}








void
nthm_truncate_all (err)
	  int *err;

	  // Tell all threads tethered to the current thread to truncate
	  // their output.
{
  nthm_pipe drain;
  unsigned bumped;

  API_ENTRY_POINT();
  if ((!(drain = _nthm_current_context ())) ? 1 : (drain->valid != MAGIC) ? IER(51) : 0)
	 return;
  if ((pthread_mutex_lock (&(drain->lock)) ? IER(52) : 0) ? (drain->valid = MUGGLE(13)) : 0)
	 return;
  if (!((drain->scope ? 0 : IER(53)) ? (drain->valid = MUGGLE(14)) : 0))
	 if ((bumped = drain->scope->truncation + 1))
		drain->scope->truncation = bumped;
  if (pthread_mutex_unlock (&(drain->lock)) ? IER(54) : 0)
	 drain->valid = MUGGLE(15);
}









unsigned
nthm_truncated (err)
	  int *err;

	  // An introspective predicate polled by user code indicates that
	  // it is free to return a partial result to the drain indicative
	  // of its current progress.
{
  nthm_pipe source;
  unsigned t;

  API_ENTRY_POINT(0);
  if ((source = _nthm_current_context ()) ? 0 : (*err = (*err ? *err : NTHM_UNMANT)))
	 return 0;
  if ((source->valid != MAGIC) ? IER(55) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(56) : 0) ? (source->valid = MUGGLE(16)) : 0)
	 return 0;
  if ((source->scope ? 0 : IER(57)) ? (source->valid = MUGGLE(17)) : 0)
	 return ((pthread_mutex_unlock (&(source->lock)) ? IER(58) : 0) ? ((unsigned) ! (source->valid = MUGGLE(18))) : 0);
  t = source->scope->truncation;
  if ((pthread_mutex_unlock (&(source->lock)) ? IER(59) : 0) ? (source->valid = MUGGLE(19)) : 0)
	 return 0;
  return (t ? t : _nthm_heritably_truncated (source, err));
}








void
nthm_kill (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tell a pipe to pack it in and make it disappear.
{
  API_ENTRY_POINT();
  if ((! source) ? (*err = (*err ? *err : NTHM_NULPIP)) : 0)
	 return;
  if ((source->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : ! _nthm_killable (source, err))
	 IER(60);
}








void
nthm_kill_all (err)
	  int *err;

	  // Tell all pipes tethered to the current one to pack it in. The
	  // placeholder field has to be cleared temporarily to stop the
	  // pipe from being reclaimed prematurely.
{
  nthm_pipe drain;

  API_ENTRY_POINT();
  if (!(drain = _nthm_current_context ()))
	 return;
  if (drain->placeholder ? (drain->placeholder = 0) : _nthm_descendants_killed (drain, err) ? 1 : IER(61))
	 return;
  if (_nthm_descendants_killed (drain, err) ? (drain->placeholder = 1) : ! IER(62))
	 _nthm_unpool (drain, err);
}









int
nthm_killed (err)
	  int *err;

	  // An introspective predicate polled by user code indicates that
	  // any result it returns ultimately will be ignored.
{
  nthm_pipe source;
  int dead;

  API_ENTRY_POINT(0);
  if ((source = _nthm_current_context ()) ? 0 : (*err = (*err ? *err : NTHM_UNMANT)))
	 return 0;
  if ((source->valid != MAGIC) ? IER(63) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(64) : 0) ? (source->valid = MUGGLE(20)) : 0)
	 return 0;
  dead = source->killed;
  return (((pthread_mutex_unlock (&(source->lock)) ? IER(65) : 0) ? (source->valid = MUGGLE(21)) : 0) ? 1 : dead);
}









void
nthm_untether (source, err)
	  nthm_pipe source;
	  int *err;

	  // Emancipate a source from its drain so that it will not be
	  // reclaimed automatically when the drain exits and will remain
	  // readable by others.
{
  API_ENTRY_POINT();
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : 0)
	 return;
  if (! _nthm_untethered (source, err))
	 IER(66);
}








void
nthm_tether (source, err)
	  nthm_pipe source;
	  int *err;

	  // Tether an untethered source to the currently running thread so
	  // that it will be taken into account by nthm_select.
{
  nthm_pipe drain;

  API_ENTRY_POINT();
  if (source ? 0 : (*err = (*err ? *err : NTHM_NULPIP)))
	 return;
  if ((source->valid != MAGIC) ? (*err = (*err ? *err : NTHM_INVPIP)) : !(drain = _nthm_current_or_new_context (err)))
	 return;
  if ((drain->valid != MAGIC) ? IER(67) : drain->yielded ? IER(68) : 0)
	 return;
  if (_nthm_heritably_killed_or_yielded (drain, err))
	 *err = (*err ? *err : NTHM_KILLED);
  else if (! _nthm_tethered (source, drain, err))
	 IER(69);
}









int
nthm_enter_scope (err)
	  int *err;

	  // Restrict attention to pipes opened subsequently.
{
  nthm_pipe p;

  API_ENTRY_POINT(0);
  if (*deadlocked ? IER(70) : (!(p = _nthm_current_or_new_context (err))) ? 1 : (p->valid != MAGIC) ? IER(71) : 0)
	 return 0;
  if (p->yielded ? IER(72) : _nthm_heritably_killed_or_yielded (p, err) ? (*err = (*err ? *err : NTHM_KILLED)) : 0)
	 return 0;
  return _nthm_scope_entered (p, err);
}









void
nthm_exit_scope (err)
	  int *err;

	  // Resume previous attention span. Any pipes opened since the
	  // scope was created continue untethered.
{
  scope_stack e;
  nthm_pipe p;

  API_ENTRY_POINT();
  if ((p = _nthm_current_context ()) ? 0 : (*err = (*err ? *err : NTHM_UNDFLO)))
	 return;
  if (((e = p->scope) ? 0 : IER(73)) ? (p->valid = MUGGLE(22)) : 0)
	 return;
  if ((!(e->enclosure)) ? (*err = (*err ? *err : NTHM_UNDFLO)) : ! _nthm_descendants_untethered (p, err))
	 return;
  if (_nthm_scope_exited (p, err))
	 _nthm_unpool (p, err);
}








void
nthm_sync (err)
	  int *err;

	  // Wait for all threads created by nthm to exit.
{
  API_ENTRY_POINT();
  _nthm_synchronize (err);
}
