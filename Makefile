# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

.PHONY: check policy layer-fakes links reuse-check

# Retired H0/C2 experiments and their checks live in the immutable preview tag.
# See docs/legacy-experiments.md; missing old directories are not test passes.
check: policy layer-fakes links
	python3 scripts/check_spdx.py
	git diff --check
	git diff --cached --check
	@if git rev-parse --verify HEAD >/dev/null 2>&1; then \
		git diff-tree --check --root --no-commit-id -r HEAD; \
	fi

policy:
	python3 scripts/check_repo_policy.py

layer-fakes:
	python3 scripts/check_layer_fakes.py

links:
	python3 scripts/check_relative_links.py

reuse-check:
	reuse lint
