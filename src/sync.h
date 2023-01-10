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

#include <nthm.h>

//  non-API routines for thread creation and synchronization ensuring
//  that all threads are joined on termination

#ifndef PTHREAD_STACK_MIN
// estimated minimum stack size to cover the overhead of creating a thread if not defined by the system
#define PTHREAD_STACK_MIN 65536
#endif

typedef struct thread_spec_struct *thread_spec;    // passed to the function used to start threads

struct thread_spec_struct
{
  nthm_pipe pipe;
  int write_only;
  nthm_worker operator;
  nthm_slacker mutator;
  void *operand;
};

// --------------- memory management -----------------------------------------------------------------------

// initialize static storage
extern int
_nthm_open_sync (int *err);

// initialize the attributes for all created threads
extern int
_nthm_stack_limited_thread_type (pthread_attr_t *a, int *err);

// return a newly allocated and initialized thread_spec
extern thread_spec
_nthm_thread_spec_of (nthm_pipe source, nthm_worker operator, nthm_slacker mutator, void *operand, int write_only, int *err);

// free a thread spec
extern void
_nthm_unspecify (thread_spec t, int *err);

// --------------- thread synchronization ------------------------------------------------------------------

// bump the count of running threads, returning non-zero if successful
extern int
_nthm_registered (int *err);

// safely check whether a thread has started
extern int
_nthm_started (int *err);

// queue the current thread to be joined
extern void
_nthm_relay_race (int *err);

// block until all running threads have yielded
extern void
_nthm_synchronize (int *err);

// wait for the last thread to finish and report errors on stderr
extern void
_nthm_close_sync (void);
