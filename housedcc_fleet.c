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
 * housedcc_fleet.c - Control the DCC fleet of vehicles (locomotives or cars).
 *
 * SYNOPSYS:
 *
 * This module handles the control of individual vehicles:
 * - Maintain a list of all vehicles, their state and properties.
 * - Move a locomotive forward, backward or stop.
 * - Control vehicles functions.
 *
 * Its main purpose is to hide the subjacent DCC system: it converts
 * a locomotive ID to a DCC address, a device ID to a function code and
 * a prototype speed (Km/h or Mph) to a DCC step value.
 *
 * A vehicle ID is a string, up to 10 characters, that should typically start
 * with a mark (up to 4 alphabetical characters) followed by a vehicle number
 * (up to 6 digits). Spaces are not allowed.
 *
 * A locomotive is a vehicle that provides traction power. In that sense, a
 * self powered car (e.g. lightrail unit) is considered a locomotive. A car
 * has no traction power, but may have a DCC decoder for functions. The
 * vehicle model indicates if the unit is a locomotive, a DCC equipped car,
 * or a car with no DCC onboard.
 *
 * This module now offers two API styles to control locomotives:
 *
 * - The standard API hides the DCC conventions: locomotives are identified
 *   by their ID (railroad mark and engine number), speeds are expressed in
 *   prototype speed and functions are identified by name. It does not matter
 *   how the locomotive is actually controlled. If a new standard replaces
 *   DCC, this API will hopefully not be impacted.
 *
 * - the DCC API uses raw DCC conventions: locomotives are identified by
 *   their DCC addresses, speeds are expressed in DCC steps and functions
 *   are identified by their DCC code. This is intended to allow control
 *   from a DCC-aware model train control software.
 *
 * This module maintains its context consistent, regardless of the API used.
 * It should support multiple clients controlling the same locomotives either
 * way.
 *
 * const char *housedcc_fleet_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void housedcc_fleet_declare (const char *model, const char *scale,
 *                              int fcount, const char *functions[],
 *                              int scount, short speeds[]);
 *
 *    Declare a new vehicle model. The default scale is "N".
 *
 *    A function string uses the format <name>:<index>, where index 0
 *    means FL and index 1..12 means F1..F12.
 *
 *    The speed array contains prototype speed values in Km/h or Mph. Value
 *    at index 0 represents DCC speed step 2, the value at index 1 represents
 *    DCC speed step 3, etc.
 *
 * const char *housedcc_fleet_add (const char *id, const char *model, int address);
 *
 *    Declare a new vehicle. This replaces an existing vehicle if the
 *    ID is already in use. It returns 0 on success, an error text otherwise.
 *
 * void housedcc_fleet_delete (const char *id);
 *
 *    Remove a declared vehicle or model. If the same name is used for
 *    a vehicle and a model, the vehicle is deleted.
 *
 * int housedcc_fleet_exists (const char *id);
 *
 *    Return 1 if a locomotive with that ID exists, 0 otherwise.
 *
 * int housedcc_fleet_move (const char *id, int speed);
 * int housedcc_fleet_stop (const char *id, int emergency);
 * int housedcc_fleet_set  (const char *id, const char *name, int state);
 *
 *    Control one locomotive's movements and devices. Typical devices are
 *    locomotive lights, sound, etc. The list of devices depend on the model
 *    of the locomotive.
 *
 *    A positive speed means forward movement, a negative speed means reverse
 *    movement, while a 0 speed means normal stop.
 *
 *    The explicit stop command has an emergency option to cut power
 *    immediately. It is otherwise similar to speed 0.
 *
 *    These functions return 0 on error, 1 on success.
 *
 * void housedcc_fleet_stopped (int emergency);
 *
 *    Tell this module that all vehicle were stopped (DCC STOP ALL).
 *
 * void housedcc_fleet_reload (void);
 *
 *    Reload from a saved configuration.
 *
 * int housedcc_fleet_export (char *buffer, int size, const char *prefix);
 *
 *    export this module's configuration to JSON format.
 *
 * int housedcc_fleet_background (time_t now);
 *
 *    The periodic function that maintain information about locomotives.
 *    This returns 1 if the live state changed, 0 otherwise.
 *
 * int housedcc_fleet_status (char *buffer, int size);
 *
 *    A function that populates a complete status in JSON.
 *
 * const char *housedcc_fleet_dcc_direction (int adr, const char *direction);
 * const char *housedcc_fleet_dcc_speed (int adr, int step);
 * const char *housedcc_fleet_dcc_set (int adr, int function, int state);
 * const char *housedcc_fleet_dcc_stop (int adr, int emergency);
 *
 *    These functions offer a raw DCC bypass, for an eventual control
 *    by DCC aware software. Return null or else an error message.
 *
 *    The direction must be "forward" or "reverse", but "backward",
 *    a positive number or a negative number are also accepted.
 *
 *    A step of 0 (or negative) means stop (negative is emergency stop).
 *
 * NOTE:
 *
 * This module uses a specific naming convention: names starting with
 * "housedcc_vehicle_" are private interfaces, typically the processing
 * that is common to the standard and DCC APIs.
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_json.h>
#include <echttp_encoding.h>
#include "echttp_libc.h"

