#
#  Copyright (C) 2002 - 2007 Tomasz Kojm <tkojm@clamav.net>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
#  MA 02110-1301, USA.

if BUILD_CLAMD

sbin_PROGRAMS = clamd

clamd_SOURCES = \
    $(top_srcdir)/shared/output.c \
    $(top_srcdir)/shared/output.h \
    $(top_srcdir)/shared/cfgparser.c \
    $(top_srcdir)/shared/cfgparser.h \
    $(top_srcdir)/shared/getopt.c \
    $(top_srcdir)/shared/getopt.h \
    $(top_srcdir)/shared/misc.c \
    $(top_srcdir)/shared/misc.h \
    $(top_srcdir)/shared/network.c \
    $(top_srcdir)/shared/network.h \
    $(top_srcdir)/shared/options.c \
    $(top_srcdir)/shared/options.h \
    clamd.c \
    tcpserver.c \
    tcpserver.h \
    localserver.c \
    localserver.h \
    session.c \
    session.h \
    thrmgr.c \
    thrmgr.h \
    server-th.c \
    server.h \
    scanner.c \
    scanner.h \
    others.c \
    others.h \
    clamuko.c \
    clamuko.h \
    dazukoio_compat12.c \
    dazukoio_compat12.h \
    dazukoio.c \
    dazukoio.h \
    dazuko_xp.h \
    dazukoio_xp.h \
    shared.h

endif

LIBS = $(top_builddir)/libclamav/libclamav.la @CLAMD_LIBS@ @THREAD_LIBS@
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/shared -I$(top_srcdir)/libclamav
