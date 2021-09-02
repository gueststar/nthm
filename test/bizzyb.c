// test the functions nthm_busy, nthm_blocked, nthm_truncate,
// nthm_truncate_all, and nthm_truncated

#include <nthm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define EXPECTED_RESULT 2216768150


uintptr_t
bizzy_bee (x, err)
	  void *x;
	  int *err;

	  // Ignore the input, wait until truncated, and then return a
	  // constant value.
{
  unsigned i;

  for (i = 0; (i & 0x3ff) ? 1 : ! nthm_truncated (err); i++);
  return EXPECTED_RESULT;
}




int
main(argc, argv)
	  int argc;
	  char **argv;
{
  void *x;
  int i, err;
  nthm_pipe source;

  x = 0;
  err = 0;
  for (i = 0; err ? 0 : (i <= 1); i++)
	 {
		source = nthm_open ((nthm_worker) &bizzy_bee, x, &err);
		if ((! source) ? 1 : err ? 1 : (! nthm_busy (source, &err)) ? 1 : ! nthm_blocked (&err))
		  {
			 printf (err ? "bizzyb failed\n%s\n" : "bizzyb failed\n", nthm_strerror(err));
			 exit(EXIT_FAILURE);
		  }
		if (i)
		  nthm_truncate_all (&err);
		else
		  nthm_truncate (source, &err);
		if ((! source) ? 1 : err ? 1 : (((uintptr_t) nthm_read (source, &err)) != EXPECTED_RESULT))
		  {
			 printf (err ? "bizzyb failed\n%s\n" : "bizzyb failed\n", nthm_strerror(err));
			 exit(EXIT_FAILURE);
		  }
	 }
  printf ("bizzyb detected no errors\n");
  exit(EXIT_SUCCESS);
}
