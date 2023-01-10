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

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <nthm.h>
#include "errs.h"
#include "nthmconfig.h"

// the maximum number of error codes worth recording
#define ERROR_LIMIT 16

// used for storing error codes not reportable any other way
static int global_error[ERROR_LIMIT];

// the number of errors reported
static unsigned error_count = 0;

// for exclusive access to the global error code
static pthread_mutex_t error_lock;

// unrecoverable pthread error
static int deadlocked = 0;



// --------------- initialization and teardown -------------------------------------------------------------






int
_nthm_error_checking_mutex_type (a, err)
	  pthread_mutexattr_t *a;
	  int *err;

	  // Initialize the attributes for a mutex to use error checking.
{
  if ((!a) ? IER(78) : pthread_mutexattr_init (a) ? IER(79) : 0)
	 return 0;
  if (!(pthread_mutexattr_settype (a, PTHREAD_MUTEX_ERRORCHECK) ? IER(80) : 0))
	 return 1;
  pthread_mutexattr_destroy (a);
  return 0;
}





int
_nthm_open_errs (err)
	  int *err;

	  // Initialize pthread resources.
{
  pthread_mutexattr_t mutex_attribute;
  unsigned i;

  for (i = 0; i < ERROR_LIMIT; i++)
	 global_error[i] = 0;
  if (! _nthm_error_checking_mutex_type (&mutex_attribute, err))
	 return 0;
  if (pthread_mutex_init (&error_lock, &mutex_attribute) ? IER(81) : 0)
	 {
		pthread_mutexattr_destroy (&mutex_attribute);
		return 0;
	 }
  if (pthread_mutexattr_destroy (&mutex_attribute) ? (! IER(82)) : 1)
	 return 1;
  pthread_mutex_destroy (&error_lock);
  return 0;
}







void
_nthm_close_errs ()

	  // Report global errors.
{
  int last_error;
  unsigned i;

  last_error = 0;
  if (deadlocked ? 0 : (last_error = (pthread_mutex_destroy (&error_lock) ? THE_IER(83) : 0)) ? (error_count + 1) : 0)
	 {
		if (error_count < ERROR_LIMIT)
		  global_error[error_count] = last_error;
		error_count++;
	 }
  for (i = 0; i < error_count ? (i < ERROR_LIMIT) : 0; i++)
	 fprintf (stderr, "%s\n", nthm_strerror (global_error[i]));
  if (i == error_count)
	 return;
  fprintf (stderr, "nthm: %u further error%s ", error_count - i, (error_count - i > 1) ? "s" : "");
  fprintf (stderr, "w%s detected\n", (error_count - i > 1) ? "ere" : "as");
}




// --------------- error handling function of last resort --------------------------------------------------






void
_nthm_globally_throw (err)
	  int err;

	  // Safely assign the global error code.
{
  if (deadlocked ? 1 : ! err)
	 return;
  if (pthread_mutex_lock (&error_lock) ? (deadlocked = 1) : 0)
	 return;
  if (error_count + 1)                      // stop counting errors if the counter is about to overflow
	 {
		if (error_count < ERROR_LIMIT)
		  global_error[error_count] = err;
		error_count++;
	 }
  if (pthread_mutex_unlock (&error_lock))
	 deadlocked = 1;
}








// --------------- public-facing API -----------------------------------------------------------------------





const char*
nthm_strerror (err)
	  int err;

	  // Return a short explanation of the given error code. This
	  // function isn't thread safe.

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
	 case NTHM_NOTDRN: return "nthm: attempt to access a non-locally tethered pipe";
	 case NTHM_NULPIP: return "nthm: null pipe";
	 case NTHM_INVPIP: return "nthm: corrupted or invalid pipe";
	 case NTHM_KILLED: return "nthm: interrupted by a kill notification";
	 case NTHM_UNDFLO: return "nthm: scope underflow";
	 case NTHM_XSCOPE: return "nthm: [warning] scope not exited";
	 default:
		sprintf (error_buffer, IER_FMT, NTHM_VERSION_MAJOR, NTHM_VERSION_MINOR, NTHM_VERSION_PATCH, -err);
		return error_buffer;
	 }
}