#include "houselog.h"
#include "houseconfig.h"
#include "housediscover.h"

#include "housedcc_pidcc.h"
#include "housedcc_fleet.h"

#define DEBUG if (echttp_isdebug()) printf

#define FUNCTION_MAX     16
#define SPEED_STEP_MAX   28
#define RESTRICTED_SPEED 15

#define MODEL_SCALE_DEFAULT "N"

typedef struct {
    char name[15]; // Keep names as standard as possible.
    char index;
} DccFunction;

typedef struct {
    char name[15];
    short count;
    DccFunction functions[FUNCTION_MAX];
    short speeds[SPEED_STEP_MAX];
    char scale[4];
} DccModel;

typedef struct {
    char id[15];
    short address;
    short speed;   // 'prototype' speed in Km/h or Mph.
    short step;    // The translation of the 'prototype' speed to DCC step.
    short dccdirection; // 1 (forward) or -1 (backward).
    short functions;
    time_t deadline;
    DccModel *model;
} DccVehicle;

static DccModel *Models = 0;
static int       ModelsCount = 0;
static int       ModelsAllocated = 0;

#define DCC_ADDRESS_LIMIT 128

static DccVehicle Vehicles[DCC_ADDRESS_LIMIT];
static int        VehiclesCount = 0;

static int housedcc_fleet_valid_address (int address) {
    return (address > 0) && (address < DCC_ADDRESS_LIMIT);
}

static int housedcc_fleet_find_model (const char *model) {

    if ((!model) || (!model[0])) return -1;

    int i;
    for (i = 0; i < ModelsCount; ++i) {
        if (!strcmp (model, Models[i].name)) return i;
    }
    DEBUG ("Cannot find model %s\n", model);
    return -1;
}

static int housedcc_fleet_find (const char *id) {

    int i;
    for (i = 0; i < VehiclesCount; ++i) {
        if (!strcmp (id, Vehicles[i].id)) return i;
    }
    DEBUG ("Cannot find vehicle %s\n", id);
    return -1;
}

static int housedcc_fleet_find_address (int address) {

    int i;
    static short Index[DCC_ADDRESS_LIMIT] = {0};

    // Remember the latest finds to use as search accelerator,
    // as long as this accelerator still refers to the same locomotive.
    // This provides some performance boost only if there are more
    // than 3 locomotives: if there are only a handful, linear search
    // is fast enough. The locomotive at index 0 never needs acceleration..

    if (VehiclesCount > 3) {
        if (!housedcc_fleet_valid_address (address)) return -1;
        int i = Index[address];
        if ((i > 0) && (i < VehiclesCount) && (address == Vehicles[i].address))
            return i;
    }

    // No valid accelerator, do a linear search as fallback.
    for (i = 0; i < VehiclesCount; ++i) {
        if (address == Vehicles[i].address) {
            Index[address] = (short)i; // Remember it for the next search.
            return i;
        }
    }
    DEBUG ("Cannot find address %d\n", address);
    return -1;
}

