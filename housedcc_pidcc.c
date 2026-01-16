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
 * housedcc_pidcc.c - Interact with a PiDCC subprocess.
 *
 * SYNOPSYS:
 *
 * This module handles sending DCC control requests to the PiDCC subprocess.
 *
 * const char *housedcc_pidcc_initialize (int argc, const char **argv);
 *
 *    Initialize this module. Return 0 on success, an error text otherwise.
 *
 * void housedcc_pidcc_config (int pina, int pinb);
 *
 *    Update the PiDCC configuration, typically on a user action.
 *
 * const char *housedcc_pidcc_reload (void);
 *
 *    Reload the program's configuration, typically on restart or when
 *    detecting a configuration change.
 *
 * int housedcc_pidcc_export (char *buffer, int size, const char *prefix);
 *
 *    Export this module's current configuration to JSON format.
 *
 * int housedcc_pidcc_move (int address, int speed);
 *
 *    Control one locomotive's movements.
 *
 *    A positive speed means forward movement, a negative speed means reverse
 *    movement, while a speed in the range [-1, 1] means stop.
 *
 *    Return <= 0 on error, >= 1 otherwise.
 *
 * int housedcc_pidcc_stop (int address, int emergency);
 *
 *    Order one or all locomotives to stop. And emergency stop is immediate
 *    (e.g. not bound to a deceleration curve). Address 0 is all locomotives.
 *
 *    Return <= 0 on error, >= 1 otherwise.
 *
 * int housedcc_pidcc_function (int address, int instruction);
 *
 *    Control one vehicle's function devices (F0 to F4).
 *
 *    Return <= 0 on error, >= 1 otherwise.
 *
 * int housedcc_pidcc_accessory (int address, int device, int value);
 *
 *    Control one accessory's devices. Typically signals and switches.
 *
 *    Return <= 0 on error, >= 1 otherwise.
 *
 * void housedcc_pidcc_periodic (time_t now);
 *
 *    The periodic function that maintain information about PiDCC.
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include <echttp.h>
#include <echttp_encoding.h>

#include "houselog.h"
#include "housecapture.h"
#include "houseconfig.h"

#include "housedcc_pidcc.h"

#define DEBUG if (echttp_isdebug()) printf

static pid_t PiDccProcess;
static int PiDccTransmit;
static int PiDccListen;

static char PiDccState = 0;
static time_t PiDccStateDeadline = 0;

static const char *PiDccExecutable = "/usr/local/bin/pidcc";

static int PiDccCapture = -1;

static int GpioPinA = 0;
static int GpioPinB = 0;

// PiDCC status receive mechanism.
static char PiDccBuffer[1024];
static int PiDccBufferConsumer = 0;
static int PiDccBufferProducer = 0;

static int housedcc_pidcc_enabled (void) {
    return (GpioPinA > 0) || (GpioPinB > 0);
}

static int housedcc_pidcc_write (const char *text, int length) {

    int submit = housedcc_pidcc_enabled();
    housecapture_record (PiDccCapture, "PIDCC", submit?"WRITE":"BUILT", text);
    if (!submit) return 0; // No configuration.

    char line[256];
    int total = snprintf (line, sizeof(line), "%s\n", text);
    if (write (PiDccTransmit, line, total) <= 0) {
       return 0;
    }
    return 1;
}

void housedcc_pidcc_config (int pina, int pinb) {

    GpioPinA = pina;
    GpioPinB = pinb;

    if (!housedcc_pidcc_enabled()) return; // No configuration.

    // Update the PiDCC configuration.
    char text[256];
    int l = snprintf (text, sizeof(text), "pin %d %d", GpioPinA, GpioPinB);
    housedcc_pidcc_write (text, l);
}

const char *housedcc_pidcc_reload (void) {

    if (! houseconfig_active()) return 0;

    // Retrieve the new configuration from the JSON data structure.
    housedcc_pidcc_config (houseconfig_integer (0, ".trains.gpio[0]"),
                           houseconfig_integer (0, ".trains.gpio[1]"));
    return 0;
}

