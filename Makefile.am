AUTOMAKE_OPTIONS = subdir-objects

AM_CFLAGS = -DDATADIR='"$(datadir)/piir"'

LIBS += @LINK_PIGPIO@ @LINK_GPIOD@

bin_PROGRAMS =  piir

piir_SOURCES = \
	src/piir.h \
	src/piir.c \
	src/hvac_panasonic_plugin.h \
	src/hvac_panasonic_plugin.c \
	src/symboldefinition.h \
	src/irgeneric.h \
	src/irslinger.h \
	src/irslinger.c \
	src/transmitter.h \
	src/transmitter.c \
	src/log.h \
	src/log.c \
	src/config.h \
	src/config.c \
	src/parson.h \
	src/parson.c

piir_conf_remotes_dir = $(datadir)/piir/conf/remotes
piir_conf_remotes__DATA = \
	conf/remotes/hvac_panasonic.json





