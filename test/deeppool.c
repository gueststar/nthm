// test a deep thread pool

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include "testconfig.h"




uintptr_t
sum_of_interval (x, err)
	  interval x;
	  int *err;

	  // Return the summation over an interval computed sequentially if
	  // the interval is small and concurrently if it's large.
{
  uintptr_t i, total, start, count;
  interval subinterval;
  nthm_pipe source;

  total = 0;
  if (!x)
	 return total;
  count = (uintptr_t) rand () >> (x->depth >> 1);
  if ((!count) ? 1 : (x->count <= count))
	 for (i = x->start; i < x->start + x->count; total += i++);
  else
	 {
		start = x->start;
		while (*err ? 0 : start < x->start + x->count)
		  {
			 if (start + count > x->start + x->count)
				count = x->start + x->count - start;
			 if (! (subinterval = (interval) malloc (sizeof (*subinterval))))
				*err = ENOMEM;
			 else
				{
				  subinterval->start = start;
				  subinterval->count = count;
				  subinterval->depth = x->depth + 1;
				  if (! nthm_open ((nthm_worker) &sum_of_interval, (void *) subinterval, err))
					 free (subinterval);
				}
			 start = start + count;
			 count = (uintptr_t) rand () >> (x->depth >> 1);
		  }
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
  unsigned s;

  err = 0;
  GETRANDOM(s);
  srand (s);
  if (!(x = (interval) malloc (sizeof (*x))))
	 err = ENOMEM;
  else
	 {
		x->depth = 2;
		x->start = 0;
		x->count = LAST_TERM;
		if (sum_of_interval (x, &err) == EXPECTED_CUMULATIVE_SUM)
		  {
			 printf ("deeppool detected no errors\n");
			 exit(EXIT_SUCCESS);
		  }
	 }
  printf (err ? "deeppool failed with seed 0x%x\n%s\n" : "deeppool failed with seed 0x%x\n", s, nthm_strerror(err));
  exit (EXIT_FAILURE);
}
