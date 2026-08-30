# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

FWLAB_FAKE_OUTPUT ?= build/c4_fake_link
FWLAB_C35_FAKE_OUTPUT := $(abspath $(FWLAB_FAKE_OUTPUT)).c35

.PHONY: check fake-link clean

check:
	$(MAKE) -C headless-c35 check
	$(MAKE) -C headless-c4 check

fake-link:
	$(MAKE) -C headless-c35 fake-link \
		FWLAB_FAKE_OUTPUT=$(FWLAB_C35_FAKE_OUTPUT)
	test -x $(FWLAB_C35_FAKE_OUTPUT)
	$(FWLAB_C35_FAKE_OUTPUT) >/dev/null
	$(MAKE) -C headless-c4 fake-link \
		FWLAB_FAKE_OUTPUT=$(abspath $(FWLAB_FAKE_OUTPUT))
	rm -f $(FWLAB_C35_FAKE_OUTPUT)

clean:
	$(MAKE) -C headless-c35 clean
	$(MAKE) -C headless-c4 clean
