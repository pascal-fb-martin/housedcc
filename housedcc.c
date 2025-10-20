/* HouseDCC - a simple web server to control DCC-equiped model trains.
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
 * housedcc.c - Main loop of the HouseDCC program.
 *
 * SYNOPSYS:
 *
 *   housedcc [-group=NAME]
 *
 * The group name is used to identify the model train layout.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_static.h"

#include "houseportalclient.h"
#include "houselog.h"
#include "housecapture.h"
#include "houseconfig.h"
#include "housediscover.h"
#include "housedepositor.h"
#include "housedepositorstate.h"

#include "housedcc_pidcc.h"
#include "housedcc_fleet.h"
#include "housedcc_consist.h"

#define DEBUG if (echttp_isdebug()) printf

static int use_houseportal = 0;

static char JsonBuffer[65537];

static long DccLatest = 0; // Detect any config or status change.


static void dcc_initial (void) {
    if (!DccLatest) { 
       // The initial value needs to be somewhat random,
       // so that the clients can detect a restart.
       //
       DccLatest = (long)(time(0) & 0xffff) * 100;
    }
}

static void dcc_changed (void) {

    dcc_initial();
    DccLatest += 1;
}

static int dcc_same (void) {

    dcc_initial();

    // The 'known' parameter is used for conditional "update" polls,
    // as a way to detect changes.
    //
    const char *knownpar = echttp_parameter_get("known");
    if (knownpar && (atol(knownpar) == DccLatest)) {
        echttp_error (304, "Not Modified");
        return 1; // Same as what alreay known.
    }
    return 0; // Not the same as what was known already.
}

static int dcc_header (char *buffer, int size) {

    int cursor;
    cursor = snprintf (buffer, size,
                       "{\"host\":\"%s\",\"timestamp\":%lld"
                           ",\"trains\":{\"layout\":\"%s\",\"latest\":%ld",
                       houselog_host(),
                       (long long)time(0),
                       housedepositor_group(),
                       DccLatest);
    return cursor;
}

static int dcc_export (void) {

    int c = dcc_header (JsonBuffer, sizeof(JsonBuffer));

    c += housedcc_pidcc_export (JsonBuffer+c, sizeof(JsonBuffer)-c, ",");
    c += housedcc_fleet_export (JsonBuffer+c, sizeof(JsonBuffer)-c, ",");
    c += housedcc_consist_export (JsonBuffer+c, sizeof(JsonBuffer)-c, ",");
    c += snprintf (JsonBuffer+c, sizeof(JsonBuffer)-c, "}}");
    return c;
}

static const char *dcc_save (void) {

    dcc_changed();

    int length = dcc_export ();

    houseconfig_update (JsonBuffer);
    housedepositor_put ("config", houseconfig_name(), JsonBuffer, length);

    echttp_content_type_json ();
    return JsonBuffer;
}

static const char *dcc_status (const char *method, const char *uri,
                               const char *data, int length) {

    if (dcc_same()) return "";

    int cursor = dcc_header (JsonBuffer, sizeof(JsonBuffer));

    cursor += housedcc_fleet_status (JsonBuffer+cursor, sizeof(JsonBuffer)-cursor);
    cursor += housedcc_consist_status (JsonBuffer+cursor, sizeof(JsonBuffer)-cursor);
    cursor += snprintf (JsonBuffer+cursor, sizeof(JsonBuffer)-cursor, "}}");

    echttp_content_type_json ();
    return JsonBuffer;
}

static const char *dcc_move (const char *method, const char *uri,
                             const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    const char *speed = echttp_parameter_get("speed");

    if (!id) {
        echttp_error (404, "missing device ID");
        return "";
    }
    if (!speed) {
        echttp_error (400, "missing speed value");
        return "";
    }
    int speedvalue = atoi (speed);

    if (isdigit(id[0])) {
       if (! housedcc_pidcc_move (atoi(id), speedvalue)) {
            echttp_error (500, "DCC failure");
            return "";
       }
    } else if (! housedcc_consist_move (id, speedvalue)) {
        if (! housedcc_fleet_move (id, speedvalue)) {
            echttp_error (404, "invalid ID");
            return "";
        }
    }
    dcc_changed();
    return dcc_status (method, uri, data, length);
}

static const char *dcc_stop (const char *method, const char *uri,
                             const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    const char *urgent = echttp_parameter_get("urgent");

    int emergency = urgent?atoi(urgent):0;

    if (!id) {
        if (! housedcc_pidcc_stop (0, emergency)) {
            echttp_error (500, "DCC failure");
            return "";
        }
        housedcc_fleet_stopped ();
        housedcc_consist_stopped ();

    } else if (isdigit(id[0])) {

       if (! housedcc_pidcc_stop (atoi(id), emergency)) {
            echttp_error (500, "DCC failure");
            return "";
       }

    } else if (! housedcc_consist_stop (id, emergency)) {

        if (! housedcc_fleet_stop (id, emergency)) {
            echttp_error (404, "invalid ID");
            return "";
        }
    }
    dcc_changed();
    return dcc_status (method, uri, data, length);
}

static const char *dcc_set (const char *method, const char *uri,
                            const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    const char *device = echttp_parameter_get("device");
    const char *state = echttp_parameter_get("state");

    if (!id) {
        echttp_error (404, "missing vehicle ID");
        return "";
    }
    if (!device) {
        echttp_error (400, "missing device");
        return "";
    }
    if (!state) {
        echttp_error (400, "missing state value");
        return "";
    }

    if (isdigit(id[0])) {
       if (! housedcc_pidcc_function (atoi(id), atoi(state))) {
            echttp_error (500, "DCC failure");
            return "";
       }
    } else {
       int statevalue;
       if (!strcmp(state, "on")) {
           statevalue = 1;
       } else if (!strcmp(state, "off")) {
           statevalue = 0;
       } else {
           echttp_error (400, "invalid state");
           return "";
       }

       if (! housedcc_fleet_set (id, device, statevalue)) {
          echttp_error (404, "invalid ID");
          return "";
       }
    }
    dcc_changed();
    return dcc_status (method, uri, data, length);
}

static const char *dcc_gpio (const char *method, const char *uri,
                             const char *data, int length) {

    const char *a = echttp_parameter_get("a");
    const char *b = echttp_parameter_get("b");

    if (!a) {
        echttp_error (404, "missing pin A");
        return "";
    }
    housedcc_pidcc_config (atoi(a), b?atoi(b):0);
    return dcc_save ();
}

static const char *dcc_addModel (const char *method, const char *uri,
                                 const char *data, int length) {

    const char *model = echttp_parameter_get("model");
    const char *type = echttp_parameter_get("type");
    const char *dev = echttp_parameter_get("devices");

    if ((!model) || (!type)) {
        echttp_error (404, "missing model name or type");
        return "";
    }

    int count = 0;
    const char *accessories[16];

    if (dev && (*dev > 0)) {

       char localcopy[512];
       snprintf (localcopy, sizeof(localcopy), "%s", dev);

       char *cursor = localcopy;
       accessories[count++] = cursor;
       while (*(++cursor) > 0) {
          if (*cursor == '+') {
             *cursor  = 0;
             accessories[count++] = cursor + 1;
          }
          if (count >= 16) break; // Avoid overflow.
       }
    }
    housedcc_fleet_declare (model, type, count, accessories);
    return dcc_save ();
}

static const char *dcc_addVehicle (const char *method, const char *uri,
                                   const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    const char *model = echttp_parameter_get("model");
    const char *adr = echttp_parameter_get("adr");

    if ((!id) || (!adr)) {
        echttp_error (404, "missing vehicle ID or address");
        return "";
    }
    housedcc_fleet_add (id, model, atoi(adr));
    return dcc_save ();
}

static const char *dcc_addConsist (const char *method, const char *uri,
                                   const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    const char *adr = echttp_parameter_get("adr");

    if ((!id) || (!adr)) {
        echttp_error (404, "missing consist ID or address");
        return "";
    }
    housedcc_consist_add (id, atoi(adr));
    return dcc_save ();
}

static const char *dcc_assign (const char *method, const char *uri,
                               const char *data, int length) {

    const char *loco = echttp_parameter_get("loco");
    const char *consist = echttp_parameter_get("consist");
    const char *modestring = echttp_parameter_get("mode");

    if ((!loco) || (!consist) || (!modestring)) {
        echttp_error (404, "missing consist information");
        return "";
    }
    housedcc_consist_assign (consist, loco, modestring[0]);
    return dcc_save ();
}

static const char *dcc_remove (const char *method, const char *uri,
                               const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    if (!id) {
        echttp_error (400, "missing id");
        return "";
    }
    housedcc_consist_remove (id);

    return dcc_save ();
}

static const char *dcc_deleteVehicle (const char *method, const char *uri,
                                      const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    if (!id) {
        echttp_error (400, "missing id");
        return "";
    }
    housedcc_fleet_delete (id);
    return dcc_save ();
}

static const char *dcc_deleteConsist (const char *method, const char *uri,
                                      const char *data, int length) {

    const char *id = echttp_parameter_get("id");
    if (!id) {
        echttp_error (400, "missing id");
        return "";
    }
    housedcc_consist_delete (id);
    return dcc_save ();
}

static const char *dcc_config (const char *method, const char *uri,
                               const char *data, int length) {

    if (dcc_same()) return "";

    dcc_export ();
    echttp_content_type_json ();
    return JsonBuffer;
}

static void dcc_background (int fd, int mode) {

    static time_t LastRenewal = 0;
    time_t now = time(0);

    if (use_houseportal) {
        static const char *path[] = {"train:/dcc"};
        if (now >= LastRenewal + 60) {
            if (LastRenewal > 0)
                houseportal_renew();
            else
                houseportal_register (echttp_port(4), path, 1);
            LastRenewal = now;
        }
    }
    housedcc_fleet_periodic(now);
    housediscover (now);
    houselog_background (now);
    housedepositor_periodic (now);
    housedepositor_state_background (now);
    housecapture_background (now);
}

static void dcc_config_listener (const char *name, time_t timestamp,
                                 const char *data, int length) {

    houselog_event ("SYSTEM", "CONFIG", "LOAD", "FROM DEPOT %s", name);
    const char *error = houseconfig_update (data);
    if (error) {
        DEBUG ("Invalid config: %s\n", error);
        return;
    }
    housedcc_pidcc_reload ();
    housedcc_fleet_reload ();
    housedcc_consist_reload ();
}

static void dcc_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    const char *error;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        houseportal_initialize (argc, argv);
        use_houseportal = 1;
    }
    houselog_initialize ("dcc", argc, argv);

    houseconfig_default ("--config=dcc");
    error = houseconfig_load (argc, argv);
    if (error) goto fatal;
    error = housedcc_pidcc_initialize (argc, argv);
    if (error) goto fatal;
    error = housedcc_fleet_initialize (argc, argv);
    if (error) goto fatal;
    error = housedcc_consist_initialize (argc, argv);
    if (error) goto fatal;

    echttp_cors_allow_method("GET");
    echttp_protect (0, dcc_protect);

    echttp_route_uri ("/dcc/gpio", dcc_gpio);

    echttp_route_uri ("/dcc/fleet/status", dcc_status);
    echttp_route_uri ("/dcc/fleet/move",   dcc_move);
    echttp_route_uri ("/dcc/fleet/set",    dcc_set);
    echttp_route_uri ("/dcc/fleet/stop",   dcc_stop);
    echttp_route_uri ("/dcc/fleet/vehicle/model",    dcc_addModel);
    echttp_route_uri ("/dcc/fleet/vehicle/add",    dcc_addVehicle);
    echttp_route_uri ("/dcc/fleet/vehicle/delete", dcc_deleteVehicle);
    echttp_route_uri ("/dcc/fleet/consist/add", dcc_addConsist);
    echttp_route_uri ("/dcc/fleet/consist/assign", dcc_assign);
    echttp_route_uri ("/dcc/fleet/consist/remove", dcc_remove);
    echttp_route_uri ("/dcc/fleet/consist/delete", dcc_deleteConsist);
    echttp_route_uri ("/dcc/fleet/config", dcc_config);

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&dcc_background);
    housediscover_initialize (argc, argv);
    housedepositor_initialize (argc, argv);
    housedepositor_state_load ("dcc", argc, argv);
    housedepositor_state_share (1);
    housecapture_initialize ("/dcc", argc, argv);

    housedepositor_subscribe ("config", houseconfig_name(), dcc_config_listener);

    houselog_event ("SERVICE", "dcc", "STARTED", "ON %s", houselog_host());
    echttp_loop();
    exit(0);

fatal:
    houselog_trace (HOUSE_FAILURE, "DCC", "Cannot initialize: %s\n", error);
    exit(1);
}

