UNAME := $(shell uname)

ifeq ($(UNAME),Linux)
	TOOLCHAIN = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64
endif
ifeq ($(UNAME),Darwin)
	TOOLCHAIN = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/darwin-x86_64
endif

CC = $(TOOLCHAIN)/bin/aarch64-linux-android30-clang
CPP = $(TOOLCHAIN)/bin/aarch64-linux-android30-clang

CFLAGS = -std=c17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck
CPPFLAGS = -std=c++17 -O3 -fstrict-aliasing -Wno-gnu-anonymous-struct -Wno-nested-anon-types -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck
ORIGIN=$ORIGIN
O=$$O
LDFLAGS = -Llib/android30 -pthread -rpath '$ORIGIN/../lib'
LIBS = -L$(TOOLCHAIN)/sysroot/usr/lib/aarch64-linux-android/30 -lstdc++ -lz -lm -lopus -luwebsockets -lraptorq -lck -lr8brain -lprotobuf-lite -llog -lOpenSLES -loboe

TARGET = waterslide-android30
SRCSC = main.c sender.c receiver.c globals.c stats.c utils.c circ.c slip.c mux.c demux.c endpoint.c monitor.c
SRCSCPP = syncer.cpp init-config.pb.cpp config.cpp audio-android.cpp
OBJS = $(subst .c,.o,$(addprefix src/,$(SRCSC))) $(subst .cpp,.o,$(addprefix src/,$(SRCSCPP)))

.PHONY: directories

all: directories bin/$(TARGET)

directories:
	mkdir -p obj

bin/$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(subst src,obj,$(OBJS)) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $(subst src,obj,$@)

.cpp.o:
	$(CPP) $(CPPFLAGS) -c $< -o $(subst src,obj,$@)

clean:
	rm -f $(subst src,obj,$(OBJS)) bin/$(TARGET)