void housedcc_fleet_declare (const char *model, const char *scale,
                             int fcount, const char *functions[],
                             int scount, short speeds[]) {

    int cursor = housedcc_fleet_find_model (model);
    if (cursor < 0) {
        // This is a new model. Try to reuse a deleted spot first.
        DEBUG ("New vehicle model %s\n", model);
        for (cursor = 0; cursor < ModelsCount; ++cursor) {
            if (Models[cursor].name[0] == 0) break;
        }
        if (cursor >= ModelsCount) {
            if (ModelsCount >= ModelsAllocated) {
                ModelsAllocated += 16;
                Models = realloc (Models, ModelsAllocated * sizeof(DccModel));
            }
            cursor = ModelsCount++;
        }
        strtcpy (Models[cursor].name, model, sizeof(Models[0].name));
    }

    if (!scale) scale = MODEL_SCALE_DEFAULT;
    strtcpy (Models[cursor].scale, scale, sizeof(Models[0].scale));

    if (fcount > FUNCTION_MAX) fcount = FUNCTION_MAX;
    Models[cursor].count = fcount;

    int i;
    for (i = 0; i < fcount; ++i) {
        strtcpy (Models[cursor].functions[i].name,
                 functions[i], sizeof(Models[0].functions[0].name));
        Models[cursor].functions[i].index = -1;
        char *sep = strchr (Models[cursor].functions[i].name, ':');
        if (sep) {
            *sep = 0;
            int index = (char)atoi(sep+1);
            if (index == 13) index = 0; // Compatibility with FL = 13.
            if ((index < 0) || (index > 12)) index = -1; // Invalid.
            Models[cursor].functions[i].index = index;
        }
    }
    if (scount > SPEED_STEP_MAX) scount = SPEED_STEP_MAX;
    for (i = 0; i < scount; ++i) Models[cursor].speeds[i] = speeds[i];
    for ( ; i < SPEED_STEP_MAX; ++i) Models[cursor].speeds[i] = 0;

    houselog_event ("MODEL", model, "CREATED", "");
}

void housedcc_fleet_stationary (DccVehicle *vehicle) {
    vehicle->step = 0;
    vehicle->speed = 0;
    vehicle->deadline = 0;
}

const char *housedcc_fleet_add (const char *id, const char *model, int address) {

    if (!housedcc_fleet_valid_address (address)) return "Invalid address";

    int cursor;
    DccModel *thismodel = 0;

    if (model) {
        cursor = housedcc_fleet_find_model (model);
        if (cursor < 0) {
            DEBUG ("Unknown model %s referenced by vehicle %s\n", model, id);
            return "Unknown model";
        }
        thismodel = Models + cursor;
    }

    cursor = housedcc_fleet_find (id);
    int synonym = housedcc_fleet_find_address (address);
    if ((synonym >= 0) && (cursor != synonym) && Vehicles[synonym].id[0]) {
        DEBUG ("Address of %s conflicts with vehicle %s\n",
               id, Vehicles[synonym].id);
        return "Duplicate address"; // No two units with the same address.
    }

    const char *action = "MODIFIED";
    if (cursor < 0) {
        // This is a new vehicle. Try to reuse a deleted spot first.
        DEBUG ("New vehicle %s\n", id);
        action = "CREATED";
        for (cursor = 0; cursor < VehiclesCount; ++cursor) {
            if (Vehicles[cursor].id[0] == 0) break;
        }
        if (cursor >= VehiclesCount) {
            if (VehiclesCount >= DCC_ADDRESS_LIMIT)
                return "too many locomotives"; // Never happen: see synonym
            cursor = VehiclesCount++;
        }
        strtcpy (Vehicles[cursor].id, id, sizeof(Vehicles[0].id));
    }
    Vehicles[cursor].address = (short)address;
    housedcc_fleet_stationary (Vehicles + cursor);
    Vehicles[cursor].functions = 0;
    Vehicles[cursor].model = thismodel;

    houselog_event ("VEHICLE", id,
                    action, "MODEL %s AT ADDRESS %d", model, address);
    return 0;
}

