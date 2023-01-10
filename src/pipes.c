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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "errs.h"
#include "pipes.h"
#include "nthmconfig.h"
#ifdef MEMTEST                  // keep counts of allocated structures; not suitable for production code
#include <stdio.h>

// number of pipes in memory
static uintptr_t pipes = 0;

// secures mutually exclusive access to the count
static pthread_mutex_t memtest_lock;

#endif // MEMTEST

// mutexs are created with these attributes
static pthread_mutexattr_t mutex_attribute;




// --------------- initialization and teardown -------------------------------------------------------------






int
_nthm_open_pipes (err)
	  int *err;

	  // Initialize static storage.
{
  if (! _nthm_error_checking_mutex_type (&mutex_attribute, err))
	 return 0;
#ifdef MEMTEST
  if (pthread_mutex_init (&memtest_lock, &mutex_attribute) ? IER(84) : 0)
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		return 0;
	 }
#endif
  return 1;
}






void
_nthm_close_pipes ()

	  // Report memory leaks if memory testing is enabled for
	  // development and diagnostics.
{
  _nthm_globally_throw (pthread_mutexattr_destroy (&mutex_attribute) ? THE_IER(85) : 0);
#ifdef MEMTEST
  _nthm_globally_throw (pthread_mutex_destroy (&memtest_lock) ? THE_IER(86) : 0);
  if (pipes)
	 fprintf (stderr, "%lu unreclaimed pipe%s\n", pipes, pipes == 1 ? "" : "s");
#endif
}






// --------------- memory management -----------------------------------------------------------------------








nthm_pipe
_nthm_new_pipe (err)
	  int *err;

	  // Return a newly allocated and initialized pipe.
{
  nthm_pipe p;
  int e;

  if ((p = (nthm_pipe) malloc (sizeof (*p))) ? 0 : (*err = (*err ? *err : ENOMEM)))
	 return NULL;
  memset (p, 0, sizeof (*p));
  if ((e = pthread_cond_init (&(p->termination), NULL)))
	 goto a;
  if ((e = pthread_cond_init (&(p->progress), NULL)))
	 goto b;
  if ((e = pthread_mutex_init (&(p->lock), &mutex_attribute)))
	 goto c;
  p->valid = MAGIC;
  if (! _nthm_scope_entered (p, err))
	 goto d;
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  pipes++;
  pthread_mutex_unlock (&memtest_lock);
#endif
  return p;
 d: pthread_mutex_destroy (&(p->lock));
 c: pthread_cond_destroy (&(p->progress));
 b: pthread_cond_destroy (&(p->termination));
 a: free (p);
  *err = (*err ? *err : (e == ENOMEM) ? e : (e == EAGAIN) ? e : THE_IER(87));
  return NULL;
}










int
_nthm_retired (p, err)
	  nthm_pipe p;
	  int *err;

	  // Tear down a pipe that has no drain, no enclosing scopes, and
	  // no blockers or finishers left.
{
  scope_stack e;

  if ((! p) ? IER(88) : (p->valid != MAGIC) ? IER(89) : ((e = p->scope) ? 0 : IER(90)) ? (p->valid = MUGGLE(23)) : 0)
	 return 0;
  if (e->enclosure ? IER(91) : ! _nthm_scope_exited (p, err))
	 return 0;
  if ((pthread_cond_destroy (&(p->termination)) ? IER(92) : 0) ? (p->valid = MUGGLE(24)) : 0)
	 return 0;
  if ((pthread_cond_destroy (&(p->progress)) ? IER(93) : 0) ? (p->valid = MUGGLE(25)) : 0)
	 return 0;
  if ((pthread_mutex_destroy (&(p->lock)) ? IER(94) : 0) ? (p->valid = MUGGLE(26)) : 0)
	 return 0;
  p->valid = MUGGLE(27);  // ensure detection of dangling references
  free (p);
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  pipes--;
  pthread_mutex_unlock (&memtest_lock);
#endif
  return 1;
}





// --------------- interrogation ---------------------------------------------------------------------------






