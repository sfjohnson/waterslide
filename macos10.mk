CC = /usr/local/opt/llvm/bin/clang
CPP = /usr/local/opt/llvm/bin/clang
PROTOC = bin/protoc
PROTOCFLAGS = --cpp_out=.
# CFLAGS = -g3 -fno-omit-frame-pointer -fsanitize=address
# LIBS = -g3 -fno-omit-frame-pointer -fsanitize=address
CFLAGS = -std=c17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck -I/usr/local/Cellar/openssl@3/3.0.0_1/include
CPPFLAGS = -std=c++17 -O3 -fstrict-aliasing -Wno-gnu-anonymous-struct -Wno-nested-anon-types -pedantic -pedantic-errors -Wall -Wextra -I./include -I./include/deps -I./include/deps/ck -I/usr/local/Cellar/openssl@3/3.0.0_1/include
LDFLAGS = -Llib/macos10 -L/usr/local/Cellar/openssl@3/3.0.0_1/lib -pthread
LIBS = -lstdc++ -lm -lz -lopus -lportaudio -lr8brain -lraptorq -lck -lssl -lcrypto -luwebsockets -lprotobuf-lite -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework CoreServices

TARGET = waterslide-macos10
PROTOBUFS = init-config.proto monitor.proto
SRCSC = main.c sender.c receiver.c globals.c utils.c circ.c slip.c mux.c demux.c endpoint.c audio-macos.c
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
