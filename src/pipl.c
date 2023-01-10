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
#include <string.h>
#include <errno.h>
#include "errs.h"
#include "pipl.h"
#include "nthmconfig.h"
#ifdef MEMTEST
#include <stdio.h>
#include <stdint.h>

// used to initialize static storage
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

// number of pipe lists in memory
static uintptr_t pipe_lists = 0;

// enforces mutually exclusive access to the counter
static pthread_mutex_t memtest_lock;

#endif // MEMTEST





// --------------- initialization and teardown -------------------------------------------------------------




#ifdef MEMTEST
static void
lazy_initialization ()

	  // Initialize static storage.
{
  int err;
  pthread_mutexattr_t a;

  err = 0;
  if (! _nthm_error_checking_mutex_type (&a, &err))
	 goto a;
  if (pthread_mutex_init (&memtest_lock, &a) ? (err = THE_IER(123)) : 0)
	 goto b;
  if (pthread_mutexattr_destroy (&a) ? (!(err = THE_IER(124))) : 1)
	 return;
  pthread_mutex_destroy (&memtest_lock);
  goto a;
 b: pthread_mutexattr_destroy (&a);
 a: fprintf (stderr, "%s\n", nthm_strerror (err));
  exit (EXIT_FAILURE);
}
#endif




void
_nthm_close_pipl ()

	  // Report memory leaks.
{
#ifdef MEMTEST
  pthread_once (&once_control, lazy_initialization);
  _nthm_globally_throw (pthread_mutex_destroy (&memtest_lock) ? THE_IER(125) : 0);
  if (pipe_lists)
	 fprintf (stderr, "%lu unreclaimed pipe list%s\n", pipe_lists, pipe_lists == 1 ? "" : "s");
#endif
}



// --------------- pipe list construction ------------------------------------------------------------------





pipe_list
_nthm_pipe_list_of (p, err)
	  nthm_pipe p;
	  int *err;

	  // Return a newly created unit list containing only the pipe p
	  // and no complement provided there is sufficient memory.
{
  pipe_list t;

#ifdef MEMTEST
  pthread_once (&once_control, lazy_initialization);
#endif
  t = NULL;
  if ((p ? 0 : IER(126)) ? 1 : (t = (pipe_list) malloc (sizeof (*t))) ? 0 : (*err = (*err ? *err : ENOMEM)))
	 return NULL;
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  pipe_lists++;
  pthread_mutex_unlock (&memtest_lock);
#endif
  memset (t, 0, sizeof (*t));
  t->pipe = p;
  return t;
}






int
_nthm_new_complementary_pipe_lists (r, d, b, s, err)
	  pipe_list *r;
	  nthm_pipe d;
	  pipe_list *b;
	  nthm_pipe s;
	  int *err;

	  // Allocate and initialize complementary unit pipe lists r of d
	  // and b of s if there is sufficient memory.
{
  if ((! b) ? IER(127) : (! r) ? IER(128) : ! (*r = _nthm_pipe_list_of (d, err)))
	 return 0;
  if ((*b = _nthm_pipe_list_of (s, err)))
	 {
		(*b)->complement = *r;
		(*r)->complement = *b;
		return 1;
	 }
  if (_nthm_freed (*r, err) ? 1 : IER(129))
	 *r = NULL;
  return 0;
}








int
_nthm_pushed (t, b, err)
	  pipe_list t;
	  pipe_list *b;
	  int *err;

	  // Concatenate a unit list t with a list b.
{
  if ((! t) ? IER(130) : (! b) ? IER(131) : t->previous_pipe ? IER(132) : t->next_pipe ? IER(133) : 0)
	 return 0;
  if ((t->next_pipe = *(t->previous_pipe = b)))
	 (*b)->previous_pipe = &(t->next_pipe);
  return ! ! (*b = t);
}








int
_nthm_enqueued (t, f, q, err)
	  pipe_list t;
	  pipe_list *f;
	  pipe_list *q;
	  int *err;

	  // Append the unit list t to the queue that starts with f and
	  // ends with q.
{
  pipe_list *p;

  if ((! f) ? IER(134) : (! q) ? IER(135) : ((! *f) != ! *q) ? IER(136) : (! *q) ? 0 : (*q)->next_pipe ? IER(137) : 0)
	 return 0;
  return ((! _nthm_pushed (t, p = (*q ? &((*q)->next_pipe) : f), err)) ? 0 : (*q = *p) ? 1 : ! IER(138));
}







