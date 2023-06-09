CC = /usr/bin/clang
CPP = /usr/bin/clang
PROTOC = bin/protoc
PROTOCFLAGS = --cpp_out=.
# CFLAGS = -g3 -fno-omit-frame-pointer -fsanitize=address
# LIBS = -g3 -fno-omit-frame-pointer -fsanitize=address
CFLAGS = -std=c17 -O3 -flto -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck -I/opt/homebrew/opt/openssl@3/include
CPPFLAGS = -std=c++17 -O3 -flto -fstrict-aliasing -Wno-gnu-anonymous-struct -Wno-nested-anon-types -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck -I/opt/homebrew/opt/openssl@3/include
LDFLAGS = -Llib/macos-arm64 -L/opt/homebrew/opt/openssl@3/lib -pthread -flto
LIBS = -lstdc++ -lm -lz -lopus -lportaudio -lr8brain -lraptorq -lck -lssl -lcrypto -luwebsockets -lprotobuf-lite -lboringtun -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework CoreServices -framework Security

TARGET = waterslide-macos-arm64
PROTOBUFS = init-config.proto monitor.proto
SRCSC = main.c sender.c receiver.c globals.c utils.c slip.c mux.c demux.c endpoint-secure.c audio-macos.c pcm.c wsocket.c
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
