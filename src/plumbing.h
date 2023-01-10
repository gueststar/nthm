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

// non-API routines to mutate live trees of pipes, with non-zero
// return values indicating success

// tether a source s to a drain d if it isn't already
extern int
_nthm_tethered (nthm_pipe s, nthm_pipe d, int *err);

// separate a possibly running source from a running drain
extern int
_nthm_untethered (nthm_pipe s, int *err);

// untether all blockers and finishers under a pipe p
extern int
_nthm_descendants_untethered (nthm_pipe p, int *err);

// untether and kill a pipe
extern int
_nthm_killable (nthm_pipe s, int *err);

// kill both the blockers and the finishers to a drain
extern int
_nthm_descendants_killed (nthm_pipe d, int *err);

// retire an untethered unpooled pipe taking note of its error status
extern int
_nthm_acknowledged (nthm_pipe s, int *err);
