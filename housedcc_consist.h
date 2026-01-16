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
 * housedcc_consist.h - Control consists.
 */
void housedcc_consist_add (const char *ID, int address);
void housedcc_consist_delete (const char *ID);
void housedcc_consist_assign (const char *consist,
                              const char *loco, char mode);
void housedcc_consist_remove (const char *loco);

const char *housedcc_consist_reload (void);
int  housedcc_consist_export (char *buffer, int size, const char *prefix);

int  housedcc_consist_move (const char *id, int speed);
int  housedcc_consist_stop (const char *id, int emergency);
void housedcc_consist_stopped (void);

void housedcc_consist_periodic (time_t now);
int housedcc_consist_status (char *buffer, int size);
const char *housedcc_consist_initialize (int argc, const char **argv);
