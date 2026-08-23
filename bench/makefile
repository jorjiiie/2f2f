CXX ?= clang++
CXXFLAGS ?= -std=c++23 -Wall -Wextra -Werror -O2

ROOT := ..
INCLUDES := -I$(ROOT)/src -I$(ROOT)/bench/include
TARGET := $(ROOT)/bin/bench

TBB_CFLAGS := $(shell pkg-config --cflags tbb 2>/dev/null)
TBB_LIBS := $(shell pkg-config --libs tbb 2>/dev/null)

LIBCUCKOO_INCLUDE ?=
FOLLY_CFLAGS ?=
FOLLY_LIBS ?=
FOLLY_DEP_LIBS := $(shell pkg-config --libs libglog 2>/dev/null) \
	$(shell pkg-config --libs gflags 2>/dev/null) \
	$(shell pkg-config --libs fmt 2>/dev/null)

DEFINES :=
LIBS :=

ifneq ($(strip $(TBB_LIBS)),)
DEFINES += -DBENCH_ENABLE_TBB
CXXFLAGS += $(TBB_CFLAGS)
LIBS += $(TBB_LIBS)
endif

ifneq ($(strip $(LIBCUCKOO_INCLUDE)),)
DEFINES += -DBENCH_ENABLE_LIBCUCKOO
INCLUDES += -isystem $(LIBCUCKOO_INCLUDE)
endif

ifneq ($(strip $(FOLLY_LIBS)),)
DEFINES += -DBENCH_ENABLE_FOLLY -DBENCH_ENABLE_FOLLY_ATOMIC
# Keep third-party diagnostics out of this project's -Werror policy.
CXXFLAGS += $(subst -I,-isystem ,$(FOLLY_CFLAGS))
LIBS += $(FOLLY_LIBS) $(FOLLY_DEP_LIBS)
endif

.PHONY: all bench clean

all: bench

bench: $(TARGET)

$(TARGET): main.cc include/bench/backend.hh include/bench/runner.hh \
           include/bench/workload.hh backends/faster.hh \
           backends/sharded_std.hh backends/tbb.hh backends/libcuckoo.hh \
           backends/folly.hh backends/folly_atomic.hh
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -o $@ main.cc $(LIBS)

clean:
	rm -f $(TARGET)