int
_nthm_heritably_killed_or_yielded (source, err)
	  nthm_pipe source;
	  int *err;

	  // Detect whether source has yielded or been killed either
	  // explicitly or due to any of its drains yielding or being
	  // killed. It's necessary to lock each source, then lock its
	  // drain, and then unlock the source all the way up the tree in
	  // case any of them gets untethered.
{
  nthm_pipe drain;
  int h;

  if ((!source) ? IER(95) : (source->valid != MAGIC) ? IER(96) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(97) : 0) ? (source->valid = MUGGLE(28)) : 0)
	 return 0;
  if (!(h = (source->yielded ? 1 : source->killed)))
	 for (; source->reader; source = drain)
		{
		  if ((!(drain = source->reader->pipe)) ? IER(98) : (drain->valid != MAGIC) ? IER(99) : 0)
			 break;
		  if ((pthread_mutex_lock (&(drain->lock)) ? IER(100) : 0) ? (drain->valid = MUGGLE(29)) : 0)
			 break;
		  if ((h = (drain->yielded ? 1 : drain->killed)))
			 goto a;
		  if ((pthread_mutex_unlock (&(source->lock)) ? IER(101) : 0) ? (source->valid = MUGGLE(30)) : 0)
			 return ((pthread_mutex_unlock (&(drain->lock)) ? IER(102) : 0) ? (! (drain->valid = MUGGLE(31))) : 0);
		  continue;
		  a: if ((pthread_mutex_unlock (&(drain->lock)) ? IER(103) : 0) ? (drain->valid = MUGGLE(32)) : 1)
			 break;
		}
  return ((pthread_mutex_unlock (&(source->lock)) ? IER(104) : 0) ? (! (source->valid = MUGGLE(33))) : h);
}










unsigned
_nthm_heritably_truncated (source, err)
	  nthm_pipe source;
	  int *err;

	  // Detect whether source has been truncated either explicitly or
	  // due to any of its drains being truncated. The truncation
	  // status depends on the scope in which the pipe is opened,
	  // unlike heritable killed or yielded status, which are global.
{
  nthm_pipe drain;
  uintptr_t l;         // scope level counter
  scope_stack e;       // local scope
  unsigned h;          // return value

  if ((!source) ? IER(105) : (source->valid != MAGIC) ? IER(106) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(source->lock)) ? IER(107) : 0) ? (source->valid = MUGGLE(34)) : 0)
	 return 0;
  if (!(h = (unsigned) (source->yielded ? 1 : source->killed)))
	 for (; source->reader; source = drain)
		{
		  if ((!(drain = source->reader->pipe)) ? IER(108) : (drain->valid != MAGIC) ? IER(109) : 0)
			 break;
		  if ((pthread_mutex_lock (&(drain->lock)) ? IER(110) : 0) ? (drain->valid = MUGGLE(35)) : 0)
			 break;
		  if (((e = drain->scope) ? 0 : IER(111)) ? (drain->valid = MUGGLE(36)) : 0)
			 goto a;
		  if ((((l = _nthm_scope_level (drain, err)) < source->depth) ? IER(112) : 0) ? (source->valid = MUGGLE(37)) : 0)
			 goto a;
		  for (l = l - source->depth; l; l--)
			 if (((e = e->enclosure) ? 0 : IER(113)) ? (drain->valid = MUGGLE(38)) : 0)
				goto a;
		  if ((h = e->truncation))
			 goto a;
		  if ((pthread_mutex_unlock (&(source->lock)) ? IER(114) : 0) ? (source->valid = MUGGLE(39)) : 0)
			 return ((pthread_mutex_unlock (&(drain->lock)) ? IER(115) : 0) ? ((unsigned) !(drain->valid = MUGGLE(40))) : 0);
		  continue;
		a: if ((pthread_mutex_unlock (&(drain->lock)) ? IER(116) : 0) ? (drain->valid = MUGGLE(41)) : 1)
			 break;
		}
  return ((pthread_mutex_unlock (&(source->lock)) ? IER(117) : 0) ? ((unsigned) ! (source->valid = MUGGLE(42))) : h);
}








int
_nthm_retirable (p, err)
	  nthm_pipe p;
	  int *err;

	  // Safely test whether a pipe is ready to be retired. A
	  // placeholder is a pipe for unmanaged threads whose descendants
	  // pertain to managed threads. A placeholder can be retired
	  // whenever its last descendant is retired, whereas a pipe
	  // pertaining to a managed thread can be retired only after the
	  // thread yields and its result has been read.
{
  scope_stack e;
  int q;

  q = 0;
  if ((! p) ? IER(118) : (p->valid != MAGIC) ? IER(119) : 0)
	 return 0;
  if ((pthread_mutex_lock (&(p->lock)) ? IER(120) : 0) ? (p->valid = MUGGLE(43)) : 0)
	 return 0;
  if (! (p->zombie))
	 if (!(((e = p->scope) ? 0 : IER(121)) ? (p->valid = MUGGLE(44)) : (q = 0)))
		q = (e->enclosure ? 0 : e->blockers ? 0 : e->finishers ? 0 : p->placeholder ? 1 : p->yielded ? p->killed : 0);
  return (((pthread_mutex_unlock (&(p->lock)) ? IER(122) : 0) ? (p->valid = MUGGLE(45)) : 0) ? 0 : p->zombie ? 1 : q);
}
