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

timeTuple moves_in_time(int increment, int remaining, int movestogo, int root_ply){
    int move_num = (root_ply + 1) / 2;
    int average_time = remaining / movestogo;
    int extra = average_time * std::max(30 - move_num, 0) / 200;
    int spend = average_time + extra + increment;
    int max_usage = std::min(spend * 6, (remaining - move_overhead) / 4);

    if (max_usage < increment) {
        max_usage = remaining > move_overhead ? remaining - move_overhead : remaining - 1;
    }

    return {spend, max_usage};
}

timeTuple no_movestogo(int increment, int remaining, int root_ply) {
    int min_to_go = increment == 0 ? 10 : 3;
    int move_num = (root_ply + 1) / 2;

    int extra_movestogo = increment == 0 ? 10 : 0;
    int movestogo = std::max(50 + extra_movestogo - 4 * move_num / 5 , min_to_go);
    int average_time = remaining / movestogo;
    int extra = average_time * std::max(30 - move_num, 0) / 200;
    int spend = average_time + extra + increment;
    int max_usage = std::min(spend * 6, (remaining - move_overhead) / 4);

    if (max_usage < increment) {
        max_usage = remaining > move_overhead ? remaining - move_overhead : remaining - 1;
    }

    return {spend, max_usage};
}

timeTuple get_myremain(int increment, int remaining, int movestogo, int root_ply){
    if (movestogo == 1) {
        return {remaining - move_overhead, remaining - move_overhead};
    } else if (movestogo == 0) {
        return no_movestogo(increment, remaining, root_ply);
    } else {
        return moves_in_time(increment, remaining, movestogo, root_ply);
    }
}

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
