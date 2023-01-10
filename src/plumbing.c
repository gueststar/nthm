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

#include <pthread.h>
#include <stdint.h>
#include "plumbing.h"
#include "pipes.h"
#include "scopes.h"
#include "context.h"
#include "pool.h"
#include "errs.h"




// --------------- tethering and untethering pipes ---------------------------------------------------------






int
_nthm_tethered (s, d, err)
	  nthm_pipe s;
	  nthm_pipe d;
	  int *err;

	  // Tether a source s to a drain d if it isn't already. If the
	  // source has terminated, then it has to be enqueued in the
	  // drain's finishers, but if it's still running, then it has to
	  // be pushed into the blockers. Locks on both are needed, and the
	  // source is locked first. If the source was previously in the
	  // root pool due to having been untethered, it has to be
	  // taken out.
{
  int t;            // set to non-zero and returned if tethering is successful
  scope_stack e;
  pipe_list r, w;   // the source's reader and the drain's finisher or blocker

  if ((! d) ? IER(159) : (d->valid != MAGIC) ? IER(160) : (! s) ? IER(161) : (s->valid != MAGIC) ? IER(162) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(s->lock)) ? IER(163) : 0) ? (s->valid = MUGGLE(46)) : 0)
	 return 0;
  if ((!(s->reader)) ? (t = 0) : _nthm_drained_by (s, d, err) ? (t = 1) : ! (t = ! (*err = (*err ? *err : NTHM_NOTDRN))))
	 goto a;
  if (s->killed ? IER(164) : (pthread_mutex_lock (&(d->lock)) ? IER(165) : 0) ? (d->valid = MUGGLE(47)) : 0)
	 goto a;
  if (((e = d->scope) ? 0 : IER(166)) ? (d->valid = MUGGLE(48)) : ! _nthm_new_complementary_pipe_lists (&r, d, &w, s, err))
	 goto b;
  if (_nthm_pushed (r, &(s->reader), err) ? 0 : _nthm_bilaterally_freed (r, w, err) ? 1 : IER(167))
	 goto b;
  t = (s->yielded ? _nthm_enqueued (w, &(e->finishers), &(e->finisher_queue), err) : _nthm_pushed (w, &(e->blockers), err));
  if (t)
	 s->depth = _nthm_scope_level (d, err);
  else if (!(_nthm_freed (w, err) ? _nthm_unilaterally_delisted (&(s->reader), err) : NULL))
	 s->valid = MUGGLE(49);
 b: if (pthread_mutex_unlock (&(d->lock)) ? IER(168) : 0)
	 d->valid = MUGGLE(50);
 a: if (pthread_mutex_unlock (&(s->lock)) ? IER(169) : 0)
	 s->valid = MUGGLE(51);
  _nthm_displace (s, err);
  return t;
}







int
_nthm_untethered (s, err)
	  nthm_pipe s;
	  int *err;

	  // Separate a possibly running source s from a running drain
	  // d. Locks on both are needed. The source is locked first. If
	  // there are no sources left on the drain after this operation,
	  // and the drain is a placeholder in the root pool, it can
	  // be taken out of the pool.
{
  scope_stack e;
  nthm_pipe d;
  int done;

  if ((! s) ? IER(170) : (s->valid == MAGIC) ? 0 : IER(171))
	 return 0;
  if ((done = ! (s->reader)))
	 return _nthm_pooled (s, err);
  if (_nthm_drained_by (s, d = _nthm_current_context (), err) ? 0 : (*err = (*err ? *err : NTHM_NOTDRN)))
	 return 0;
  if ((! d) ? IER(172) : (d->valid != MAGIC) ? IER(173) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(s->lock)) ? IER(174) : 0) ? (s->valid = MUGGLE(52)) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(175) : 0) ? (d->valid = MUGGLE(53)) : 0)
	 goto a;
  if (((e = d->scope) ? 0 : IER(176)) ? (d->valid = MUGGLE(54)) : 0)
	 goto b;
  if (! (done = (s == _nthm_bilaterally_dequeued (s->reader, &(e->finishers), &(e->finisher_queue), err))))
	 s->valid = d->valid = MUGGLE(55);
 b: if (pthread_mutex_unlock (&(d->lock)) ? IER(177) : 0)
	 d->valid = MUGGLE(56);
 a: if (pthread_mutex_unlock (&(s->lock)) ? IER(178) : 0)
	 s->valid = MUGGLE(57);
  if (done)
	 _nthm_unpool (d, err);
  return (done ? _nthm_pooled (s, err) : 0);
}