int housedcc_pidcc_export (char *buffer, int size, const char *prefix) {
    return snprintf (buffer, size,
                     "%s\"gpio\":[%d,%d]", prefix, GpioPinA, GpioPinB);
}

static void housedcc_pidcc_decode (char *line) {

    switch (line[0]) {
    case 0:   return; // Empty line.
    case '#': // PiDCC is idle.
        housecapture_record (PiDccCapture, "PIDCC", "IDLE", line + 2);
        PiDccState = line[0];
        break;
    case '%': // PiDCC is busy.
        housecapture_record (PiDccCapture, "PIDCC", "BUSY", line + 2);
        PiDccState = line[0];
        PiDccStateDeadline = time(0) + 3;
        break;
    case '*': // The PiDCC queue is full.
        housecapture_record (PiDccCapture, "PIDCC", "FULL", line + 2);
        PiDccState = line[0];
        PiDccStateDeadline = time(0) + 3;
        break;
    case '!':
        housecapture_record (PiDccCapture, "PIDCC", "ERROR", line + 2);
        break;
    case '$':
        housecapture_record (PiDccCapture, "PIDCC", "DEBUG", line + 2);
        break;
    }
}

static void housedcc_pidcc_receive (int fd, int mode) {

    int received = read (PiDccListen,
                         PiDccBuffer+PiDccBufferProducer,
                         sizeof(PiDccBuffer)-PiDccBufferProducer-1);

    if (received <= 0) {
        const char *error = strerror(errno);
        DEBUG ("Pipe read error: %s\n", error);
        housecapture_record (PiDccCapture, "PIDCC", "ERROR", "read(): %s", error);
        return;
    }
    PiDccBuffer[PiDccBufferProducer+received] = 0;
    int i = PiDccBufferProducer;
    PiDccBufferProducer += received;

    for (i = PiDccBufferConsumer; i < PiDccBufferProducer; ++i) {
        char c = PiDccBuffer[i];
        if (c == '\n' || c == '\r') {
            if (i <= PiDccBufferConsumer) continue; // Ignore empty line.
            PiDccBuffer[i] = 0;
            housedcc_pidcc_decode (PiDccBuffer+PiDccBufferConsumer);
            PiDccBufferConsumer = i + 1;
            continue;
        }
    }

    if (PiDccBufferConsumer >= PiDccBufferProducer) {

        // Empty buffer.
        PiDccBufferConsumer = PiDccBufferProducer = 0;

    } else if (PiDccBufferProducer >= sizeof(PiDccBuffer) - 128) {

        // Shift all text left to make room for the next data..
        //
        int length = PiDccBufferProducer - PiDccBufferConsumer;
        memmove (PiDccBuffer, PiDccBuffer+PiDccBufferConsumer, length);
        PiDccBufferConsumer = 0;
        PiDccBufferProducer = length;
    }
}

static void housedcc_pidcc_launch (void) {

    int listen_pipe[2];
    int transmit_pipe[2];

    if (pipe (listen_pipe) < 0) return;
    if (pipe (transmit_pipe) < 0) return;

    PiDccProcess = fork();
    if (PiDccProcess < 0) {
        DEBUG ("fork() error: %s\n", strerror (errno));
        houselog_event ("PIDCC", PiDccExecutable, "FAILED",
                        "FORK ERROR %s", strerror(errno));
        return;
    }

    if (PiDccProcess == 0) {
        // This is the child process.
        dup2 (transmit_pipe[0], 0);
        dup2 (listen_pipe[1], 1);
        execlp (PiDccExecutable, "pidcc", (char *)0);
        DEBUG ("exec(%s) error: %s\n", PiDccExecutable, strerror (errno));
        exit(1);
    }

    // This is the parent process.
    houselog_event ("PIDCC", PiDccExecutable, "START", "PID %d", PiDccProcess);
    PiDccTransmit = transmit_pipe[1];
    PiDccListen = listen_pipe[0];
    echttp_listen (PiDccListen, 1, housedcc_pidcc_receive, 1);
}

