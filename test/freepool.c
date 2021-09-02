// test reading a pipe in an unmanaged thread context

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include "testconfig.h"


typedef struct pipe_list_struct *pipe_list; // a list of pipes

struct pipe_list_struct
{
  nthm_pipe head;      // first pipe in the list
  pipe_list tail;      // rest of the list
};




void
push (h, t, err)
	  nthm_pipe h;
	  pipe_list *t;
	  int *err;

	  // Untether a pipe h and push it into a list t.
{
  pipe_list l;

  nthm_untether (h, err);                          // make the pipe readable in any context
  if (! (l = (pipe_list) malloc (sizeof (*l))))
	 {
		*err = ENOMEM;
		return;
	 }
  l->head = h;
  l->tail = *t;
  *t = l;
}





uintptr_t
sum_of_interval (x, err)
	  interval x;
	  int *err;

	  // Return the summation over an interval computed the hard
	  // way. This function runs in a thread created by nthm_open
	  // and its output goes through a pipe.
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






static void*
add (void_pointer)
	  void *void_pointer;

	  // Sum the outputs from a list of untethered pipes created in a
     // different context. This function runs in a thread created
     // directly by pthread_create, and receives a list of pipes
     // opened in the main thread.
{
  uintptr_t *cumulative_sum;
  pipe_list pipes, t;
  int err;

  err = 0;
  pipes = (pipe_list) void_pointer;
  if (! (cumulative_sum = (uintptr_t *) malloc (sizeof (*cumulative_sum))))
	 pthread_exit (NULL);
  for (*cumulative_sum = 0; err ? 0 : pipes; free (t))
	 {
		*cumulative_sum += (uintptr_t) nthm_read (pipes->head, &err);
		pipes = (t = pipes)->tail;
	 }
  pthread_exit ((void *) cumulative_sum);
}






int main(argc, argv)
	  int argc;
	  char **argv;
{
  int err;
  interval x;
  pthread_t adder;
  pipe_list pipes;
  uintptr_t *cumulative_sum;
  uintptr_t start, count;
  unsigned seed;

  err = 0;
  start = 0;
  pipes = NULL;
  GETRANDOM(seed);
  srand (seed);
  while (err ? 0 : start < LAST_TERM)
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
			 push (nthm_open ((nthm_worker) &sum_of_interval, (void *) x, &err), &pipes, &err);
		  }
		start = start + count;
	 }
  if (err ? 0 : ! (err = pthread_create (&adder, NULL, add, (void *) pipes)))
	 {
		err = pthread_join (adder, (void **) &cumulative_sum);
		if (err ? 0 : (*cumulative_sum == EXPECTED_CUMULATIVE_SUM))
		  {
			 free (cumulative_sum);
			 printf ("freepool detected no errors\n");
			 exit(EXIT_SUCCESS);
		  }
		free (cumulative_sum);
	 }
  printf (err ? "freepool failed with seed 0x%x\n%s\n" : "freepool failed with seed 0x%x\n", seed, nthm_strerror(err));
  exit(EXIT_FAILURE);
}