void housedcc_fleet_delete (const char *id) {

    int cursor = housedcc_fleet_find (id);
    if (cursor >= 0) {
        Vehicles[cursor].id[0] = 0;
        Vehicles[cursor].address = 0;
        houselog_event ("VEHICLE", id, "DELETED", "");
        return;
    }
    cursor = housedcc_fleet_find_model (id);
    if (cursor >= 0) {
        Models[cursor].name[0] = 0;
        houselog_event ("MODEL", id, "DELETED", "");
        return;
    }
}

int housedcc_fleet_exists (const char *id) {
    return housedcc_fleet_find (id) >= 0;
}

static const char *housedcc_fleet_direction_name (int dirorspeed) {
    return (dirorspeed >= 0)?"FORWARD":"REVERSE";
}

static int housedcc_vehicle_move (DccVehicle *vehicle) {

    vehicle->deadline = time(0) + 7; // TBD: make it configurable
    return housedcc_pidcc_move (vehicle->address, vehicle->step);
}

static int housedcc_vehicle_stop (DccVehicle *vehicle, int emergency) {

    houselog_event ("VEHICLE", vehicle->id, "STOP",
                    emergency?"EMERGENCY BREAK":"STANDARD BREAK");

    int dir = (vehicle->dccdirection >= 0)? 1 : 0;
    housedcc_fleet_stationary (vehicle);
    return housedcc_pidcc_stop (vehicle->address, emergency, dir);
}

static int housedcc_vehicle_function (DccVehicle *vehicle,
                                      int function, int state) {

    short mask = 1 << function;
    if (state)
       mask = vehicle->functions | mask;
    else
       mask = vehicle->functions & (~mask);

    int instruction;
    if (function <= 4) { // F1 to F4, FL.
       instruction = 0x80; // CCC=100
       instruction += (mask >> 1) & 0xf; // F1 to F4
       instruction += (mask & 1)? 0x10 : 0; // FL

    } else if (function <= 8) { // F5 to F8.
       instruction = 0xb0; // CCC=101, S=1
       instruction += (mask >> 5) & 0xf; // F5 to F8

    } else if (function <= 12) { // F9 to F12.
       instruction = 0xa0; // CCC=101, S=0
       instruction += (mask >> 9) & 0xf; // F9 to F12

    } else {
       return 0; // Invalid auxiliary function index.
    }

    // Remember the current commanded state.
    vehicle->functions = mask;

    return housedcc_pidcc_function (vehicle->address, instruction);
}

const char *housedcc_fleet_dcc_direction (int adr, const char *dir) {

    int cursor = housedcc_fleet_find_address (adr);
    if (cursor < 0) return "unknown locomotive";

    DccVehicle *vehicle = Vehicles + cursor;
    int old = vehicle->dccdirection;

    if (strsame (dir, "forward"))
        vehicle->dccdirection = 1;
    else if (strsame (dir, "reverse"))
        vehicle->dccdirection = -1;
    else if (strsame (dir, "backward"))
        vehicle->dccdirection = -1;
    else {
        int value = atoi(dir);
        if (value != 0) vehicle->dccdirection = (value > 0)?1:-1;
        else return "invalid direction";
    }
    if ((vehicle->step != 0) && (vehicle->dccdirection != old))
        housedcc_vehicle_stop (vehicle, 0);

    return 0;
}