int
_nthm_descendants_untethered (p, err)
	  nthm_pipe p;
	  int *err;

	  // Untether all blockers and finishers under a pipe p.
{
  scope_stack e;
  pipe_list c;

  if ((! p) ? IER(179) : (p->valid != MAGIC) ? IER(180) : 0)
	 return 0;
  do
	 {
		if ((pthread_mutex_lock (&(p->lock)) ? IER(181) : 0) ? (p->valid = MUGGLE(58)) : 0)
		  return 0;
		if (((e = p->scope) ? 0 : IER(182)) ? (p->valid = MUGGLE(59)) : 0)
		  return ((pthread_mutex_unlock (&(p->lock)) ? IER(183) : 0) ? (!(p->valid = MUGGLE(60))) : 0);
		c = (e->finishers ? e->finishers : e->blockers);
		if ((pthread_mutex_unlock (&(p->lock)) ? IER(184) : 0) ? (p->valid = MUGGLE(61)) : 0)
		  return 0;
	 }
  while (c ? _nthm_untethered (c->pipe, err) : 0);
  return 1;
}





// --------------- killing pipes ---------------------------------------------------------------------------








int
_nthm_killable (s, err)
	  nthm_pipe s;
	  int *err;

	  // Kill and untether a pipe, which may entail pooling or retiring it.
{
  if ((!s) ? IER(185) : (s->valid != MAGIC) ? IER(186) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(s->lock)) ? IER(187) : 0) ? (s->valid = MUGGLE(62)) : 0)
	 return 0;
  s->killed = 1;
  if (s->yielded ? 0 : pthread_cond_signal (&(s->progress)) ? IER(188) : 0)
	 s->valid = MUGGLE(63);
  return ((pthread_mutex_unlock (&(s->lock)) ? IER(189) : 0) ? (!(s->valid = MUGGLE(64))) : _nthm_untethered (s, err));
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
  nthm_pipe s;
  scope_stack e;
  int done;

  if ((! d) ? IER(190) : (d->valid != MAGIC) ? IER(191) : d->placeholder ? IER(192) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(193) : 0) ? (d->valid = MUGGLE(65)) : 0)
	 return 0;
  if (d->yielded ? IER(194) : ((e = d->scope) ? 0 : IER(195)) ? (d->valid = MUGGLE(66)) : 0)
	 return ((pthread_mutex_unlock (&(d->lock)) ? IER(196) : 0) ? (!(d->valid = MUGGLE(67))) : 0);
  while (! (done = ! (e->blockers)))
	 {
		if ((s = e->blockers->pipe) ? 0 : (d->valid = MUGGLE(68)))
		  break;
		if ((pthread_mutex_unlock (&(d->lock)) ? IER(197) : 0) ? (d->valid = MUGGLE(69)) : ! _nthm_killable (s, err))
		  return 0;
		if ((pthread_mutex_lock (&(d->lock)) ? IER(198) : 0) ? (d->valid = MUGGLE(70)) : 0)
		  return 0;
	 }
  return (((pthread_mutex_unlock (&(d->lock)) ? IER(199) : 0) ? (d->valid = MUGGLE(71)) : 0) ? 0 : done);
}








int
_nthm_descendants_killed (d, err)
	  nthm_pipe d;
	  int *err;

	  // Kill both the blockers and the finishers to a drain. The pipes
	  // in the finishers queue are assumed to have had their
	  // descendants killed already. A lock is needed here in case one
	  // of the blockers finishes concurrently.
{
  nthm_pipe finisher;
  scope_stack e;
  int done;

  if ((! d) ? IER(200) : (d->valid != MAGIC) ? IER(201) : ! blockers_killed (d, err))
	 return 0;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(202) : 0) ? (d->valid = MUGGLE(72)) : 0)
	 return 0;
  if (!(((e = d->scope) ? 0 : IER(203)) ? (d->valid = MUGGLE(73)) : (done = 0)))
	 while (! (done = ! (finisher = _nthm_dequeued (&(e->finishers), &(e->finisher_queue), err))))
		if (finisher->pool ? IER(204) : ! _nthm_retired (finisher, err))
		  break;
  return ((pthread_mutex_unlock (&(d->lock)) ? IER(205) : 0) ? (!(d->valid = MUGGLE(74))) : done);
}









int
_nthm_acknowledged (s, err)
	  nthm_pipe s;
	  int *err;

	  // Retire an untethered unpooled pipe taking note of its error
	  // status.
{
  if ((! s) ? IER(206) : (s->valid != MAGIC) ? IER(207) : 0)
	 return 0;
  *err = (*err ? *err : s->status);
  return (_nthm_descendants_killed (s, err) ? _nthm_retired (s, err) : 0);
}
