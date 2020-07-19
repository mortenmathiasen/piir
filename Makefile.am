AUTOMAKE_OPTIONS = subdir-objects

AM_LDFLAGS = -lpigpio

bin_PROGRAMS =  piir

piir_SOURCES = \
	src/hvac_panasonic_jke.h \
	src/hvac_panasonic_jke.c \
	src/symboldefinition.h \
	src/irslinger.h \
	src/irslinger.c \
	src/log.h \
	src/log.c \
	src/config.h \
	src/config.c \
	src/parson.h \
	src/parson.c

piirdir = $(datadir)/piir
piir_DATA = \
	conf/remotes/hvac/panasonic_jke.json




