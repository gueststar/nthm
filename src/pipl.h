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

// This file declares functions for operating on pipe list data
// structures. These declarations are not part of the public
// API. Integer valued functions return non-zero if successful.

typedef struct pipe_list_struct *pipe_list;   // doubly linked list of pipes

struct pipe_list_struct
{
  nthm_pipe pipe;
  pipe_list complement;       // points to a node in another list whose complement points back to this one
  pipe_list *previous_pipe;   // points to the next_pipe field in its predecessor
  pipe_list next_pipe;
};

// --------------- pipe list construction ------------------------------------------------------------------

// return a newly created unit list containing only the pipe p
extern pipe_list
_nthm_pipe_list_of (nthm_pipe p, int *err);

// allocate and initialize complementary unit pipe lists r of d and b of s
extern int
_nthm_new_complementary_pipe_lists (pipe_list *r, nthm_pipe d, pipe_list *b, nthm_pipe s, int *err);

// concatenate a unit list t with a list b
extern int
_nthm_pushed (pipe_list t, pipe_list *b, int *err);

// append the unit list t to the queue that starts with f and ends with q
extern int
_nthm_enqueued (pipe_list t, pipe_list *f, pipe_list *q, int *err);

// --------------- pipe list demolition --------------------------------------------------------------------

// remove an item from a pipe list without freeing it
extern int
_nthm_severed (pipe_list t, int *err);

// free a unit pipe list and remove the reference to it from its complement, if any
extern int
_nthm_freed (pipe_list r, int *err);

// return the first pipe in a list f and bilaterally delist it
extern nthm_pipe
_nthm_popped (pipe_list *f, int *err);

// remove an item from a pipe list, free it, and remove the reference from its complement
extern nthm_pipe
_nthm_unilaterally_delisted (pipe_list *t, int *err);

// bilaterally delist and return the first pipe in a non-empty queue, or return NULL if the queue is empty
extern nthm_pipe
_nthm_dequeued (pipe_list *f, pipe_list *q, int *err);

// return r->complement->pipe while also delisting both r and its complement, which may be in a queue starting with f
extern nthm_pipe
_nthm_bilaterally_dequeued (pipe_list r, pipe_list *f, pipe_list *q, int *err);

// free a pair of complementary unit pipe lists
extern int
_nthm_bilaterally_freed (pipe_list r, pipe_list b, int *err);

// write errors to stderr
extern void
_nthm_close_pipl (void);
