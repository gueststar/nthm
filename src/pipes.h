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

// non-API non-mutating operations on the nthm_pipe data structure

#include <nthm.h>
#include "scopes.h"

struct nthm_pipe_struct
{
  int valid;                  // holds a muggle if any pthread operation or integrity check fails, MAGIC otherwise
  int killed;                 // set either by user code or by the reader yielding without having read from the pipe
  int zombie;                 // not in use but unable to be freed because something points to it
  int yielded;                // set by the thread when its result is finished being computed
  pipe_list pool;             // root neighbors if the pipe is untethered
  pipe_list reader;           // a list of at most one pipe designated to read the result from this one
  pthread_cond_t progress;    // signaled by the reader thread when this one is killed or by any blocker that terminates
  pthread_cond_t termination; // signaled by the thread to indicate termination if it has no reader
  pthread_mutex_t lock;       // secures mutually exclusive access for otherwise non-atomic operations on this structure
  scope_stack scope;          // pipes corresponding to enclosing scopes
  uintptr_t depth;            // number of enclosing scopes to the drain at the time this source was created
  int placeholder;            // set for the unmanaged thread at the root of a tree of pipes
  void *result;               // returned by user code in the thread when it terminates
  int status;                 // an error code returned by user code if not overridden by other conditions
};

// --------------- memory management -----------------------------------------------------------------------

// initialize static storage
extern int
_nthm_open_pipes (int *err);

// report memory leaks
extern void
_nthm_close_pipes (void);

// allocate and initialize a pipe
extern nthm_pipe
_nthm_new_pipe (int *err);

// tear down a pipe that has no drain, no enclosing scopes, and no blockers or finishers
extern int
_nthm_retired (nthm_pipe p, int *err);

// --------------- interrogation ---------------------------------------------------------------------------

// return non-zero if the source has yielded or been indirectly killed
extern int
_nthm_heritably_killed_or_yielded (nthm_pipe source, int *err);

// return the truncation level if the source has been truncated indrectly
extern unsigned
_nthm_heritably_truncated (nthm_pipe source, int *err);

// safely test whether a pipe is ready to be retired
extern int
_nthm_retirable (nthm_pipe p, int *err);