const char *housedcc_fleet_dcc_speed (int adr, int step) {

    if (step <= 0) return housedcc_fleet_dcc_stop (adr, (step < 0));

    int cursor = housedcc_fleet_find_address (adr);
    if (cursor < 0) return "unknown locomotive";

    if (step >= SPEED_STEP_MAX) {
        if (step >= 128) return "invalid step";
        return "unsupported step";
    }

    DccVehicle *vehicle = Vehicles + cursor;

    if (abs(vehicle->step) != step) {
        const char *direction =
            housedcc_fleet_direction_name (vehicle->dccdirection);

        houselog_event ("VEHICLE", vehicle->id,
                        "MOVE", "%s AT DCC STEP %d", direction, step);
        vehicle->step = vehicle->dccdirection * step;

        // Remember an approximate prototype speed.
        // This is used to report a raisonable speed to all clients.
        DccModel *model = vehicle->model;
        int i;
        for (i = step-1; i >= 0; --i) {
           if (model->speeds[i] > 0) {
              vehicle->speed = vehicle->dccdirection * model->speeds[i];
              break;
           }
        }
        // Lower than any configured speed. Report an arbitrary slow speed.
        if (i < 0) vehicle->speed = vehicle->dccdirection * RESTRICTED_SPEED;
    }
    if (housedcc_vehicle_move (vehicle) <= 0) return "DCC error";
    return 0;
}

const char *housedcc_fleet_dcc_stop (int adr, int emergency) {

    int cursor = housedcc_fleet_find_address (adr);
    if (cursor < 0) return "unknown locomotive";

    if (housedcc_vehicle_stop (Vehicles+cursor, emergency) <= 0)
        return "DCC error";
    return 0;
}

const char *housedcc_fleet_dcc_set (int adr, int function, int state) {

    int cursor = housedcc_fleet_find_address (adr);
    if (cursor < 0) return "unknown locomotive";

    DccModel *model = Vehicles[cursor].model;
    if (!model) return "unknown model"; // No known DCC functions

    int index = function;
    if (index == 13) index = 0; // Compatibility with FL = 13.

    int i;
    for (i = 0; i < model->count; ++i) {
        if (model->functions[i].index == index) break;
    }
    if (i >= model->count) return "unknown function";

    houselog_event ("VEHICLE", Vehicles[cursor].id,
                    "SET", "DCC %d (%s) TO %s",
                    function, model->functions[i].name, state?"ON":"OFF");

    if (housedcc_vehicle_function (Vehicles+cursor, index, state) <= 0)
        return "DCC error";
    return 0;
}

int housedcc_fleet_move (const char *id, int speed) {

    int cursor = housedcc_fleet_find (id);
    if (cursor < 0) return 0;

    DccVehicle *vehicle = Vehicles + cursor;
    DccModel *model = vehicle->model;
    if (!model) return 0;

    if (speed != vehicle->speed) {
        // Convert the 'prototype' speed to a DCC step.
        int sign = (speed < 0)?-1:1;
        int absspeed = abs(speed);
        int step = 0;
        int i;
        for (i = 0; i < SPEED_STEP_MAX; ++i) {
           if (model->speeds[i] == absspeed) {
              step = sign * (i + 1);
              break;
           }
        }
        if (step == 0) return 0;

        if (step < -31) step = -31;
        else if (step > 31) step = 31;

        if (vehicle->speed) {
            int existingsign = ((vehicle->speed) < 0)?-1:1;
            if (sign != existingsign) {
               // The locomotive is reversing direction. DCC expects
               // a stop command first.
               int dir = (Vehicles[cursor].speed >= 0)? 1 : 0;
               housedcc_pidcc_stop (vehicle->address, 0, dir);
            }
        }
        const char *direction = housedcc_fleet_direction_name (speed);
        houselog_event ("VEHICLE", vehicle->id,
                        "MOVE", "%s AT %d (DCC STEP %d)",
                        direction, absspeed, abs(step));

        vehicle->speed = speed;
        vehicle->step = step;
        vehicle->dccdirection = (step >= 0)? 1 : -1;
    }
    return housedcc_vehicle_move (vehicle);
}

