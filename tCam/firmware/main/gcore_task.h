/*
 * ADC Task
 *
 * Periodically updates operating state measured by gCore.  Detects snap picture button
 * press and shutdown conditions (power button press and critical battery) and notifies
 * the application task.
 *
 * Copyright 2020-2022 Dan Julio
 *
 * This file is part of tCam.
 *
 * tCam is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tCam is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with firecam.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#ifndef GCORE_TASK_H
#define GCORE_TASK_H


//
// gCore Task Constants
//

// Evaluation interval
#define GCORE_TASK_EVAL_MSEC   50


// Update interval (should be a multiple of GCORE_TASK_EVAL_MSEC)
#define GCORE_TASK_SAMPLE_MSEC 150

// gCore Task notifications
#define GCORE_NOTIFY_SHUTDOWN_MASK          0x00000001

#define GCORE_NOTIFY_HALT_ACCESS_MASK       0x00000010
#define GCORE_NOTIFY_RESUME_ACCESS_MASK     0x00000020



//
// gCore Task API
//
void gcore_task();
 
#endif /* GCORE_TASK_H */
