UNAME := $(shell uname)

ifeq ($(UNAME),Linux)
	TOOLCHAIN = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64
endif
ifeq ($(UNAME),Darwin)
	TOOLCHAIN = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/darwin-x86_64
endif

CC = $(TOOLCHAIN)/bin/aarch64-linux-android30-clang
CPP = $(TOOLCHAIN)/bin/aarch64-linux-android30-clang
PROTOC = bin/protoc
PROTOCFLAGS = --cpp_out=.

CFLAGS = -std=c17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck
CPPFLAGS = -std=c++17 -O3 -fstrict-aliasing -Wno-gnu-anonymous-struct -Wno-nested-anon-types -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck
ORIGIN=$ORIGIN
O=$$O
LDFLAGS = -Llib/android30 -pthread -rpath '$ORIGIN/../lib'
LIBS = -L$(TOOLCHAIN)/sysroot/usr/lib/aarch64-linux-android/30 -lstdc++ -lz -lm -lopus -luwebsockets -lraptorq -lck -lr8brain -lprotobuf-lite -lboringtun -llog -lOpenSLES -ltinyalsa

TARGET = waterslide-android30
PROTOBUFS = init-config.proto monitor.proto
SRCSC = main.c audio-android.c sender.c receiver.c globals.c utils.c slip.c mux.c demux.c endpoint-secure.c pcm.c wsocket.c
SRCSCPP = syncer.cpp config.cpp monitor.cpp $(subst .proto,.pb.cpp,$(addprefix protobufs/,$(PROTOBUFS)))
OBJS = $(subst .c,.o,$(addprefix src/,$(SRCSC))) $(subst .cpp,.o,$(addprefix src/,$(SRCSCPP)))

.PHONY: setup

all: setup protobufs bin/$(TARGET)

protobufs: $(PROTOBUFS)

setup:
	mkdir -p obj/protobufs include/protobufs src/protobufs

%.proto:
	$(PROTOC) $(PROTOCFLAGS) protobufs/$@
	mv $(subst .proto,.pb.cc,$(addprefix protobufs/,$@)) $(subst .proto,.pb.cpp,$(addprefix src/protobufs/,$@))
	mv $(subst .proto,.pb.h,$(addprefix protobufs/,$@)) include/protobufs

bin/$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(subst src,obj,$(OBJS)) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $(subst src,obj,$@)

.cpp.o:
	$(CPP) $(CPPFLAGS) -c $< -o $(subst src,obj,$@)

clean:
	rm -f \
		$(subst src,obj,$(OBJS)) \
		$(subst .proto,.pb.cpp,$(addprefix src/protobufs/,$(PROTOBUFS))) \
		$(subst .proto,.pb.h,$(addprefix include/protobufs/,$(PROTOBUFS))) \
		bin/$(TARGET) \
