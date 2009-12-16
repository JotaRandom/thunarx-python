#!/bin/sh
#
# Copyright (c) 2002-2006
#         The Thunar development team. All rights reserved.
#
# Written for Thunar by Benedikt Meurer <benny@xfce.org>.
#

(type xdt-autogen) >/dev/null 2>&1 || {
  cat >&2 <<EOF
autogen.sh: You don't seem to have the Xfce development tools installed on
            your system, which are required to build this software.
            Please install the xfce4-dev-tools package first, it is available
            from http://www.xfce.org/.
EOF
  exit 1
}

ACLOCAL_AMFLAGS="-I m4"

XDT_AUTOGEN_REQUIRED_VERSION="4.7.0" exec xdt-autogen $@

# vi:set ts=2 sw=2 et ai:
