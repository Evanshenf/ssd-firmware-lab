# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

FWLAB_FAKE_OUTPUT ?= build/c34_file_fake_link

.PHONY: check fake-link clean

check:
	$(MAKE) -C c34-file check

fake-link:
	$(MAKE) -C c34-file fake-link FWLAB_FAKE_OUTPUT=$(abspath $(FWLAB_FAKE_OUTPUT))

clean:
	$(MAKE) -C c34-file clean
