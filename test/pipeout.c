// test whether it's possible to read from a pipe

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define EXPECTED_RESULT 2568413717


uintptr_t
pingback (x, err)
	  uintptr_t x;
	  int *err;

	  // Ignore the input and return a constant value.
{
  return EXPECTED_RESULT;
}




int
main(argc, argv)
	  int argc;
	  char **argv;
{
  int err;
  uintptr_t x;
  nthm_pipe source;
  uintptr_t result;

  err = 0;
  result = 0;
  x = 0;
  if ((source = nthm_open ((nthm_worker) &pingback, (void *) x, &err)))
	 result = (uintptr_t) nthm_read (source, &err);
  if (err ? 0 : (result == EXPECTED_RESULT))
	 {
		printf ("pipeout detected no errors\n");
		exit(EXIT_SUCCESS);
	 }
  printf (err ? "pipeout failed\n%s\n" : "pipeout failed\n", nthm_strerror(err));
  exit(EXIT_FAILURE);
}
