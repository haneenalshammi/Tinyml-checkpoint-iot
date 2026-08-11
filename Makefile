CONTIKI_PROJECT = finalClient finalServer Fianlintermitentxx
all: $(CONTIKI_PROJECT)
CFLAGS += -std=gnu99
WERROR = 0
MODULES += os/storage/cfs
CFLAGS += -I/senslab/users/roql/iot-lab/parts/iot-lab-contiki-ng/arch/platform/iotlab/openlab
CFLAGS += -DRANDOM_RAND_MAX=0x7fff
CONTIKI=../..
include $(CONTIKI)/Makefile.include
