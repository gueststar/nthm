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


#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "protocol.h"
#include "plumbing.h"
#include "pipes.h"
#include "sync.h"
#include "context.h"
#include "errs.h"

// unrecoverable pthread error
static int deadlocked = 0;



// --------------- drain-side protocol ---------------------------------------------------------------------




void *
_nthm_untethered_read (s, err)
	  nthm_pipe s;
	  int *err;

	  // Read from a pipe with no designated drain and therefore no
	  // opportunity for the read to be interrupted by the drain being
	  // killed. Wait on the pipe's termination signal if necessary.
{
  void *result;

  result = NULL;
  if ((! s) ? IER(237) : (s->valid != MAGIC) ? IER(238) : 0)
	 return NULL;
  if ((pthread_mutex_lock (&(s->lock)) ? IER(239) : 0) ? (s->valid = MUGGLE(81)) : 0)
	 return NULL;
  if (s->reader ? (*err = (*err ? *err : NTHM_NOTDRN)) : 0)
	 goto a;
  if (s->yielded ? 0 : (pthread_cond_wait (&(s->termination), &(s->lock)) ? IER(240) : 0) ? (s->valid = MUGGLE(82)) : 0)
	 goto a;
  result = s->result;
  if (*err ? 0 : s->status ? (*err = s->status) : 0)
	 s->status = 0;
 a: if (s->yielded ? 0 : (s->yielded = 1))
	 IER(241);
  if (pthread_mutex_unlock (&(s->lock)) ? IER(242) : 0)
	 s->valid = MUGGLE(83);
  return (_nthm_killable (s, err) ? result : NULL);
}












void *
_nthm_tethered_read (s, err)
	  nthm_pipe s;
	  int *err;

	  // Read from a source s whose drain d is running in the current
	  // context, unless the drain is killed. The source knows it has a
	  // drain and so will signal the drain's progress rather than its
	  // own termination signal when it terminates. If the drain has
	  // other sources than the one it's trying to read, one of the
	  // others might signal it first and it may have to continue
	  // waiting, hence the loop.
{
  nthm_pipe d;
  void *result;
  int done;

  result = NULL;
  if ((! s) ? IER(243) : (s->valid != MAGIC) ? IER(244) : s->reader ? 0 : IER(245))
	 return NULL;
  if (_nthm_drained_by (s, d = _nthm_current_context (), err) ? 0 : (*err = (*err ? *err : NTHM_NOTDRN)))
	 return NULL;
  if (((! d) ? IER(246) : (d->valid != MAGIC) ? IER(247) : 0) ? (s->valid = MUGGLE(84)) : 0)
	 return NULL;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(248) : 0) ? (d->valid = MUGGLE(85)) : 0)
	 return NULL;
  while (! (done = (s->yielded ? 1 : d->killed)))
	 if (pthread_cond_wait (&(d->progress), &(d->lock)) ? IER(249) : 0)
		goto a;
  if (*err ? 0 : s->status ? (*err = s->status) : 0)
	 s->status = 0;
  result = (s->yielded ? s->result : NULL);
 a: if (pthread_mutex_unlock (&(d->lock)) ? IER(250) : 0)
	 d->valid = MUGGLE(86);
  return ((done ? _nthm_killable (s, err) : 0) ? result : NULL);
}








// --------------- source-side protocol --------------------------------------------------------------------








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
  if ((! s) ? IER(251) : (s->valid != MAGIC) ? IER(252) : 0)
	 return;
  s->yielded = 1;
  if (pthread_cond_signal (&(s->termination)) ? IER(253) : 0)
	 s->valid = MUGGLE(87);
  else if (s->killed ? 0 : s->status ? 0 : (s->status = *err))
	 *err = 0;
  if (pthread_mutex_unlock (&(s->lock)) ? IER(254) : 0)
	 s->valid = MUGGLE(88);
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
  scope_stack e;
  pipe_list b;    // the entry in the drain's blocker list corresponding to the source
  nthm_pipe d;    // drain
  uintptr_t l;    // scope level

  if ((! s) ? IER(255) : (s->valid != MAGIC) ? IER(256) : s->killed ? IER(257) : 0)
	 return;
  if ((!(s->reader)) ? IER(258) : (!(d = s->reader->pipe)) ? IER(259) : (d->valid != MAGIC) ? IER(260) : 0)
	 goto a;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(261) : 0) ? (d->valid = MUGGLE(89)) : 0)
	 goto a;
  if (((e = d->scope) ? 0 : IER(262)) ? (d->valid = MUGGLE(90)) : 0)
	 goto b;
  if ((((l = _nthm_scope_level (d, err)) < s->depth) ? IER(263) : 0) ? (s->valid = MUGGLE(91)) : 0)
	 goto b;
  for (l = l - s->depth; l; l--)
	 if (((e = e->enclosure) ? 0 : IER(264)) ? (d->valid = MUGGLE(92)) : 0)
		goto b;
  if (! _nthm_severed (b = s->reader->complement, err))                                 // remove s from d's blockers
	 goto b;
  s->yielded = _nthm_enqueued (b, &(e->finishers), &(e->finisher_queue), err);          // install s in d's finishers
  if ((s->yielded ? 0 : _nthm_freed (b, err) ? 1 : IER(265)) ? (s->yielded = 1) : 0)
	 s->valid = MUGGLE(93);
  if (pthread_cond_signal (&(d->progress)) ? IER(266) : 0)
	 d->valid = MUGGLE(94);
  if (s->status ? 0 : (s->status = *err))
	 *err = 0;
 b: if (pthread_mutex_unlock (&(d->lock)) ? IER(267) : 0)
	 d->valid = MUGGLE(95);
 a: if (pthread_mutex_unlock (&(s->lock)) ? IER(268) : 0)
	 s->valid = MUGGLE(96);
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
  if ((! _nthm_descendants_killed (source, err)) ? 1 : (! source) ? IER(269) : (source->valid != MAGIC) ? IER(270) : 0)
	 return;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(271) : 0) ? (source->valid = MUGGLE(97)) : 0)
	 return;
  if (source->killed ? 1 : !(source->reader))
	 untethered_yield (source, err);
  else
	 tethered_yield (source, err);
}








void *
_nthm_manager (void_pointer)
	  void *void_pointer;

	  // Used as a start routine for pthread_create, this function runs
	  // the given function in the created thread and yields when
	  // finished.
{
  thread_spec t;
  nthm_pipe s;
  int err;

  err = 0;
  if ((t = (thread_spec) void_pointer) ? 0 : (deadlocked = err = THE_IER(272)))
	 goto a;
  if (((!(s = t->pipe)) ? 1 : (s->valid != MAGIC) ? 1 : ! _nthm_set_context (s, &err)) ? (deadlocked = err = THE_IER(273)) : 0)
	 goto b;
  if (_nthm_registered (&err) ? 0 : (deadlocked = 1))
	 goto c;
  t->pipe = NULL;
  if (t->write_only)
	 (t->mutator) (t->operand);
  else
	 s->result = (t->operator) (t->operand, &(s->status));
  _nthm_vacate_scopes (s, &err);
  if (!(t->write_only))
	 yield (s, &err);
  else if (! _nthm_acknowledged (s, &err))
	 deadlocked = 1;
 c: _nthm_clear_context (&err);
 b: _nthm_unspecify (t, &err);
  _nthm_relay_race (&err);
 a: _nthm_globally_throw (err);
  pthread_exit (NULL);
}







int *
_nthm_deadlocked ()

	  // Return the address of a location used as a last resort for
	  // reporting unrecoverable pthread errors.
{
  return &deadlocked;
}
