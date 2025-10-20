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
 * housedcc_pidcc.h - Interact with a PiDCC subprocess.
 */
const char *housedcc_pidcc_initialize (int argc, const char **argv);
void housedcc_pidcc_config (int pina, int pinb);
void housedcc_pidcc_reload (void);
int housedcc_pidcc_export (char *buffer, int size, const char *prefix);

int housedcc_pidcc_move (int address, int speed);
int housedcc_pidcc_stop (int address, int emergency);
int housedcc_pidcc_function (int address, int instruction);

int housedcc_pidcc_accessory (int address, int device, int value);

void housedcc_pidcc_periodic (time_t now);

