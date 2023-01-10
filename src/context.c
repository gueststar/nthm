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
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include "context.h"
#include "pipes.h"
#include "pool.h"
#include "errs.h"

// used to retrieve the pipe writable by the currently executing thread
static pthread_key_t cursor;




// --------------- initialization and teardown -------------------------------------------------------------




int
_nthm_open_context (err)
	  int *err;

	  // Create the thread specific storage key.
{
  return ! (pthread_key_create (&cursor, NULL) ? IER(74) : 0);
}






void
_nthm_close_context ()

	  // Free the thread specific storage key.
{
  _nthm_globally_throw (pthread_key_delete (cursor) ? THE_IER(75) : 0);
}





// --------------- thread contexts -------------------------------------------------------------------------





nthm_pipe 
_nthm_current_context ()

	  // Return a pointer to the pipe node corresponding to the
	  // currently running thread, if any.
{
  return (nthm_pipe) pthread_getspecific (cursor);
}







int
_nthm_set_context (drain, err)
	  nthm_pipe drain;
	  int *err;

	  // Identify a pipe with the current thread context.
{
  int e;

  if (!(e = pthread_setspecific (cursor, (void *) drain)))
	 return 1;
  *err = (*err ? *err : (e == ENOMEM) ? e : (e = EAGAIN) ? e : THE_IER(76));
  return 0;
}







void
_nthm_clear_context (err)
	  int *err;

	  // Identify no pipe with the current thread context.
{
  _nthm_set_context (NULL, err);
}








nthm_pipe
_nthm_current_or_new_context (err)
	  int *err;

	  // Return an existing pipe associated with the current thread
	  // context or create one and pool it with the root pipes. In the
	  // latter case, the resulting pipe is a placeholder for an
	  // unmanaged thread whose descendants correspond to managed
	  // threads.
{
  nthm_pipe drain;

  if ((drain = _nthm_current_context ()) ? 1 : ! (drain = _nthm_new_pipe (err)))
	 return drain;
  drain->placeholder = 1;
  if (! _nthm_placed (drain, err))
	 goto a;
  if (_nthm_set_context (drain, err))
	 return drain;
 a: if (! _nthm_retired (drain, err))
	 IER(77);
  return NULL;
}
