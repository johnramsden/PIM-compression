CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra -I../PIM-common/common/include
DPU_OPTS = `dpu-pkg-config --cflags --libs dpu`

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

# Default Parameters
NR_DPUS = 1
NR_TASKLETS = 16

SOURCE = dpu_snappy.c

.PHONY: default all dpu host clean tags

default: all

all: dpu host

clean:
	$(RM) decompress
	$(MAKE) -C dpu-decompress $@

dpu:
	DEBUG=$(DEBUG) NR_DPUS=$(NR_DPUS) NR_TASKLETS=$(NR_TASKLETS) $(MAKE) -C dpu-decompress

host: decompress
	
decompress: $(SOURCE)
	$(CC) $(CFLAGS) -DNR_DPUS=$(NR_DPUS) -DNR_TASKLETS=$(NR_TASKLETS) $^ -o $@ $(DPU_OPTS)

tags:
	ctags -R -f tags . /usr/share/upmem/include


TEST_SNAPPY = $(wildcard ../test/*.snappy)
TEST_HOST_VERIFIED = $(patsubst ../test/%.snappy,test/%.host_verified,$(TEST_SNAPPY))
TEST_DPU_VERIFIED = $(patsubst ../test/%.snappy,test/%.dpu_verified,$(TEST_SNAPPY))

.PHONY: test test_dpu test_host
test: test_host test_dpu
test_dpu: test/ $(TEST_DPU_VERIFIED)
test_host: test/ $(TEST_HOST_VERIFIED)

test/:
	mkdir -p test/

# alternative diff command:
# bash -c "diff <(xxd test/$*.host_output) <(xxd ../test/$*.txt)"
test/%.host_verified: ../test/%.snappy ../test/%.txt all
	./decompress -i $< -o test/$*.host_uncompressed 2>&1 | tee test/$*.host_output
	cmp test/$*.host_uncompressed ../test/$*.txt

test/%.dpu_verified: ../test/%.snappy ../test/%.txt all
	./decompress -d -i $< -o test/$*.dpu_uncompressed 2>&1 | tee test/$*.dpu_output
	cmp test/$*.dpu_uncompressed ../test/$*.txt
