# Copyright 2023 Sam Johnson
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

ifeq ($(shell uname -m), arm64)
  ARCH = macos-arm64
else
  ARCH = macos11
endif

# Uses Homebrew clang, tested with version 16
LLVM_PATH := $(shell brew --prefix llvm@16)
OPENSSL_PATH := $(shell brew --prefix openssl@3)
CC = $(LLVM_PATH)/bin/clang
CPP = $(LLVM_PATH)/bin/clang
PROTOC = bin/protoc
PROTOCFLAGS = --cpp_out=.
# CFLAGS = -g3 -fno-omit-frame-pointer -fsanitize=address
# LIBS = -g3 -fno-omit-frame-pointer -fsanitize=address
CFLAGS = -std=c17 -O3 -flto -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck -I$(OPENSSL_PATH)/include
CPPFLAGS = -std=c++20 -O3 -flto -fstrict-aliasing -Wno-gnu-anonymous-struct -Wno-nested-anon-types -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck -I$(OPENSSL_PATH)/include
LDFLAGS = -Llib/$(ARCH) -L$(OPENSSL_PATH)/lib -pthread -flto
LIBS = -lstdc++ -lm -lz -lopus -lportaudio -lr8brain -lraptorq -lck -lssl -lcrypto -luwebsockets -lprotobuf-lite -lboringtun -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework CoreServices -framework Security

TARGET = waterslide-$(ARCH)
PROTOBUFS = init-config.proto monitor.proto
SRCSC = main.c sender.c receiver.c globals.c utils.c mux.c demux.c endpoint.c audio-macos.c pcm.c event-recorder.c
SRCSCPP = syncer/enqueue.cpp syncer/resamp-state.cpp syncer/receiver-sync.cpp config.cpp monitor.cpp $(subst .proto,.pb.cpp,$(addprefix protobufs/,$(PROTOBUFS)))
OBJS = $(subst .c,.o,$(addprefix src/,$(SRCSC))) $(subst .cpp,.o,$(addprefix src/,$(SRCSCPP)))

.PHONY: setup

all: setup protobufs bin/$(TARGET)

protobufs: $(PROTOBUFS)

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
