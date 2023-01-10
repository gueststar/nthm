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

#ifndef NTHM_SCOPES_H
#define NTHM_SCOPES_H 1

#include <nthm.h>
#include "pipl.h"

// non-API scope stack operations

typedef struct scope_stack_struct *scope_stack;

struct scope_stack_struct
{
  unsigned truncation;        // set by user code when a partial result is acceptable
  pipe_list blockers;         // a list of pipes whose results are awaited
  pipe_list finishers;        // a list of pipes whose results are available in the order they finished
  pipe_list finisher_queue;   // points to the most recently finished pipe in this scope
  scope_stack enclosure;      // specifications of enclosing scopes
};

// enter a local scope by pushing the current descendants into an enclosing scope
extern int
_nthm_scope_entered (nthm_pipe p, int *err);

// exit a scope by retrieving the former descendants from an enclosing scope
extern int
_nthm_scope_exited (nthm_pipe p, int *err);

// return the current scope level of a pipe
extern uintptr_t
_nthm_scope_level (nthm_pipe p, int *err);

// return non-zero if d is the drain of s in the drain's current scope
extern int
_nthm_drained_by (nthm_pipe s, nthm_pipe d, int *err);

// exit all enclosed scopes
extern void
_nthm_vacate_scopes (nthm_pipe s, int *err);

// report memory leaks
extern void
_nthm_close_scopes (void);

#endif
