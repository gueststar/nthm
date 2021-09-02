// Confirm that pipes opened within a local scope are selectable only
// in that scope.

#include <nthm.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define SCOPE_DEPTH 1
#define CONCURRENCY 5
#define DELAY 10000

uintptr_t
echo (x, err)
	  uintptr_t x;
	  int *err;

	  // Echo the input. The delay makes the threads with lower values
	  // of x likely to finish first so that passing the test by chance
	  // is less likely.
{
  usleep ((unsigned) x * DELAY);
  return x;
}




int
main (argc, argv)
	  int argc;
	  char **argv;
{
  nthm_pipe p;
  int err, valid;
  uintptr_t scope, result, local_pipes, global_pipes;

  global_pipes = scope = 0;
  for (err = ! (valid = 1); scope < SCOPE_DEPTH; scope++)    // open CONCURRENCY pipes in each of SCOPE_DEPTH nested scopes
	 {
		for (local_pipes = 0; local_pipes < CONCURRENCY; local_pipes++)
		  global_pipes += ! ! nthm_open ((nthm_worker) &echo, (void *) scope, &err);
		nthm_enter_scope (&err);
	 }
  while (scope--)
	 {
		nthm_exit_scope (&err);
		for (local_pipes = 0; err ? NULL : (p = nthm_select (&err)); local_pipes++)  // only the innermost pipes are expected
		  {
			 global_pipes--;
			 valid = (((result = (uintptr_t) nthm_read (p, &err)) == scope) ? valid : 0);
		  }
		valid = (valid ? (local_pipes == CONCURRENCY) : 0);
	 }
  if (err ? 0 : global_pipes ? 0 : valid)               // there should have been SCOPE_DEPTH * CONCURRENCY pipes globally
	 {
		printf ("scopestrial detected no errors\n");
		exit (EXIT_SUCCESS);
	 }
  printf (err ? "scopestrial failed\n%s\n" : "scopestrial failed\n", nthm_strerror(err));
  exit (EXIT_FAILURE);
}