int housedcc_fleet_stop (const char *id, int emergency) {

    int cursor = housedcc_fleet_find (id);
    if (cursor < 0) return 0;

    return housedcc_vehicle_stop (Vehicles+cursor, emergency);
}

void housedcc_fleet_stopped (int emergency) {
    int i;
    houselog_event ("VEHICLE", "ALL", "STOPPED",
                    emergency?"EMERGENCY BREAK":"STANDARD BREAK");
    for (i = 0; i < VehiclesCount; ++i) {
        if (Vehicles[i].id[0]) housedcc_fleet_stationary (Vehicles + i);
    }
}

int housedcc_fleet_set (const char *id, const char *name, int state) {

    int cursor = housedcc_fleet_find (id);
    if (cursor < 0) return 0;

    DccModel *model = Vehicles[cursor].model;
    if (!model) return 0; // No known DCC functions

    int i;
    for (i = 0; i < model->count; ++i) {
        if (!strcmp (name, model->functions[i].name)) break;
    }
    if (i >= model->count) return 0; // No such functions.

    houselog_event ("VEHICLE", Vehicles[cursor].id,
                    "SET", "%s TO %s", name, state?"ON":"OFF");

    int index = model->functions[i].index;
    if (index < 0) return 0; // Invalid function.

    return housedcc_vehicle_function (Vehicles+cursor, index, state);
}

static int housedcc_fleet_speeds (char *buffer, int size, DccModel *model) {

    int cursor = 0;
    int step = SPEED_STEP_MAX;
    while (--step >= 0) if (model->speeds[step]) break;
    if (step >= 0) {
       const char *prefix = ",\"speeds\":[";
       int j;
       for (j = 0 ; j <= step; ++j) {
          cursor += snprintf (buffer+cursor, size-cursor,
                              "%s%d", prefix, model->speeds[j]);
          if (cursor >= size) goto overflow;
          prefix = ",";
       }
       cursor += snprintf (buffer+cursor, size-cursor, "]");
       if (cursor >= size) goto overflow;
    }
    return cursor;

overflow:
    return 0;
}

int housedcc_fleet_status (char *buffer, int size) {

    if (VehiclesCount <= 0) return 0; // Nothing to list.

    int i;
    int cursor = 0;
    int listed = 0;
    const char *prefix = ",\"vehicles\":[";

    for (i = 0; i < VehiclesCount; ++i) {

        if (!Vehicles[i].id[0]) continue; // Ignore obsolete entries.

        DccModel *model = Vehicles[i].model;
        char modelinfo[72];
        if (model) {
            snprintf (modelinfo, sizeof(modelinfo),
                      ",\"model\":\"%s\"", model->name);
        } else {
            modelinfo[0] = 0;
        }

        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"address\":%d,\"speed\":%d%s",
                            prefix,
                            Vehicles[i].id,
                            Vehicles[i].address,
                            Vehicles[i].speed,
                            modelinfo);
        if (cursor >= size) goto overflow;
        listed += 1;
        prefix = ",";

        if (model && (model->count > 0)) {
           const char *prefix2 = ",\"devices\":{";
           int mask = Vehicles[i].functions;
           int j;
           for (j = 0; j < model->count; ++j) {
              int on = mask & (1 << model->functions[j].index);
              cursor += snprintf (buffer+cursor, size-cursor,
                                  "%s\"%s\":%d",
                                  prefix2,
                                  model->functions[j].name,
                                  on?1:0);
              if (cursor >= size) goto overflow;
              prefix2 = ",";
           }
           cursor += snprintf (buffer+cursor, size-cursor, "}");
           if (cursor >= size) goto overflow;
        }

        if (model) {
            cursor += housedcc_fleet_speeds (buffer+cursor, size-cursor, model);
        }
        cursor += snprintf (buffer+cursor, size-cursor, "}");
        if (cursor >= size) goto overflow;
    }
    if (listed > 0) {
        cursor += snprintf (buffer+cursor, size-cursor, "]");
        if (cursor >= size) goto overflow;
    }
    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    buffer[0] = 0;
    return 0;
}

