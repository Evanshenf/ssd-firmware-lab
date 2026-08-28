# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

.PHONY: check policy c21-unit c22-build c23-build layer-fakes links reuse-check

check: policy c21-unit c22-build c23-build layer-fakes links
	python3 scripts/check_spdx.py
	git diff --check
	git diff --cached --check
	@if git rev-parse --verify HEAD >/dev/null 2>&1; then \
		git diff-tree --check --root --no-commit-id -r HEAD; \
	fi

policy:
	python3 scripts/check_repo_policy.py

c21-unit:
	@if [ -f tests/unit/vfio-c21/Makefile ]; then \
		$(MAKE) -C tests/unit/vfio-c21 check; \
	else \
		echo "C2.1 unit: SKIP (tests/unit/vfio-c21 not present)"; \
	fi

c22-build:
	@if [ -f tools/vfio-cdev-v1/Makefile ]; then \
		$(MAKE) -C tools/vfio-cdev-v1; \
		./tools/vfio-cdev-v1/build/vfio_cdev_v1_c2_2 --selftest-layout; \
	else \
		echo "C2.2 userspace build: SKIP (tool not present)"; \
	fi
	@if [ -f tests/privileged/c2_2_vfio_cdev_v1.sh ]; then \
		sh -n tests/privileged/c2_2_vfio_cdev_v1.sh; \
	else \
		echo "C2.2 privileged syntax: SKIP (script not present)"; \
	fi

c23-build:
	@if [ -f tools/vfio-cdev-v1-c23/Makefile ]; then \
		$(MAKE) -C tools/vfio-cdev-v1-c23; \
		./tools/vfio-cdev-v1-c23/build/vfio_cdev_v1_c23 --selftest; \
	else \
		echo "C2.3 userspace build: SKIP (tool not present)"; \
	fi
	@if [ -f tests/privileged/c2_3_vfio_cdev_v1.sh ]; then \
		sh -n tests/privileged/c2_3_vfio_cdev_v1.sh; \
	else \
		echo "C2.3 privileged syntax: SKIP (script not present)"; \
	fi

layer-fakes:
	python3 scripts/check_layer_fakes.py

links:
	python3 scripts/check_relative_links.py

reuse-check:
	reuse lint
