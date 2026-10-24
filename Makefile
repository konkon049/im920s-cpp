# make            全部ビルド (build/ に出力)
# make test       ハードウェア不要の単体テスト
# make hwtest     2台つないだ実機テスト (例: make hwtest A=/dev/ttyUSB0 B=/dev/ttyUSB1 N=300)
# make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDFLAGS  ?= -pthread

A ?= /dev/ttyUSB0
B ?= /dev/ttyUSB1
N ?= 100

BINS := build/test_im920s build/example build/pingpong build/im920s_test build/im920s_min

all: $(BINS)

build/test_im920s: tests/test_im920s.cpp include/im920s.hpp
build/example: examples/example.cpp include/im920s.hpp
build/pingpong: examples/pingpong.cpp include/im920s.hpp
build/im920s_test: tools/im920s_test.cpp
build/im920s_min: tools/im920s_min.cpp

$(BINS):
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test: build/test_im920s
	./build/test_im920s

hwtest: build/test_im920s
	./build/test_im920s $(A) $(B) $(N)

# 配布用: 静的リンクなので Ubuntu 22.04 / 24.04 など glibc の版が違う x86_64 Linux でもそのまま動く
bin/pingpong: examples/pingpong.cpp include/im920s.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -static -o $@ $< $(LDFLAGS)
	strip $@

clean:
	rm -rf build

.PHONY: all test hwtest clean