const char *housedcc_pidcc_initialize (int argc, const char **argv) {

    PiDccCapture = housecapture_register ("PIDCC");
    housedcc_pidcc_launch ();
    return 0; // No error.
}

int housedcc_pidcc_move (int address, int speed) {

    static int speed2cssss[29] = {0,   0x2, 0x12, 0x3, 0x13,  //  0  1  2  3  4
                                  0x4, 0x14, 0x5, 0x15, 0x6,  //  5  6  7  8  9
                                  0x16, 0x7, 0x17, 0x8, 0x18, // 10 11 12 13 14
                                  0x9, 0x19, 0xa, 0x1a, 0xb,  // 15 16 17 18 19
                                  0x1b, 0xc, 0x1c, 0xd, 0x1d, // 20 21 22 23 24
                                  0xe, 0x1e, 0xf, 0x1f};      // 25 26 27 28

    if ((address <= 0) || (address >= 128)) return 0; // Not supported yet.
    if (PiDccState == '*') return 0; // Failed.

    char command[32];
    int dir = (speed > 0) ? 0x20 : 0;
    speed = abs(speed);
    if (speed > 28) return 0; // Over the limit speed.

    int l = snprintf (command, sizeof(command), "send %d %d",
                      address & 0x7f,
                      0x40 + dir + (speed2cssss[speed] & 0x1f));

    return housedcc_pidcc_write (command, l);
}

int housedcc_pidcc_stop (int address, int emergency) {

    if ((address < 0) || (address >= 128)) return 0; // Not supported yet.
    // No state check: a stop is a safety command.

    char command[32];
    int l = snprintf (command, sizeof(command), "send %d %d",
                      address & 0x7f, 0x40 + (emergency?1:0));

    return housedcc_pidcc_write (command, l);
}

int housedcc_pidcc_function (int address, int instruction) {

    if (address >= 128) return 0; // Not supported yet.
    if (PiDccState == '*') return 0; // Failed.

    char command[32];
    int l = snprintf (command, sizeof(command), "send %d %d",
                      address & 0x7f, instruction);
    return housedcc_pidcc_write (command, l);
}

int housedcc_pidcc_accessory (int address, int device, int value) {

    if (address >= 512) return 0;
    if (PiDccState == '*') return 0; // Failed.

    value = value ? 0x08 : 0;
    device &= 0x0f;

    char command[32];
    int l = snprintf (command, sizeof(command), "send %d %d",
                      0x80 + (address & 0x3f),
                      0x80 + (0 ^ ((address & 0x1c0) >> 2)) + value + device);
    return housedcc_pidcc_write (command, l);
}

static int housedcc_pidcc_deceased (void) {

    if (PiDccProcess <= 0) return 1;

    pid_t pid = waitpid (PiDccProcess, 0, WNOHANG);
    if (pid == PiDccProcess) {
        houselog_event ("PIDCC", PiDccExecutable, "DIED", "");
        PiDccProcess = 0;
        if (PiDccTransmit > 0) {
            close (PiDccTransmit);
            PiDccTransmit = 0;
        }
        if (PiDccListen > 0) {
            echttp_forget (PiDccListen);
            close (PiDccListen);
            PiDccListen = 0;
        }
        return 1;
    }
    return 0;
}

void housedcc_pidcc_periodic (time_t now) {

    if (PiDccState == '*') {
        if (PiDccStateDeadline < now) {
            PiDccState = '#'; // Did we miss something?
            housecapture_record (PiDccCapture, "PIDCC", "TIMEOUT", "");
        }
    }
    if (now % 5 == 0) {
        if (housedcc_pidcc_deceased()) {
            housecapture_record (PiDccCapture, "PIDCC", "ERROR", "PiDCC died");
            housedcc_pidcc_launch();
        }
    }
}

