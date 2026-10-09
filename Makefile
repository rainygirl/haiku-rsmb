CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -pthread
CPPFLAGS += -Isrc -Ideps/libsmb2-4.0.0/include -D_DEFAULT_SOURCE
LDFLAGS += -Llib
NETWORK_LIBS = $(if $(filter Haiku,$(shell uname -s)),-lnetwork -lbsd,)
LDLIBS = -lsmb2 $(NETWORK_LIBS) -lbe -ltracker -pthread
CORE = build/Location.o build/Discovery.o build/Client.o
.PHONY: all clean test network-test install runtime
all: runtime RSMB rsmb RSMBNetwork smb-tool
runtime: lib/libsmb2.so.1
lib/libsmb2.so.1: tools/build-smb-runtime.sh tools/patch-libsmb2.py
	./tools/build-smb-runtime.sh
build/Client.o: | $(if $(filter Haiku,$(shell uname -s)),lib/libsmb2.so.1,)
build:
	mkdir -p build
build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/%.o: tests/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
RSMB: build/Manager.o $(CORE) resources/App.rdef
	$(CXX) $(CXXFLAGS) build/Manager.o $(CORE) $(LDFLAGS) $(LDLIBS) -o $@
	rc -o App.rsrc resources/App.rdef
	xres -o $@ App.rsrc
	mimeset -f $@
smb-tool: build/Tool.o $(CORE)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
core-test: build/CoreTest.o build/Location.o build/Discovery.o
	$(CXX) $(CXXFLAGS) $^ $(NETWORK_LIBS) -o $@
transfer-test: build/TransferTest.o build/Client.o build/Location.o build/Discovery.o
	$(CXX) $(CXXFLAGS) $^ $(NETWORK_LIBS) -o $@
test: core-test transfer-test
	./core-test
	./transfer-test
network-test: core-test
	./core-test --network
install: runtime RSMB rsmb RSMBNetwork
	sh tools/install-volume.sh
clean:
	rm -f RSMB rsmb RSMBNetwork smb-tool core-test transfer-test App.rsrc build/*.o build/*.d
-include $(wildcard build/*.d)

# Tracker volume. Hybrid systems use the app-private modern-ABI host.
VOLUME_FLAGS = -O2 -g -std=c++17 -pthread -fPIC -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -DB_USE_POSITIVE_POSIX_ERRORS -DRSMB_FUSE -Isrc -Isrc/volume -Ideps/libsmb2-4.0.0/include -I/boot/system/develop/headers/userlandfs/fuse
VOLUME_CORE = build/volume-Location.o build/volume-Discovery.o build/volume-Client.o
build/volume-%.o: src/%.cpp | build
	$(CXX) $(VOLUME_FLAGS) -c $< -o $@
build/FileSystem.o: src/volume/FileSystem.cpp src/volume/Settings.h src/volume/Paths.h | build
	$(CXX) $(VOLUME_FLAGS) -c $< -o $@
rsmb: build/FileSystem.o $(VOLUME_CORE)
	$(CXX) -shared -Wl,-soname,_APP_ $^ -Ldeps/userland-runtime -Llib -L/boot/system/lib -luserlandfs_fuse -lsmb2 -lnetwork -lposix_error_mapper -lbe -pthread -o $@
build/Manager.o: src/volume/Manager.cpp src/volume/Settings.h src/volume/Strings.h src/volume/Paths.h | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
# Network preferences runs with the primary compiler ABI, including gcc2 hybrids.
RSMBNetwork: src/volume/NetworkAddOn.cpp src/volume/Strings.h src/volume/Paths.h
	/boot/system/bin/g++ -O2 -fPIC -shared -I/boot/system/develop/headers/os/add-ons/network_settings $< -lbe -o $@
