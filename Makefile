# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

.PHONY: check policy c21-unit layer-fakes links reuse-check

check: policy c21-unit layer-fakes links
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

layer-fakes:
	python3 scripts/check_layer_fakes.py

links:
	python3 scripts/check_relative_links.py

reuse-check:
	reuse lint
