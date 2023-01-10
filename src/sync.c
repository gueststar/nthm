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
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include "errs.h"
#include "sync.h"
#include "pipes.h"
#include "nthmconfig.h"
#ifdef MEMTEST                  // keep counts of allocated structures; not suitable for production code
#include <stdio.h>

// number of thread specifications in memory
static uintptr_t thread_specs = 0;

// for mutually exclusive access to the count
static pthread_mutex_t memtest_lock;

#endif // MEMTEST

// non-zero means at least one thread has been created since the application started; used in the exit routine
static int starting = 0;

// the number of recently created threads whose creation has not yet been confirmed by a call to _nthm_started
static uintptr_t starters = 0;

// used to wake up threads waiting for confirmation in _nthm_started
static pthread_cond_t started;

// secures mutually exclusive access to starters and started
static pthread_mutex_t starter_lock;

// the number of currently extant threads not yet joined by _nthm_relay_race or _nthm_synchronize
static uintptr_t runners = 0;

// secures mutually exclusive access to runners and starting
static pthread_mutex_t runner_lock;

// non-zero when there is a thread waiting to be joined by _nthm_relay_race or _nthm_synchronize
static int finishers = 0;

// wakes up threads waitng to be joined
static pthread_cond_t finished;

// holds the thread id of the waiting thread to be joined next by _nthm_relay_race or _nthm_synchronize
static pthread_t finishing_thread;

// wakes up _nthm_synchronize when the last thread is ready to be joined
static pthread_cond_t last_runner;

// non-zero means unrecoverable pthread errors or counter overflows have sabotaged the synchronization protocol
static int deadlocked = 0;




// --------------- initialization --------------------------------------------------------------------------






int
_nthm_stack_limited_thread_type (a, err)
	  pthread_attr_t *a;
	  int *err;

	  // Initialize the attributes for all created threads. On 32-bit
	  // systems, use small stacks.

{
  if ((!a) ? IER(297) : pthread_attr_init (a) ? IER(298) : 0)
	 return 0;
#ifdef USE_SMALL_STACKS
  if (pthread_attr_setstacksize (a, PTHREAD_STACK_MIN + NTHM_STACK_MIN) ? IER(299) : 0)
	 {
		pthread_attr_destroy (a);
		return 0;
	 }
#endif
  return 1;
}






int
_nthm_open_sync (err)
	  int *err;

	  // Initialize static storage.
{
  pthread_mutexattr_t a;

  if (deadlocked ? IER(300) : ! _nthm_error_checking_mutex_type (&a, err))
	 return 0;
  if (pthread_mutex_init (&starter_lock, &a) ? IER(301) : 0)
	 goto a;
  if (pthread_mutex_init (&runner_lock, &a) ? IER(302) : 0)
	 goto b;
#ifdef MEMTEST
  if (pthread_mutex_init (&memtest_lock, &a) ? IER(303) : 0)
	 goto c;
#endif
  if ((pthread_mutexattr_destroy (&a) ? 1 : pthread_cond_init (&last_runner, NULL)) ? IER(304) : 0)
	 goto d;
  if (pthread_cond_init (&started, NULL) ? IER(305) : 0)
	 goto e;
  if (pthread_cond_init (&finished, NULL) ? IER(306) : 0)
	 goto f;
  return 1;
 f: pthread_cond_destroy (&started);
 e: pthread_cond_destroy (&last_runner);
 d: pthread_mutex_destroy (&runner_lock);
  pthread_mutex_destroy (&starter_lock);
#ifdef MEMTEST
  pthread_mutex_destroy (&memtest_lock);
#endif
  return 0;
#ifdef MEMTEST
 c: pthread_mutex_destroy (&runner_lock);
#endif
 b: pthread_mutex_destroy (&starter_lock);
 a: pthread_mutexattr_destroy (&a);
  return 0;
}







// --------------- teardown --------------------------------------------------------------------------------






static void
release_pthread_resources (err)
	  int *err;

	  // Release pthread related resources.
{
#ifdef MEMTEST
  if (pthread_mutex_destroy (&memtest_lock))
	 IER(307);
#endif
  if (pthread_mutex_destroy (&starter_lock))
	 IER(308);
  else if (pthread_cond_destroy (&started))
	 IER(309);
  if (pthread_mutex_destroy (&runner_lock) ? IER(310) : 0)
	 return;
  if (pthread_cond_destroy (&finished))
	 IER(311);
  if (pthread_cond_destroy (&last_runner))
	 IER(312);
}








void
_nthm_close_sync ()

	  // Free the static storage, synchronize with all threads and
	  // report errors on stderr.
{
  int err;

  err = 0;
  _nthm_synchronize (&err);
  if (! deadlocked)
	 release_pthread_resources (&err);
  _nthm_globally_throw (err ? err : deadlocked ? THE_IER(313) : 0);
#ifdef MEMTEST
  if (thread_specs)
	 fprintf (stderr, "%lu unreclaimed thread specification%s\n", thread_specs, thread_specs == 1 ? "" : "s");
#endif
}








// --------------- memory management -----------------------------------------------------------------------







