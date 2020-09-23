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

#ifndef __OLDTUNE__
int num_threads = 1;
#else
int num_threads = 7;
#endif

void get_ready() {

    main_thread.rootheight = main_thread.position.historyIndex;

    for (int i = 1; i < num_threads; ++i) {
        SearchThread *t = (SearchThread*)get_thread(i);
        t->rootheight = main_thread.rootheight;
        memcpy(&t->position, &main_thread.position, sizeof(Position));
        t->position.my_thread = t;
    }

    for (int i = 0; i < num_threads; i++)
    {
        SearchThread *t = (SearchThread*)get_thread(i);
        t->doNMP = true;

        for (int j = 0; j < MAX_PLY + 2; ++j) {
            searchInfo *info = &t->ss[j];
            info->pv[0] = MOVE_NONE;
            info->pvLen = 0;
            info->ply = 0;
            info->hadSingularExtension = false;
            info->chosenMove = MOVE_NONE;
            info->excludedMove = MOVE_NONE;
            info->staticEval = UNDEFINED;
            info->killers[0] = info->killers[1] = MOVE_NONE;
            info->counterMove_history = &t->counterMove_history[BLANK][0];
        }
        memset(&t->pawntable, 0 , sizeof(t->pawntable));
    }
}

void clear_threads() {
    for (int i = 0; i < num_threads; ++i) {
        SearchThread *search_thread = (SearchThread*)get_thread(i);

        // Clear history
        std::memset(&search_thread->historyTable, 0, sizeof(search_thread->historyTable));
        //initialize history
        for (int j = 0; j < 14; ++j) {
            for (int k = 0; k < 64; ++k) {
                for (int l = 0; l < 14; ++l) {
                    for (int m = 0; m < 64; ++m) {
                        search_thread->counterMove_history[j][k][l][m] = j < 2 ? -1 : 0;
                    }
                }
            }
        }

        // Clear counter moves
        for (int j = 0; j < 14; ++j) {
            for (int k = 0; k < 64; ++k) {
                search_thread->counterMoveTable[j][k] = MOVE_NONE;
            }
        }
    }
}

void reset_threads(int thread_num) {
    num_threads = thread_num;
    delete[] search_threads;
    search_threads = new SearchThread[num_threads - 1];

    for (int i = 1; i < thread_num; ++i) {
        ((SearchThread*)get_thread(i))->thread_id = i;
    }
    clear_threads();
    get_ready();
}

void init_threads() {
    search_threads = new SearchThread[num_threads - 1];

    for (int i = 0; i < num_threads; ++i) {
        ((SearchThread*)get_thread(i))->thread_id = i;
    }
    clear_threads();
}
