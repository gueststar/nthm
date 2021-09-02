// test truncating a few threads at random

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include "testconfig.h"

// used to keep track of the numbers that didn't get added to the sum due to truncation
uintptr_t global_shortfall;

// used for mutually exclusive access to the shortfall
pthread_mutex_t global_lock;



uintptr_t
approximate_sum_of_interval (x, err)
	  interval x;
	  int *err;

	  // This function computes a rubbish approximation of the sum over
	  // an interval by quitting if it detects getting truncated. It
	  // might also randomly decide to truncate any threads it creates
	  // to sum the subintervals.
{
  uintptr_t i, total, start, count, omission;
  interval subinterval;
  nthm_pipe source;

  total = 0;
  if (!x)
	 return total;
  count = (uintptr_t) rand () >> (x->depth >> 1);
  if (((x->depth > 2) ? (rand () & 0x1) : 0) ? 1 : (!count) ? 1 : (x->count <= count))
	 {
		for (i = x->start; (!(i < x->start + x->count)) ? 0 : (i & 0xfffff) ? 1 : ! nthm_truncated (err); total += i++);
		for (omission = 0; i < x->start + x->count; omission += i++);
		pthread_mutex_lock (&global_lock);
		global_shortfall += omission;
		pthread_mutex_unlock (&global_lock);
	 }
  else
	 {
		source = NULL;
		start = x->start;
		while (*err ? 0 : start < x->start + x->count)
		  {
			 if (start + count > x->start + x->count)
				count = x->start + x->count - start;
			 if (!(subinterval = (interval) malloc (sizeof (*subinterval))))
				*err = ENOMEM;
			 else
				{
				  subinterval->start = start;
				  subinterval->count = count;
				  subinterval->depth = x->depth + 1;
				  if (! (source = nthm_open ((nthm_worker) &approximate_sum_of_interval, (void *) subinterval, err)))
					 free (subinterval);
				  else if (! (rand () & 0x3))
					 nthm_truncate (source, err);
				}
			 start = start + count;
			 count = (uintptr_t) rand () >> (x->depth >> 1);
		  }
		if (! (rand () & 0x3))
		  nthm_truncate_all (err);
		while (*err ? NULL : (source = nthm_select (err)))
		  total += (uintptr_t) nthm_read (source, err);
	 }
  free (x);
  return total;
}




int
main(argc, argv)
	  int argc;
	  char **argv;
{
  int err;
  interval x;
  unsigned seed;

  err = 0;
  global_shortfall = 0;
  GETRANDOM(seed);
  srand (seed);
  if (!(x = (interval) malloc (sizeof (*x))))
	 err = ENOMEM;
  else
	 {
		x->depth = 2;
		x->start = 0;
		x->count = LAST_TERM;
		pthread_mutex_init (&global_lock, NULL);
		if (approximate_sum_of_interval (x, &err) + global_shortfall == EXPECTED_CUMULATIVE_SUM)
		  {
			 pthread_mutex_destroy (&global_lock);
			 printf ("rubbish detected no errors\n");
			 exit (EXIT_SUCCESS);
		  }
		pthread_mutex_destroy (&global_lock);
	 }
  printf (err ? "rubbish failed with seed 0x%x\n%s\n" : "rubbish failed with seed 0x%x\n", seed, nthm_strerror(err));
  exit (EXIT_FAILURE);
}
