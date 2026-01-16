/* HouseDCC - A model train control service
 *
 * Copyright 2025, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housedcc_consist.c - Control consists.
 *
 * SYNOPSYS:
 *
 * A consist is the list of vehicles that are linked together to constitute
 * a train.
 *
 * This module handles the control of train consists:
 * - Maintain each consist's properties (address).
 * - Maintain the list of vehicles that are assigned to this consist.
 * - Move whole consist forward, backward and stop.
 *
 * void housedcc_consist_add (const char *ID, int address);
 *
 *    Declare a new empty consist. The address is the DCC consist address
 *    that will be set for each locomotive assigned to this consist.
 *
 * void housedcc_consist_delete (const char *ID);
 *
 *    Delete a declared consist. This remove all
 *
 * void housedcc_consist_assign (const char *consist,
 *                               const char *vehicle, char mode);
 *
 *    Assign the specified vehicle to the named consist. Multiple
 *    locomotives can be assigned to the same consist, but each locomotive
 *    can only be assigned to at most one consist. Mode is one of:
 *       'f': pulls forward when the train moves forward.
 *       'r': push in reverse when the train moves forward.
 *       'i': this vehicle does not provide any traction power.
 *       'd': this vehicle does not even have a DCC decoder.
 *
 *    A consist ID must not conflict with a locomotive ID.
 *
 * void housedcc_consist_remove (const char *vehicle);
 *
 *    Remove the specified vehicle from its current consist, if any.
 *    A consist is deleted when its last vehicle has been removed.
 *
 * int housedcc_consist_export (char *buffer, int size, const char *prefix);
 *
 *    Export the list of declared consists in JSON format. This is used
 *    to save the HouseDcc configuration.
 *
 * const char *housedcc_consist_reload (void);
 *
 *    Reload the list of consists from a saved configuration.
 *
 * int housedcc_consist_move (const char *id, int speed);
 *
 *    Control a consist's or locomotive's movements.
 *
 *    A positive speed means forward movement, a negative speed means reverse
 *    movement, while a speed in the range [-1, 1] means stop.
 *
 *    Movement is authorized if:
 *    - The ID denotes an existing consist.
 *    - The ID denotes a known locomotive that is part of a consist. In that
 *      case the command applies to the consist.
 *
 *    This function returns 1 if the ID exists (locomotive or consist),
 *    0 otherwise.
 *
 * int housedcc_consist_stop (const char *id, int emergency);
 *
 *    Step the designed consist. If emergency is true, cut power immediately.
 *
 * void housedcc_consist_stopped (void);
 *
 *    Tell this module that all vehicles were stopped (DCC STOP ALL)
 *
 * void housedcc_consist_periodic (time_t now);
 *
 *    The periodic function that maintain information about locomotives.
 *
 * int housedcc_consist_status (char *buffer, int size);
 *
 *    A function that populates a complete status in JSON.
 *
 * const char *housedcc_consist_initialize (int argc, const char **argv);
 *
 *    Initialize this module. Return 0 on success, an error text otherwise.
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_json.h>
#include <echttp_encoding.h>

#include "houselog.h"
#include "housediscover.h"

#include "housedcc_consist.h"

#define DEBUG if (echttp_isdebug()) printf

void housedcc_consist_add (const char *ID, int address) {
    // TBD
}

void housedcc_consist_delete (const char *ID) {
    // TBD
}

void housedcc_consist_assign (const char *consist,
                              const char *loco, char mode) {
    // TBD
}

void housedcc_consist_remove (const char *loco) {
    // TBD
}

int housedcc_consist_export (char *buffer, int size, const char *prefix) {
    // TBD
    return 0;
}

const char *housedcc_consist_reload (void) {
    // TBD
    return 0;
}

int housedcc_consist_move (const char *id, int speed) {
    // TBD
    return 0;
}

int housedcc_consist_stop (const char *id, int emergency) {
    // TBD
    return 0;
}

void housedcc_consist_stopped (void) {
    // TBD
}

void housedcc_consist_periodic (time_t now) {
    // TBD
}

int housedcc_consist_status (char *buffer, int size) {
    // TBD
    return 0;
}

const char *housedcc_consist_initialize (int argc, const char **argv) {
    // TBD
    return 0;
}

