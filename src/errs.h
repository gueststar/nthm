/*
  Nthm -- non-preemptive thread hierarchy manager

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

// declarations related to error handling

// the n-th internal error code
#define THE_IER(n) (-n)

// macro for raising an internal error without overwriting an existing error
#define IER(n) (*err = (*err ? *err : THE_IER(n)))

// arbitrary magic number for consistency checks; most positive numbers less than 2^31 would also work
#define MAGIC 1887434018

// the n-th non-magic value; as with internal errors, distinguishable values are used to log the point of detection
#define MUGGLE(n) n

// initialize the attributes for a mutex to use error checking
extern int
_nthm_error_checking_mutex_type (pthread_mutexattr_t *a, int *err);

// safely assign the global error code
extern void
_nthm_globally_throw (int err);

// initialize pthread resources
extern int
_nthm_open_errs (int *err);

// release pthread resources and report errors to stderr
extern void
_nthm_close_errs (void);
