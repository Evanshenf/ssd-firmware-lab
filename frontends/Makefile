# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

FWLAB_FAKE_OUTPUT ?= build/c35_fake_link

.PHONY: check fake-link clean

check:
	$(MAKE) -C headless-c35 check

fake-link:
	$(MAKE) -C headless-c35 fake-link \
		FWLAB_FAKE_OUTPUT=$(abspath $(FWLAB_FAKE_OUTPUT))

clean:
	$(MAKE) -C headless-c35 clean
