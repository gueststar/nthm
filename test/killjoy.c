// test killing a few threads at random

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include "testconfig.h"



uintptr_t
approximate_sum_of_interval (x, err)
	  interval x;
	  int *err;

	  // This function is just like a similarly named function in
	  // rubbish.c except that it kills threads instead of truncating
	  // them.

{
  uintptr_t i, total, start, count;
  interval subinterval;
  nthm_pipe source;
  int any_killed;

  total = 0;
  if (!x)
	 return total;
  count = (uintptr_t) rand () >> (x->depth >> 1);
  if ((!count) ? 1 : (x->count <= count))
	 for (i = x->start; (!(i < x->start + x->count)) ? 0 : (i & 0xfff) ? 1 : ! nthm_killed (err); total += i++);
  else
	 {
		source = NULL;
		any_killed = 0;
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
				  source = nthm_open ((nthm_worker) &approximate_sum_of_interval, subinterval, err);
				  if (! source)
					 free (subinterval);
				  else if ((rand () & 0x1) ? (any_killed = 1) : 0)
					 nthm_kill (source, err);
				}
			 start = start + count;
			 count = (uintptr_t) rand () >> (x->depth >> 1);
		  }
		if (any_killed ? 0 : ! (rand () & 0x3))  // it would be an error to kill any thread both ways
		  nthm_kill_all (err);
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
  uintptr_t total;
  unsigned seed;

  err = 0;
  GETRANDOM(seed);
  srand (seed);
  if (!(x = (interval) malloc (sizeof (*x))))
	 err = ENOMEM;
  else
	 {
		x->depth = 2;
		x->start = 0;
		x->count = LAST_TERM;
		total = approximate_sum_of_interval (x, &err);
		if (total)
		  total = 0;
		if (! err)
		  {
			 printf ("killjoy detected no errors\n");
			 exit (EXIT_SUCCESS);
		  }
	 }
  printf (err ? "killjoy failed with seed 0x%x\n%s\n" : "killjoy failed with seed 0x%x\n", seed, nthm_strerror(err));
  exit (EXIT_FAILURE);
}
