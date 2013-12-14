/* Utility functions for the QEMU PALcode.

   Copyright (C) 2011 Richard Henderson

   This file is part of QEMU PALcode.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the text
   of the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not see
   <http://www.gnu.org/licenses/>.  */

#include "protos.h"


void
ndelay(unsigned long nsec)
{
  unsigned long target, now;

  /* ??? Fix race between setting an alarm and waiting for an interrupt,
     so that we can use wtint here.  This isn't used much except for 
     during startup, so it probably doesn't matter much.  */

  now = get_wall_time();
  target = now + nsec;

  do
    now = get_wall_time();
  while (now < target);
}
