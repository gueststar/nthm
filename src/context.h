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

// non-API routines for thread specific storage of pipes

// return a pointer to the pipe node corresponding to the currently running thread, if any
extern nthm_pipe 
_nthm_current_context (void);

// identify a pipe with the current thread context
extern int
_nthm_set_context (nthm_pipe drain, int *err);

// identify no pipe with the current thread context
extern void
_nthm_clear_context (int *err);

// return an existing pipe associated with the current thread or create one and pool it
extern nthm_pipe
_nthm_current_or_new_context (int *err);

// eagerly create the thread specific storage key
extern int
_nthm_open_context (int *err);

// free the thread specific storage key
extern void
_nthm_close_context (void);