int housedcc_fleet_background (time_t now) {

    // DCC engines stop moving after a few seconds if the speed command
    // is not repeated. This is a safety feature, to prevent runaway
    // vehicle events.
    // This program does not automatically repeat the speed command:
    // this is up to the top level application to do it, for the same
    // reason that the timeout exists in the first place.
    int i;
    int changed = 0;
    for (i = VehiclesCount-1; i >= 0; --i) {
        DccVehicle *vehicle = Vehicles + i;
        if ((vehicle->deadline > 0) && (vehicle->deadline < now)) {
            if (vehicle->speed != 0)
                houselog_event ("VEHICLE", vehicle->id, "STOP", "ON DEADLINE");
            housedcc_fleet_stationary (vehicle);
            changed = 1;
        }
    }
    return changed;
}

const char *housedcc_fleet_initialize (int argc, const char **argv) {
    // TBD
    return 0;
}

static const char *housedcc_fleet_reload_models (void) {

    int models = houseconfig_array (0, ".trains.models");
    int count = 0;

    if (models >= 0) count = houseconfig_array_length (models);

    if (Models) free (Models);
    ModelsCount = 0;
    ModelsAllocated = count + 16;
    Models = calloc (ModelsAllocated, sizeof(DccModel));

    if (count <= 0) return 0; // Nothing to load.

    int i;
    int *list = calloc (count, sizeof(int));
    count = houseconfig_enumerate (models, list, count);
    for (i = 0; i < count; ++i) {
        int item = list[i];
        if (item <= 0) continue;
        const char *name = houseconfig_string (item, ".name");
        if (!name) continue;

        const char *scale = houseconfig_string (item, ".scale");
        if (!scale) scale = MODEL_SCALE_DEFAULT;

        DccModel *thismodel = Models + ModelsCount++;
        strtcpy (thismodel->name, name, sizeof(Models[0].name));
        strtcpy (thismodel->scale, scale, sizeof(Models[0].scale));
        thismodel->count = 0;

        int devices = houseconfig_array (item, ".devices");
        if (devices <= 0) continue;
        int devcount = houseconfig_array_length (devices);
        if (devcount <= 0) continue;
        if (devcount > FUNCTION_MAX) devcount = FUNCTION_MAX;

        int j;
        int devlist[FUNCTION_MAX];
        devcount = houseconfig_enumerate (devices, devlist, FUNCTION_MAX);
        for (j = 0; j < devcount; ++j) {
           int dev = devlist[j];
           if (dev <= 0) continue;
           const char *devname = houseconfig_string (dev, ".name");
           int index = houseconfig_integer (dev, ".index");
           if (index == 13) index = 0; // Compatibility with FL == 13.
           if ((!devname) || (index < 0) || (index > 12)) continue;

           DccFunction *thisdev =
               thismodel->functions + (thismodel->count)++;
           strtcpy (thisdev->name, devname, sizeof(thisdev->name));
           thisdev->index = index;
        }

        int speeds = houseconfig_array (item, ".speeds");
        if (speeds > 0) {
            int speedcount = houseconfig_array_length (speeds);
            if (speedcount <= 0) continue;
            if (speedcount > SPEED_STEP_MAX) speedcount = SPEED_STEP_MAX;

            int speedlist[SPEED_STEP_MAX];
            speedcount = houseconfig_enumerate (speeds, speedlist, SPEED_STEP_MAX);
            for (j = 0; j < speedcount; ++j) {
               int item = speedlist[j];
               if (item <= 0) continue;
               thismodel->speeds[j] = houseconfig_integer (item, 0);
            }
        } else {
            // Set an arbitrary set of speed steps for compatibility
           for (j = 0; j < 12; ++j) thismodel->speeds[j] = (j+1) * 10;
        }
        for (; j < SPEED_STEP_MAX; ++j) thismodel->speeds[j] = 0;
    }
    free (list);
    return 0;
}

