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

// non-API operations for managing a pool of root pipes corresponding
// to untethered and unmanaged threads

// insert a pipe to the root pool if it's not retirable
extern int
_nthm_pooled (nthm_pipe d, int *err);

// insert a pipe into the root pool unconditionally
extern int
_nthm_placed (nthm_pipe d, int *err);

// if a pipe is retirable, take it out of the root pool retire it
extern void
_nthm_unpool (nthm_pipe p, int *err);

// take a pipe out of the root pool unconditionally
extern void
_nthm_displace (nthm_pipe p, int * err);

// initialize static storage
extern int
_nthm_open_pool (int *err);

// free the root pipes and other static storage
extern void
_nthm_close_pool (void);