// --------------- pipe list demolition --------------------------------------------------------------------






int
_nthm_severed (t, err)
	  pipe_list t;
	  int *err;

	  // Remove an item from a pipe list without freeing it.
{
  if ((! t) ? IER(139) : t->previous_pipe ? 0 : IER(140))
	 return 0;
  if ((*(t->previous_pipe) = t->next_pipe))
	 t->next_pipe->previous_pipe = t->previous_pipe;
  t->previous_pipe = NULL;
  t->next_pipe = NULL;
  return 1;
}








int
_nthm_freed (r, err)
	  pipe_list r;
	  int *err;

	  // Free a unit pipe list and remove the reference to it from its
	  // complement, if any.
{
#ifdef MEMTEST
  pthread_once (&once_control, lazy_initialization);
#endif
  if ((! r) ? IER(141) : r->next_pipe ? IER(142) : r->previous_pipe ? IER(143) : 0)
	 return 0;
  if (r->complement)
	 {
		if ((r->complement->complement == r) ? 0 : IER(144))
		  return 0;
		r->complement->complement = NULL;
	 }
  free (r);
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  pipe_lists--;
  pthread_mutex_unlock (&memtest_lock);
#endif
  return 1;
}







nthm_pipe
_nthm_unilaterally_delisted (t, err)
	  pipe_list *t;
	  int *err;

	  // Remove an item from a pipe list, free it, and remove the
	  // reference from its complement.
{
  nthm_pipe p;
  pipe_list o;

  if (t ? 0 : IER(145))
	 return NULL;
  if (! (p = (*t ? (*t)->pipe : NULL)))
	 return NULL;
  _nthm_severed (o = *t, err);
  _nthm_freed (o, err);
  *t = NULL;
  return p;
}





nthm_pipe
_nthm_popped (f, err)
	  pipe_list *f;
	  int *err;

	  // Return the first pipe in a list f and bilaterally delist it.
{
  pipe_list o;
  nthm_pipe p;

  if ((! f) ? IER(146) : (! *f) ? IER(147) : 0)
	 return NULL;
  o = (*f)->next_pipe;
  _nthm_unilaterally_delisted (&((*f)->complement), err);
  p = _nthm_unilaterally_delisted (f, err);
  *f = o;
  return p;
}






nthm_pipe
_nthm_dequeued (f, q, err)
	  pipe_list *f;
	  pipe_list *q;
	  int *err;

	  // Bilaterally delist and return the first pipe in a non-empty
	  // queue, or return NULL if the queue is empty.
{
  nthm_pipe p;

  if ((! q) ? IER(148) : (! f) ? IER(149) : ((! *f) != ! *q) ? IER(150) : ! *f)
	 return NULL;
  if (*f != *q)
	 return _nthm_popped (f, err);
  if ((p = _nthm_popped (f, err)))
	 *q = NULL;
  return p;
}









nthm_pipe
_nthm_bilaterally_dequeued (r, f, q, err)
	  pipe_list r;
	  pipe_list *f;
	  pipe_list *q;
	  int *err;

	  // Bilaterally delist a pipe whose complement may be in a queue.
{
  nthm_pipe p;
  pipe_list c;

  if ((! f) ? IER(151) : (! q) ? IER(152) : (! r) ? IER(153) : 0)
	 return NULL;
  c = r->complement;
  if (c == *f)
	 return _nthm_dequeued (f, q, err);
  if (c != *q)
	 return _nthm_popped (&c, err);
  if (*f ? 0 : IER(154))
	 return NULL;
  p = _nthm_popped (q, err);
  for (*q = *f; (*q)->next_pipe; *q = (*q)->next_pipe);
  return p;
}









int
_nthm_bilaterally_freed (r, b, err)
	  pipe_list r;
	  pipe_list b;
	  int *err;

	  // Free a pair of complementary unit pipe lists.
{
  if ((! r) ? IER(155) : (! b) ? IER(156) : (r->complement != b) ? IER(157) : (b->complement != r) ? IER(158) : 0)
	 return 0;
  if (! _nthm_freed (r, err))
	 return 0;
  if (! _nthm_freed (b, err))
	 return 0;
  return 1;
}
