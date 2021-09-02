// test a flat thread pool

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "testconfig.h"

uintptr_t
sum_of_interval (x, err)
	  interval x;
	  int *err;

	  // Return the summation over an interval computed the hard way.
{
  uintptr_t total;
  uintptr_t i;

  total = 0;
  if (x)
	 {
		for (i = x->start; i < x->start + x->count; total += i++);
		free (x);
	 }
  return total;
}




int
main(argc, argv)
	  int argc;
	  char **argv;
{
  int err;
  interval x;
  nthm_pipe source;
  uintptr_t start, count, cumulative_sum;
  unsigned seed;

  err = 0;
  GETRANDOM (seed);
  srand (seed);
  start = cumulative_sum = 0;
  while (err ? 0 : (start < LAST_TERM))
	 {
		count = (uintptr_t) rand () >> 2;
		if (start + count > LAST_TERM)
		  count = LAST_TERM - start;
		if (!(x = (interval) malloc (sizeof (*x))))
		  err = ENOMEM;
		else
		  {
			 x->start = start;
			 x->count = count;
			 if (! nthm_open ((nthm_worker) &sum_of_interval, (void *) x, &err))
				free (x);
		  }
		start += count;
	 }
  while (err ? NULL : (source = nthm_select (&err)))
	 cumulative_sum += (uintptr_t) nthm_read (source, &err);
  if (err ? 0 : (cumulative_sum == EXPECTED_CUMULATIVE_SUM))
	 {
		printf ("flatpool detected no errors\n");
		exit(EXIT_SUCCESS);
	 }
  printf (err ? "flatpool failed with seed 0x%x\n%s\n" : "flatpool failed with seed 0x%x\n", seed, nthm_strerror(err));
  exit(EXIT_FAILURE);
}
