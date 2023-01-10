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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "errs.h"
#include "pipes.h"
#include "scopes.h"
#include "plumbing.h"
#include "nthmconfig.h"
#ifdef MEMTEST                // keep a count of allocated structures; not suitable for production code
#include <stdio.h>

// used to initialize static storage
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

// number of scopes in memory
static uintptr_t scopes = 0;

// mutually exclusion for the count
static pthread_mutex_t memtest_lock;

#endif // MEMTEST



// --------------- initialization and teardown -------------------------------------------------------------




#ifdef MEMTEST
void
lazy_initialization ()

	  // Initialize storage.
{
  int err;
  pthread_mutexattr_t a;

  err = 0;
  if (! _nthm_error_checking_mutex_type (&a, &err))
	 goto a;
  if (pthread_mutex_init (&memtest_lock, &a) ? (err = THE_IER(274)) : 0)
	 goto b;
  if (pthread_mutexattr_destroy (&a) ? (!(err = THE_IER(275))) : 1)
	 return;
  pthread_mutex_destroy (&memtest_lock);
  goto a;
 b: pthread_mutexattr_destroy (&a);
 a: fprintf (stderr, "%s\n", nthm_strerror (err));
  exit (EXIT_FAILURE);
}
#endif







void
_nthm_close_scopes ()

	  // Report memory leaks.
{
#ifdef MEMTEST
  pthread_once (&once_control, lazy_initialization);
  _nthm_globally_throw (pthread_mutex_destroy (&memtest_lock) ? THE_IER(276) : 0);
  if (scopes)
	 fprintf (stderr, "%lu unreclaimed scope%s\n", scopes, scopes == 1 ? "" : "s");
#endif
}






// --------------- scope stack operations ------------------------------------------------------------------








int
_nthm_scope_entered (p, err)
	  nthm_pipe p;
	  int *err;

	  // Enter a local scope by pushing the current descendants into an
	  // enclosing scope.
{
  scope_stack e;

#ifdef MEMTEST
  pthread_once (&once_control, lazy_initialization);
#endif
  if ((e = (scope_stack) malloc (sizeof (*e))) ? 0 : (*err = (*err ? *err : ENOMEM)))
	 return 0;
  if ((! p) ? IER(277) : (p->valid != MAGIC) ? IER(278) : 0)
	 goto a;
  if ((pthread_mutex_lock (&(p->lock)) ? IER(279) : 0) ? (p->valid = MUGGLE(98)) : 0)
	 goto a;
  memset (e, 0, sizeof (*e));
  e->enclosure = p->scope;
  p->scope = e;
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  scopes++;
  pthread_mutex_unlock (&memtest_lock);
#endif
  return ((pthread_mutex_unlock (&(p->lock)) ? IER(280) : 0) ? (!(p->valid = MUGGLE(99))) : 1);
 a: free (e);
  return 0;
}









int
_nthm_scope_exited (p, err)
	  nthm_pipe p;
	  int *err;

	  // Exit a scope by retrieving the former descendants from an
	  // enclosing scope.
{
  scope_stack e;

#ifdef MEMTEST
  pthread_once (&once_control, lazy_initialization);
#endif
  e = NULL;
  if ((! p) ? IER(281) : (p->valid != MAGIC) ? IER(282) : ((e = p->scope) ? 0 : IER(283)) ? (p->valid = MUGGLE(100)) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(p->lock)) ? IER(284) : 0) ? (p->valid = MUGGLE(101)) : 0)
	 return 0;
  if (e->blockers ? IER(285) : e->finishers ? IER(286) : e->finisher_queue ? IER(287) : 0)
	 goto a;
  p->scope = e->enclosure;
  free (e);
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  scopes--;
  pthread_mutex_unlock (&memtest_lock);
#endif
 a: return ((pthread_mutex_unlock (&(p->lock)) ? IER(288) : 0) ? (!(p->valid = MUGGLE(102))) : 1);
}







uintptr_t
_nthm_scope_level (p, err)
	  nthm_pipe p;
	  int *err;

	  // Return the current scope level of a pipe, defined as the
	  // number of times code running in its context has called
	  // scope_entered minus the number of times it has called
	  // scope_exited, and computed from the length of the list of
	  // enclosing scopes.
{
  uintptr_t d;
  scope_stack e;

  e = NULL;
  if ((! p) ? IER(289) : (p->valid != MAGIC) ? IER(290) : ((e = p->scope) ? 0 : IER(291)) ? (p->valid = MUGGLE(103)) : 0)
	 return 0;
  d = 0;
  for (e = e->enclosure; e; e = e->enclosure)
	 d++;
  return d;
}








int
_nthm_drained_by (s, d, err)
	  nthm_pipe s;
	  nthm_pipe d;
	  int *err;

	  // Return non-zero if d is the drain of s in the drain's current scope.
{
  if ((! s) ? IER(292) : (s->valid != MAGIC) ? IER(293) : s->reader ? (s->reader->pipe != d) : 1)
	 return 0;
  return (s->depth == _nthm_scope_level (d, err));
}









void
_nthm_vacate_scopes (s, err)
	  nthm_pipe s;
	  int *err;

	  // Exit all enclosed scopes.
{
  if ((! s) ? IER(294) : (s->valid != MAGIC) ? IER(295) : 0)
	 return;
  if ((s->scope ? 0 : IER(296)) ? (! (s->valid = MUGGLE(104))) : 0)
	 return;
  while (s->scope->enclosure ? (*err = (*err ? *err : NTHM_XSCOPE)) : 0)
	 if (!(_nthm_descendants_untethered (s, err) ? _nthm_scope_exited (s, err) : 0))
		break;
}


