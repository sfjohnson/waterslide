# Copyright 2023 Sam Johnson
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

ERROR_MESSAGE := This Makefile can be run on x86_64 Linux only

CC = $(TOOLCHAIN)/bin/arm-rpi-linux-gnueabihf-gcc
CPP = $(TOOLCHAIN)/bin/arm-rpi-linux-gnueabihf-g++
PROTOC = bin/protoc
PROTOCFLAGS = --cpp_out=.

CFLAGS = --sysroot=$(TOOLCHAIN)/arm-rpi-linux-gnueabihf/sysroot -D_POSIX_C_SOURCE=200809L -std=c17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck
CPPFLAGS = --sysroot=$(TOOLCHAIN)/arm-rpi-linux-gnueabihf/sysroot -std=c++20 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck
ORIGIN=$ORIGIN
O=$$O
LDFLAGS = -Llib/rpi -pthread
LIBS = -lstdc++ -ldl -latomic -lm -lopus -luwebsockets -lraptorq -lck -lr8brain -lprotobuf-lite -lboringtun -ltinyalsa

TARGET = waterslide-rpi
PROTOBUFS = init-config.proto monitor.proto
SRCSC = main.c audio-linux.c sender.c receiver.c globals.c utils.c mux.c demux.c endpoint.c pcm.c event-recorder.c
SRCSCPP = syncer/enqueue.cpp syncer/resamp-state.cpp syncer/receiver-sync.cpp config.cpp monitor.cpp $(subst .proto,.pb.cpp,$(addprefix protobufs/,$(PROTOBUFS)))
OBJS = $(subst .c,.o,$(addprefix src/,$(SRCSC))) $(subst .cpp,.o,$(addprefix src/,$(SRCSCPP)))

.PHONY: exit setup

all: exit setup protobufs bin/$(TARGET)

protobufs: $(PROTOBUFS)

exit:
ifneq ($(shell uname), Linux)
	$(error $(ERROR_MESSAGE))
endif

setup:
	mkdir -p obj/protobufs obj/syncer include/protobufs src/protobufs

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
