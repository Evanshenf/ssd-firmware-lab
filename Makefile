# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

.PHONY: check policy links reuse-check

check: policy links
	python3 scripts/check_spdx.py
	git diff --check
	git diff --cached --check
	@if git rev-parse --verify HEAD >/dev/null 2>&1; then \
		git diff-tree --check --root --no-commit-id -r HEAD; \
	fi

policy:
	python3 scripts/check_repo_policy.py

links:
	python3 scripts/check_relative_links.py

reuse-check:
	reuse lint
