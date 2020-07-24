#!/usr/bin/env bash
rm -rf INSTALL compile aclocal.m4 config.guess config.h.in* config.sub configure depcomp install-sh install.sh
rm -rf ltmain.sh missing autom4te.cache
find . -name Makefile -delete
find . -name Makefile.in -delete
find . -name \*.la -delete
autoreconf -v -i -f
