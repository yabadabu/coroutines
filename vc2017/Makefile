SRCS_PATHS = . ../coroutines
SRCS = $(foreach p,$(SRCS_PATHS),$(wildcard $(p)/*.cpp))
OBJS_PATH = objs
OBJS = $(foreach f,$(SRCS),$(OBJS_PATH)/$(basename $(notdir $(f))).o)
CFLAGS = -c -std=c++11 -I.. -O2
LNKFLAGS = -lstdc++ -L../libs/osx -lfcontext

TARGET = test

$(TARGET) : $(OBJS)
	$(CC) -o $@ $(OBJS) $(LNKFLAGS)

$(OBJS_PATH)/%.o : ../coroutines/%.cpp
	mkdir -p $(OBJS_PATH) && $(CC) $(CFLAGS) -o $@ $<

$(OBJS_PATH)/%.o : ./%.cpp
	mkdir -p $(OBJS_PATH) && $(CC) $(CFLAGS) -o $@ $<

run :
	cd ../example && ../osx/server

clean : 
	rm $(OBJS)

$(info OBJS = $(OBJS))
