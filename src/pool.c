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

#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "errs.h"
#include "context.h"
#include "pool.h"
#include "pipes.h"
#include "protocol.h"
#include "plumbing.h"

// list of untethered and top pipes to be freed on exit
static pipe_list root_pipes = NULL;

// enforces mutually exclusive access to the root pipe list
static pthread_mutex_t root_lock;




// --------------- initialization and teardown -------------------------------------------------------------




int
_nthm_open_pool (err)
	  int *err;

    // Initialize static storage.
{
  pthread_mutexattr_t a;

  if (! _nthm_error_checking_mutex_type (&a, err))
	 return 0;
  if (pthread_mutex_init (&root_lock, &a) ? IER(208) : 0)
	 {
		pthread_mutexattr_destroy (&a);
		return 0;
	 }
  if (!(pthread_mutexattr_destroy (&a) ? IER(209) : 0))
	 return 1;
  pthread_mutex_destroy (&root_lock);
  return 0;
}








static void
eradicate (err)
	  int *err;

	  // Reclaim the root pipes.
{
  nthm_pipe p;
  pipe_list q;
  void *leak;
  int k;

  p = NULL;
  while (! *err)
	 {
		if (pthread_mutex_lock (&root_lock) ? IER(210) : 0)
		  return;
		if (((q = root_pipes)) ? (q->previous_pipe = &q) : NULL)
		  root_pipes = NULL;
		if (pthread_mutex_unlock (&root_lock) ? IER(211) : ! q)
		  return;
		while (*err ? NULL : (p = (q ? _nthm_popped (&q, err) : NULL)))
		  {
			 _nthm_vacate_scopes (p, err);
			 if ((p->valid != MAGIC) ? IER(212) : p->reader ? IER(213) : p->pool ? (! ! (p->pool = NULL)) : IER(214))
				return;
			 if (_nthm_retirable (p, err) ? (_nthm_retired (p, err) ? 1 : IER(215)) : 0)
				continue;
			 if ((pthread_mutex_lock (&(p->lock)) ? IER(216) : 0) ? (p->valid = MUGGLE(75)) : 0)
				return;
			 k = (p->placeholder ? (p->killed ? 1 : (p->killed)++) : 0);
			 if ((pthread_mutex_unlock (&(p->lock)) ? IER(217) : 0) ? (p->valid = MUGGLE(76)) : 0)
				return;
			 if (k ? 0 : _nthm_pooled (p, err) ? 1 : IER(218))
				continue;
			 if ((leak = _nthm_untethered_read (p, err)))
				IER(219);
		  }
	 }
}






void
_nthm_close_pool ()

	  // Safely free the root pipes and other static storage. This
	  // operation executes during the exit phase but multiple threads
	  // might still be running.
{
  int err;

  err = 0;
  eradicate (&err);
  _nthm_globally_throw (err);
  _nthm_globally_throw (pthread_mutex_destroy (&root_lock) ? THE_IER(220) : 0);
}






// --------------- root pipe list management ---------------------------------------------------------------






int
_nthm_pooled (d, err)
	  nthm_pipe d;
	  int *err;

	  // Insert a pipe d into the root pool if it's not retirable but
	  // retire it otherwise.
 {
 if (! _nthm_retirable (d, err))
	return _nthm_placed (d, err);
 _nthm_displace (d, err);
 return _nthm_retired (d, err);
}







int
_nthm_placed (d, err)
	  nthm_pipe d;
	  int *err;

	  // Insert a pipe d into the root pool unconditionally.
{
  int done;

  done = 0;
  if ((! d) ? IER(221) : (d->valid != MAGIC) ? IER(222) : 0)
	 return 0;
  if (pthread_mutex_lock (&root_lock) ? IER(223) : (done = 0))
	 return 0;
  if ((pthread_mutex_lock (&(d->lock)) ? IER(224) : 0) ? (d->valid = MUGGLE(77)) : 0)
	 goto a;
  if (d->pool ? (done = 1) : ! (d->pool = _nthm_pipe_list_of (d, err)))
	 goto b;
  if ((done = _nthm_pushed (d->pool, &root_pipes, err)))
	 goto b;
  if (_nthm_freed (d->pool, err) ? (! ! (d->pool = NULL)) : 1)
	 IER(225);
 b: if (pthread_mutex_unlock (&(d->lock)) ? IER(226) : 0)
	 d->valid = MUGGLE(78);
 a: return ((pthread_mutex_unlock (&root_lock) ? IER(227) : 0) ? 0 : done);
}









void
_nthm_displace (p, err)
	  nthm_pipe p;
	  int * err;

	  // Take a pipe out of the root pool unconditionally.
{
  if ((! p) ? IER(228) : (p->valid != MAGIC) ? IER(229) : 0)
	 return;
  if (pthread_mutex_lock (&root_lock) ? IER(230) : 0)
	 return;
  if ((pthread_mutex_lock (&(p->lock)) ? IER(231) : 0) ? (p->valid = MUGGLE(79)) : 0)
	 goto a;
  if (p->pool ? _nthm_unilaterally_delisted (&(p->pool), err) : 0)
	 p->pool = NULL;
  if (pthread_mutex_unlock (&(p->lock)) ? IER(232) : 0)
	 p->valid = MUGGLE(80);
  a: if (pthread_mutex_unlock (&root_lock))
	 IER(233);
}








void
_nthm_unpool (p, err)
	  nthm_pipe p;
	  int *err;

	  // If a pipe is retirable, retire it and take it out of the root
	  // pool.
{
  int c;   // non-zero if the pipe is a placeholder for the current context

  if ((! p) ? IER(234) : (p->valid != MAGIC) ? IER(235) : ! _nthm_retirable (p, err))
	 return;
  c = (p->placeholder ? (_nthm_current_context () == p) : 0);
  _nthm_displace (p, err);
  if (_nthm_retired (p, err) ? c : ! IER(236))
	 _nthm_clear_context (err);
}
