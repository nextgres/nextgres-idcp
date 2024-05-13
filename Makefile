MODULE_big = nextgres_idcp

EXTENSION = nextgres_idcp
DATA = sql/nextgres_idcp--0.1.0.sql
PGFILEDESC = "nextgres_idcp - in-database connection pool"

OBJS = \
  src/backend/libpq/pqcomm.o \
  src/backend/port/socket.o \
  src/backend/postmaster/controller.o \
  src/backend/postmaster/postmaster.o \
  src/backend/postmaster/proxy.o \
  src/backend/utils/init/globals.o \
  src/backend/utils/misc/guc.o \
  src/extension/entrypoint.o

REGRESS = nextgres_idcp

# enable our module in shared_preload_libraries for dynamic bgworkers
REGRESS_OPTS = --temp-config $(srcdir)/st/modules/nextgres_idcp/dynamic.conf

# Disable installcheck to ensure we cover dynamic bgworkers.
NO_INSTALLCHECK = 1

PG_CPPFLAGS += -I$(includedir) -I$(srcdir)/src/include

PG_CPPFLAGS += -DNEXTGRES_EMBEDDED_LIBRARY
PG_LDFLAGS += -lssl -lpq

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = src/test/modules/nextgres_idcp
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
