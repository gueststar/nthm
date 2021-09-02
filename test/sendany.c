// Test synchronization of sent jobs. This program should take
// slightly more than one second because nthm waits for the job to
// start and then finish before allowing the process to exit.

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>


void
slacker (x)
	  void *x;

	  // Ignore the input and wait.
{
  sleep (1);
}




int
main(argc, argv)
	  int argc;
	  char **argv;
{
  void *x;
  int err;

  err = 0;
  x = NULL;
  nthm_send ((nthm_slacker) &slacker, x, &err);
  printf (err ? "sendany failed\n%s\n" : "sendany detected no errors\n", nthm_strerror(err));
  exit (err ? EXIT_FAILURE : EXIT_SUCCESS);
}
