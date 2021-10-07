// Test synchronization of sent jobs. This program should take
// slightly more than one second because nthm waits for the job to
// start and then finish before allowing the process to exit.  The
// sent job should observe that the global resource is still set
// because the main thread calls nthm_sync before clearing it.

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

static int global_resource = 1;



void
slacker (x)
	  void *x;

	  // Ignore the input and wait.
{
  sleep (1);
  if (! global_resource)
	 {
		printf ("synchrotron subsequently failed\n");
		exit (EXIT_FAILURE);
	 } 
}




int
main (argc, argv)
	  int argc;
	  char **argv;
{
  void *x;
  int err;

  err = 0;
  x = NULL;
  nthm_send ((nthm_slacker) &slacker, x, &err);
  nthm_sync (&err);
  global_resource = 0;
  printf (err ? "synchrotron failed\n%s\n" : "synchrotron detected no errors\n", nthm_strerror(err));
  exit (err ? EXIT_FAILURE : EXIT_SUCCESS);
}
