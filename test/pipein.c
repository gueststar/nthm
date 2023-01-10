// confirm that a pipe can receive input

#include <nthm.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define EXPECTED_INPUT 2568413717

uintptr_t
comparitor (x, err)
	  uintptr_t x;
	  int *err;

	  // Compare the input to a constant value.
{
  return (x == EXPECTED_INPUT);
}




int
main(argc, argv)
	  int argc;
	  char **argv;
{
  int err;
  nthm_pipe p;
  uintptr_t x;
  uintptr_t result;

  err = 0;
  x = EXPECTED_INPUT;
  p = nthm_open ((nthm_worker) &comparitor, (void *) x, &err);
  result = (uintptr_t) nthm_read (p, &err);
  if (err ? 0 : result)
	 {
		printf ("pipein detected no errors\n");
		exit (EXIT_SUCCESS);
	 }
  printf (err ? "pipein failed\n%s\n" : "pipein failed\n", nthm_strerror(err));
  exit (EXIT_FAILURE);
}
