CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wno-unused-function -Wno-unused-variable -fvisibility=default
OUT      ?= out
# The plugin API header lives in the main tree.
HDR_DIR  ?= ../../src/slic3r/GUI/FlashForge

all: $(OUT)/libFlashNetwork.dylib $(OUT)/fnet_helper

gen/gen_common.inc gen/gen_shim.inc gen/gen_helper.inc gen/fake_stubs.inc: gen/gen.py $(HDR_DIR)/FlashNetwork.h
	python3 gen/gen.py $(HDR_DIR)/FlashNetwork.h gen

$(OUT):
	mkdir -p $(OUT)

# arm64 shim: loaded in-process by the (arm64) slicer
$(OUT)/libFlashNetwork.dylib: shim.cpp types.h engine.h proto.h gen/gen_common.inc gen/gen_shim.inc | $(OUT)
	$(CXX) $(CXXFLAGS) -arch arm64 -I. -I$(HDR_DIR) -Igen -dynamiclib -install_name @rpath/libFlashNetwork.dylib -mmacosx-version-min=11.0 shim.cpp -o $@

# x86_64 helper: runs under Rosetta and hosts the original plugin
$(OUT)/fnet_helper: helper.cpp types.h engine.h proto.h gen/gen_common.inc gen/gen_helper.inc | $(OUT)
	$(CXX) $(CXXFLAGS) -arch x86_64 -I. -I$(HDR_DIR) -Igen -mmacosx-version-min=10.15 helper.cpp -o $@ -ldl

# Tests (see README): fake plugin + drivers, direct (x86_64) and bridged (arm64)
tests: gen/fake_stubs.inc | $(OUT)
	mkdir -p $(OUT)/fake $(OUT)/fakebridge
	$(CXX) -std=c++17 -O1 -arch x86_64 -I. -I$(HDR_DIR) -Igen -dynamiclib fake_impl.cpp fake_stubs.cpp -o $(OUT)/fake/libfake_x64.dylib
	$(CXX) -std=c++17 -O1 -arch x86_64 -I. -I$(HDR_DIR) test_fake.cpp -o $(OUT)/test_fake_x64
	$(CXX) -std=c++17 -O1 -arch arm64  -I. -I$(HDR_DIR) test_fake.cpp -o $(OUT)/test_fake_arm64
	cp $(OUT)/libFlashNetwork.dylib $(OUT)/fnet_helper $(OUT)/fakebridge/
	cp $(OUT)/fake/libfake_x64.dylib $(OUT)/fakebridge/libFlashNetwork_x64.dylib

clean:
	rm -rf $(OUT) gen/*.inc

.PHONY: all clean tests
