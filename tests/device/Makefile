CC ?= cc
BUILD := ../../build/tests/device

.PHONY: check clean

# isp_probe_preload.c against a fake libimp, through its named pipe
check: isp_probe_test.c isp_probe_preload.c
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -rdynamic \
		isp_probe_test.c isp_probe_preload.c -ldl -lpthread \
		-o $(BUILD)/isp-probe-test
	rm -f $(BUILD)/isp-probe.fifo $(BUILD)/isp-probe.log
	ISPPROBE_FIFO=$(BUILD)/isp-probe.fifo ISPPROBE_LOG=$(BUILD)/isp-probe.log \
		$(BUILD)/isp-probe-test 2>/dev/null

clean:
	$(RM) -r $(BUILD)
