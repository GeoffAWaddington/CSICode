SRC_PATH = ./reaper_csurf_integrator
WDL_PATH = $(SRC_PATH)/WDL
vpath %.c $(WDL_PATH)
vpath %.cpp $(WDL_PATH) $(SRC_PATH) $(WDL_PATH)/swell
vpath %.mm $(WDL_PATH)/swell

OBJS = control_surface_integrator_ui.o control_surface_integrator.o main.o

CFLAGS += -pipe -fvisibility=hidden -fno-math-errno -fPIC -DPIC -Wall -Wtype-limits \
          -Wno-unused-function -Wno-multichar -Wno-unused-result -Wno-sign-compare \
          -Wno-reorder -Wno-array-bounds -Wshadow

CFLAGS += -D_FILE_OFFSET_BITS=64 -DSWELL_PROVIDED_BY_APP

ARCH := $(shell uname -m)
UNAME_S = $(shell uname -s)

ifeq ($(UNAME_S), Darwin)
  APPNAME=reaper_csurf_integrator.dylib
  LINKEXTRA= -framework Cocoa -lobjc
  SWELL_OBJS=swell-modstub.o
  %.o : %.mm
	$(CXX) -ObjC++ $(CXXFLAGS) -c -o $@ $<
else
  APPNAME=reaper_csurf_integrator.so
  LINKEXTRA=-lpthread -ldl
  SWELL_OBJS=swell-modstub-generic.o
endif

default: $(APPNAME)

ifdef DISALLOW_WARNINGS
  CFLAGS += -Werror
endif

ifneq ($(filter arm%,$(ARCH)),)
  CFLAGS += -fsigned-char -marm
endif

ifdef DEBUG
  CFLAGS += -O0 -g -D_DEBUG -DWDL_CHECK_FOR_NON_UTF8_FOPEN
else
  CFLAGS += -O2 -DNDEBUG
endif

CXXFLAGS = $(CFLAGS) -std=c++17

$(OBJS): $(SRC_PATH)/*.h

OBJS += $(SWELL_OBJS)

.PHONY: clean
	
$(APPNAME): $(OBJS)
	$(CXX) -o $@ -shared $(CFLAGS) $(OBJS) $(LINKEXTRA)

clean:
	-rm $(OBJS) $(APPNAME)