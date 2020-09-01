/*
  Beef is a UCI-compliant chess engine.
  Copyright (C) 2020 Jonathan Tseng.

  Beef is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Beef is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Beef.h"

extern timeInfo globalLimits;

int move_overhead = 100;

timeTuple calculate_time()
{
    int optimaltime;
    int maxtime;

    if (globalLimits.timelimited) // movetime mode
    {
        return {globalLimits.movetime, globalLimits.movetime};
    }

    if (globalLimits.movesToGo == 1) // one move left so use all the time
    {
        return {globalLimits.totalTimeLeft - move_overhead, globalLimits.totalTimeLeft - move_overhead};
    }
    else if (globalLimits.movesToGo == 0) // sudden death
    {
        optimaltime = globalLimits.totalTimeLeft / 50 + globalLimits.increment;
        maxtime = min(6 * optimaltime, globalLimits.totalTimeLeft / 4);
    }
    else // moves in time
    {
        int movestogo = globalLimits.movesToGo;
        optimaltime = globalLimits.totalTimeLeft / (movestogo + 5) + globalLimits.increment;
        maxtime = min(6 * optimaltime, globalLimits.totalTimeLeft / 4);
    }

    optimaltime = min(optimaltime, globalLimits.totalTimeLeft - move_overhead);
    maxtime = min(maxtime, globalLimits.totalTimeLeft - move_overhead);
    return {optimaltime, maxtime};
}
