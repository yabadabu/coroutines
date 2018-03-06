SRCS_PATHS = . ../coroutines
SRCS = $(foreach p,$(SRCS_PATHS),$(wildcard $(p)/*.cpp))
OBJS_PATH = objs
OBJS = $(foreach f,$(SRCS),$(OBJS_PATH)/$(basename $(notdir $(f))).o)
CFLAGS = -c -std=c++11 -I..
LNKFLAGS = -lstdc++ -lfcontext -lm
CXX = clang

UNAME := $(shell uname -s)
ifeq ($(UNAME),Linux)
	LNKFLAGS += -L../libs/linux
endif
ifeq ($(UNAME),Darwin)
	LNKFLAGS += -L../libs/osx
endif

TARGET = test

$(TARGET) : $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LNKFLAGS)

$(OBJS_PATH)/%.o : ../coroutines/%.cpp
	mkdir -p $(OBJS_PATH) && $(CXX) $(CFLAGS) -o $@ $<

$(OBJS_PATH)/%.o : ./%.cpp
	mkdir -p $(OBJS_PATH) && $(CXX) $(CFLAGS) -o $@ $<

run :
	cd ../example && ../osx/server

clean : 
	rm $(OBJS)

$(info OBJS = $(OBJS))