thread_spec
_nthm_thread_spec_of (source, operator, mutator, operand, write_only, err)
	  nthm_pipe source;
	  nthm_worker operator;
	  nthm_slacker mutator;
	  void *operand;
	  int write_only;
	  int *err;

	  // Return a newly allocated and initialized thread_spec.
{
  thread_spec t;

  t = NULL;
  if (source ? ((t = (thread_spec) malloc (sizeof (*t))) ? 0 : (*err = (*err ? *err : ENOMEM))) : 1)
	 if (source ? (_nthm_retired (source, err) ? 1 : IER(314)) : 1)
		return NULL;
  memset (t, 0, sizeof (*t));
  t->write_only = write_only;
  t->operator = operator;
  t->mutator = mutator;
  t->operand = operand;
  t->pipe = source;
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  thread_specs++;
  pthread_mutex_unlock (&memtest_lock);
#endif
  return t;
}









void
_nthm_unspecify (t, err)
	  thread_spec t;
	  int *err;

	  // Free a thread specification structure.
{
  if (t ? 0 : IER(315))
	 return;
  if ((! (t->pipe)) ? 1 : (t->pipe->zombie = 1))
	 free (t);
#ifdef MEMTEST
  pthread_mutex_lock (&memtest_lock);
  thread_specs--;
  pthread_mutex_unlock (&memtest_lock);
#endif
}






// --------------- thread synchronization ------------------------------------------------------------------



 
int
_nthm_registered (err)
	  int *err;

	  // Bump the count of running threads and assert that a thread
	  // has started. This function should be called by the start
	  // routine of a new thread when it starts.
{
  if (deadlocked ? 1 : pthread_mutex_lock (&runner_lock) ? (deadlocked = IER(316)) : 0)
	 return 0;
  if ((runners += (uintptr_t) (starting = 1)) ? 0 : IER(317))
	 deadlocked = 1;
  if (pthread_mutex_unlock (&runner_lock) ? (deadlocked = IER(318)) : 0)
	 return 0;
  if (deadlocked ? 1 : pthread_mutex_lock (&starter_lock) ? (deadlocked = IER(319)) : 0)
	 return 0;
  if (starters++ ? 0 : (! starters) ? IER(320) : pthread_cond_broadcast (&started) ? IER(321) : 0)
	 deadlocked = 1;
  if (pthread_mutex_unlock (&starter_lock) ? IER(322) : 0)
	 deadlocked = 1;
  return ! deadlocked;
}









int
_nthm_started (err)
	  int *err;

	  // Safely confirm that a thread has started. This function
	  // should be called by a routine that creates a thread after
	  // creating it.
{

  if (deadlocked ? 1 : pthread_mutex_lock (&starter_lock) ? (deadlocked = IER(323)) : 0)
	 return 0;
  while (deadlocked ? 0 : starters ? 0 : ! (pthread_cond_wait (&started, &starter_lock) ? IER(324) : 0));
  starters -= ! deadlocked;
  return ! (pthread_mutex_unlock (&starter_lock) ? (deadlocked = IER(325)) : deadlocked);
}








void
_nthm_relay_race (err)
	  int *err;

	  // Join with previous threads until they're all finished or
	  // there's a lull. Then schedule the current thread to be joined
	  // with the next one to finish, and wait for its signal before
	  // exiting. If the current thread is the last one running, then
	  // signal the exit routine to join with it instead. This waiting
	  // doesn't block the user code in the current thread because it
	  // yielded before this function was called.
{
  void *leak;
  pthread_t f;

  leak = NULL;
  if (deadlocked ? 1 : pthread_mutex_lock (&runner_lock) ? (deadlocked = IER(326)) : 0)
	 return;
  while (finishers ? finishers-- : 0)
	 {
		f = finishing_thread;
		if (pthread_cond_signal (&finished) ? (deadlocked = IER(327)) : 0)
		  goto a;
		if ((pthread_mutex_unlock (&runner_lock) ? 1 : pthread_join (f, &leak)) ? (deadlocked = IER(328)) : 0)
		  return;
		if (leak)
		  IER(329);
		if (deadlocked ? 1 : pthread_mutex_lock (&runner_lock) ? (deadlocked = IER(330)) : 0)
		  return;
	 }
  finishers++;
  finishing_thread = pthread_self ();
  if ((! runners) ? (deadlocked = IER(331)) : --runners ? 0 : pthread_cond_signal (&last_runner) ? (deadlocked = IER(332)) : 0)
	 goto a;
  if (deadlocked ? 1 : pthread_cond_wait (&finished, &runner_lock) ? IER(333) : 0)
	 deadlocked = 1;
 a: if (pthread_mutex_unlock (&runner_lock) ? IER(334) : 0)
	 deadlocked = 1;
}








void
_nthm_synchronize (err)
	  int *err;

	  // Join with the last thread still running, if any. This function
	  // is called only by the exit routine _nthm_close_sync but may
	  // also be called indirectly from user code through
	  // nthm_syncrhonize in the public API.
{
  void *leak;

  leak = NULL;
  if (deadlocked ? 1 : pthread_mutex_lock (&runner_lock) ? (deadlocked = IER(335)) : 0)
	 return;
  if (starting ? 0 : pthread_mutex_unlock (&runner_lock) ? (deadlocked = IER(336)) : 1)
	 return;
  starting = 0;
  if (deadlocked ? 1 : (! runners) ? 0 : pthread_cond_wait (&last_runner, &runner_lock) ? IER(337) : 0)
	 deadlocked = 1;
  if (pthread_cond_signal (&finished) ? IER(338) : 0)
	 deadlocked = 1;
  if (pthread_mutex_unlock (&runner_lock) ? IER(339) : 0)
	 deadlocked = 1;
  if (pthread_join (finishing_thread, &leak) ? IER(340) : 0)
	 deadlocked = 1;
  if (leak)
	 IER(341);
}
