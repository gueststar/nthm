/*
  nthm -- non-preemptive thread hierarchy manager

  copyright (c) 2020-2023 Dennis Furey

  Nthm is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Nthm is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
  License for more details.

  You should have received a copy of the GNU General Public License
  along with nthm. If not, see <https://www.gnu.org/licenses/>.
*/

#include <nthm.h>

// non-API routines pertaining to dataflow among threads

// read from a pipe with no designated drain
extern void *
_nthm_untethered_read (nthm_pipe source, int *err);

// read from a source whose drain is running in the current context
extern void *
_nthm_tethered_read (nthm_pipe source, int *err);

// used as a start routine for pthread_create
extern void *
_nthm_manager (void *void_pointer);

// return the address of a location used for reporting unrecoverable pthread errors
extern int *
_nthm_deadlocked (void);
