/* houseslights - A simple home web server for lighting control
 *
 * Copyright 2020, Pascal Martin
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
 * housedcc_fleet.h - Manage and control DCC fleet of vehicles.
 *
 */

const char *housedcc_fleet_initialize (int argc, const char **argv);

void housedcc_fleet_declare (const char *model, const char *type,
                               int count, const char *accessory[]);
void housedcc_fleet_add (const char *id, const char *model, int address);
void housedcc_fleet_delete (const char *id);
int  housedcc_fleet_exists (const char *id);
int  housedcc_fleet_move (const char *id, int speed);
int  housedcc_fleet_stop (const char *id, int emergency);
void housedcc_fleet_stopped (void);
int  housedcc_fleet_set (const char *id, const char *name, int state);
void housedcc_fleet_periodic (time_t now);

int  housedcc_fleet_status (char *buffer, int size);

void housedcc_fleet_reload (void);
int housedcc_fleet_export (char *buffer, int size, const char *prefix);
