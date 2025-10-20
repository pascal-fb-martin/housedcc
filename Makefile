# HouseTrain - a model train control service
#
# Copyright 2025, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=housedcc
HCAT=train

# Application build. --------------------------------------------

OBJS= housedcc_pidcc.o \
      housedcc_consist.o \
      housedcc_fleet.o \
      housedcc.o
LIBOJS=

all: housedcc

clean:
	rm -rf build
	rm -f *.o *.a housedcc

rebuild: clean all

%.o: %.c
	gcc -c -Wall -g -Os -o $@ $<

housedcc: $(OBJS)
	gcc -g -O -o housedcc $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

# Distribution agnostic file installation -----------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/dcc
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/dcc

install-runtime: install-preamble
	$(INSTALL) -m 0755 -s housedcc $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housedcc

install-app: install-ui install-runtime

uninstall-app:
	rm -rf $(DESTDIR)$(SHARE)/public/dcc
	rm -f $(DESTDIR)$(prefix)/bin/housedcc

purge-app:

purge-config:
	rm -f $(DESTDIR)/etc/default/housedcc

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package: debian-package-generic

# System installation. ------------------------------------------

include $(SHARE)/install.mak