static const char *housedcc_fleet_reload_vehicles (void) {

    int vehicles = houseconfig_array (0, ".trains.vehicles");
    int count = 0;
    if (vehicles >= 0) count = houseconfig_array_length (vehicles);

    VehiclesCount = 0;

    if (count <= 0) return 0; // Nothing to load.

    int i;
    int *list = calloc (count, sizeof(int));
    count = houseconfig_enumerate (vehicles, list, count);
    for (i = 0; i < count; ++i) {
        int item = list[i];
        if (item <= 0) continue;

        const char *id = houseconfig_string (item, ".id");
        const char *model = houseconfig_string (item, ".model");
        if (!id) continue;

        DccVehicle *thisvehicle = Vehicles + VehiclesCount++;
        strtcpy (thisvehicle->id, id, sizeof(thisvehicle->id));
        thisvehicle->model = 0;
        if (model) {
            int m = housedcc_fleet_find_model (model);
            if (m >= 0) thisvehicle->model = Models + m;
        }
        thisvehicle->address = houseconfig_integer(item, ".address");
        thisvehicle->functions = 0; // TBD: save the state?
    }
    free (list);
    return 0;
}

const char *housedcc_fleet_reload (void) {

    if (! houseconfig_active()) return 0;

    const char *error = housedcc_fleet_reload_models();
    if (error) return error;
    return housedcc_fleet_reload_vehicles();
}

int housedcc_fleet_export (char *buffer, int size, const char *prefix) {

    int i;
    int cursor = 0;

    cursor = snprintf (buffer, size, "%s\"models\":[", prefix);

    prefix = "";
    for (i = 0; i < ModelsCount; ++i) {

        DccModel *model = Models + i;

        if (!model->name[0]) continue; // Ignore obsolete entries.

        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"name\":\"%s\",\"scale\":\"%s\"",
                            prefix, model->name, model->scale);

        if (model->count > 0) {
           const char *prefix2 = ",\"devices\":[";
           int j;
           for (j = 0; j < model->count; ++j) {
               cursor += snprintf (buffer+cursor, size-cursor,
                                   "%s{\"name\":\"%s\",\"index\":%d}",
                                   prefix2,
                                   model->functions[j].name,
                                   model->functions[j].index);
               if (cursor >= size) goto overflow;
               prefix2 = ",";
           }
           cursor += snprintf (buffer+cursor, size-cursor, "]");
           if (cursor >= size) goto overflow;
        }

        cursor += housedcc_fleet_speeds (buffer+cursor, size-cursor, model);

        cursor += snprintf (buffer+cursor, size-cursor, "}");
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor, "%s", ",\"vehicles\":[");
    prefix = "";

    for (i = 0; i < VehiclesCount; ++i) {

        if (!Vehicles[i].id[0]) continue; // Ignore obsolete entries.

        DccModel *model = Vehicles[i].model;
        char modelitem[64];
        if (model)
           snprintf (modelitem, sizeof(modelitem),
                     ",\"model\":\"%s\"", model->name);
        else
           modelitem[0] = 0;

        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"address\":%d%s}",
                            prefix,
                            Vehicles[i].id,
                            Vehicles[i].address,
                            modelitem);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    cursor += snprintf (buffer+cursor, size-cursor, "]");
    return cursor;

overflow:
    return 0;
}

