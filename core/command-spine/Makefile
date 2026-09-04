# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
AR ?= ar

CPPFLAGS += -I../../include -I.
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
CFLAGS ?= $(BASE_CFLAGS)

include spine.mk

.PHONY: all check-s0a check-s0a-owned check-s0b check-s0b-owned \
	check-headers check-public-contracts \
	check-fake-adjacent check-token-negatives check-anchor-negatives \
	check-architecture check-s0b-architecture clean clean-s0a-owned \
	clean-s0b-owned

.INTERMEDIATE: $(SPINE_S0A_CONTRACT_OBJECTS) \
	$(SPINE_S0A_PUBLIC_TEST_OBJECT) $(SPINE_S0A_FAKE_OBJECT) \
	$(SPINE_S0B_INTERMEDIATES)

all: check-s0a

check-s0a:
	$(MAKE) --no-print-directory clean-s0a-owned
	$(MAKE) --no-print-directory check-s0a-owned

check-s0b:
	$(MAKE) --no-print-directory clean-s0a-owned clean-s0b-owned
	$(MAKE) --no-print-directory check-s0b-owned

$(SPINE_S0A_BUILD_DIR):
	mkdir -p $@

$(SPINE_S0A_BUILD_DIR)/%.o: %.c $(SPINE_S0A_PUBLIC_HEADERS) \
		$(SPINE_S0A_ANCHOR_HEADER) spine.mk | $(SPINE_S0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0A_PUBLIC_TEST_OBJECT): $(SPINE_S0A_PUBLIC_TEST_SOURCE) \
		$(SPINE_S0A_PUBLIC_HEADERS) | $(SPINE_S0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0A_FAKE_OBJECT): $(SPINE_S0A_FAKE_SOURCE) \
		$(SPINE_S0A_PUBLIC_HEADERS) $(SPINE_S0A_ANCHOR_HEADER) \
		| $(SPINE_S0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0A_ARCHIVE): $(SPINE_S0A_CONTRACT_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(SPINE_S0A_CONTRACT_OBJECTS)

$(SPINE_S0A_PUBLIC_CONTRACTS): $(SPINE_S0A_PUBLIC_TEST_OBJECT) \
		$(SPINE_S0A_ARCHIVE)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(SPINE_S0A_PUBLIC_TEST_OBJECT) $(SPINE_S0A_ARCHIVE)

$(SPINE_S0A_FAKE_ADJACENT): $(SPINE_S0A_FAKE_OBJECT) \
		$(SPINE_S0A_ARCHIVE)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SPINE_S0A_FAKE_OBJECT) \
		-Wl,--whole-archive $(SPINE_S0A_ARCHIVE) -Wl,--no-whole-archive

check-headers:
	@set -eu; for header in $(SPINE_S0A_PUBLIC_HEADERS); do \
		$(CC) $(CPPFLAGS) $(CFLAGS) -x c -fsyntax-only \
			-include "$$header" /dev/null; \
		$(CLANG) $(CPPFLAGS) $(CFLAGS) -x c -fsyntax-only \
			-include "$$header" /dev/null; \
	done
	@echo "S0-A standalone public headers: PASS (GCC+Clang headers=5)"

check-public-contracts: $(SPINE_S0A_PUBLIC_CONTRACTS)
	$(SPINE_S0A_PUBLIC_CONTRACTS)

check-fake-adjacent: $(SPINE_S0A_FAKE_ADJACENT)
	$(SPINE_S0A_FAKE_ADJACENT)

check-token-negatives: | $(SPINE_S0A_BUILD_DIR)
	@set -eu; temporary=$$(mktemp -d); \
	trap 'rm -rf -- "$$temporary"' EXIT; \
	for mode in \
		FWLAB_NEGATIVE_HDAR_DMOP FWLAB_NEGATIVE_HDAR_CBLS \
		FWLAB_NEGATIVE_HDAR_CPLS FWLAB_NEGATIVE_DMOP_CBLS \
		FWLAB_NEGATIVE_DMOP_CPLS FWLAB_NEGATIVE_CBLS_CPLS \
		FWLAB_NEGATIVE_LEGACY_ACTION; do \
		if $(CC) $(CPPFLAGS) $(CFLAGS) \
			-Werror=incompatible-pointer-types -D$$mode \
			-fsyntax-only $(SPINE_S0A_NEGATIVE_SOURCE) \
			>"$$temporary/$$mode.out" 2>"$$temporary/$$mode.err"; then \
			echo "S0-A token negative unexpectedly compiled: $$mode" >&2; \
			exit 1; \
		fi; \
		if ! grep -Eiq 'incompatible.*pointer|incompatible-pointer-types' \
			"$$temporary/$$mode.err"; then \
			cat "$$temporary/$$mode.err" >&2; \
			echo "S0-A token negative failed for the wrong reason: $$mode" >&2; \
			exit 1; \
		fi; \
	done
	@echo "S0-A nominal token substitution negatives: PASS (cases=7)"

check-anchor-negatives: $(SPINE_S0A_ARCHIVE) $(SPINE_S0A_FAKE_OBJECT)
	@set -eu; temporary=$$(mktemp -d); \
	trap 'rm -rf -- "$$temporary"' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFWLAB_NEGATIVE_MISSING_ANCHORS \
		-c $(SPINE_S0A_NEGATIVE_SOURCE) -o "$$temporary/missing.o"; \
	if $(CC) $(CFLAGS) $(LDFLAGS) -o "$$temporary/missing" \
		"$$temporary/missing.o" -Wl,--whole-archive \
		$(SPINE_S0A_ARCHIVE) -Wl,--no-whole-archive \
		>"$$temporary/missing.out" 2>"$$temporary/missing.err"; then \
		echo "S0-A missing-anchor link unexpectedly succeeded" >&2; \
		exit 1; \
	fi; \
	for anchor in $(SPINE_S0A_ANCHOR_SYMBOLS); do \
		grep -Eiq "undefined reference to .*$$anchor" \
			"$$temporary/missing.err" || { \
			cat "$$temporary/missing.err" >&2; \
			echo "missing-anchor class omitted $$anchor" >&2; exit 1; }; \
	done; \
	if grep -Eiq 'multiple definition|cannot find|no such file|file format' \
		"$$temporary/missing.err"; then \
		cat "$$temporary/missing.err" >&2; \
		echo "missing-anchor link failed for a second class" >&2; exit 1; \
	fi; \
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFWLAB_NEGATIVE_DUPLICATE_ANCHORS \
		-c $(SPINE_S0A_NEGATIVE_SOURCE) -o "$$temporary/duplicate.o"; \
	if $(CC) $(CFLAGS) $(LDFLAGS) -o "$$temporary/duplicate" \
		$(SPINE_S0A_FAKE_OBJECT) "$$temporary/duplicate.o" \
		-Wl,--whole-archive $(SPINE_S0A_ARCHIVE) -Wl,--no-whole-archive \
		>"$$temporary/duplicate.out" 2>"$$temporary/duplicate.err"; then \
		echo "S0-A duplicate-anchor link unexpectedly succeeded" >&2; \
		exit 1; \
	fi; \
	for anchor in $(SPINE_S0A_ANCHOR_SYMBOLS); do \
		grep -Eiq "multiple definition of .*$$anchor" \
			"$$temporary/duplicate.err" || { \
			cat "$$temporary/duplicate.err" >&2; \
			echo "duplicate-anchor class omitted $$anchor" >&2; exit 1; }; \
	done; \
	if grep -Eiq 'undefined reference|cannot find|no such file|file format' \
		"$$temporary/duplicate.err"; then \
		cat "$$temporary/duplicate.err" >&2; \
		echo "duplicate-anchor link failed for a second class" >&2; exit 1; \
	fi
	@echo "S0-A link-exclusive anchor negatives: PASS (missing=1 duplicate=1)"

check-architecture: $(SPINE_S0A_ARCHIVE) $(SPINE_S0A_PUBLIC_CONTRACTS) \
		$(SPINE_S0A_FAKE_ADJACENT)
	python3 ../../scripts/check_spine_architecture.py \
		--archive $(SPINE_S0A_ARCHIVE) \
		--public $(SPINE_S0A_PUBLIC_CONTRACTS) \
		--fake $(SPINE_S0A_FAKE_ADJACENT)

check-s0a-owned: check-headers check-public-contracts check-fake-adjacent \
	check-token-negatives check-anchor-negatives check-architecture

$(SPINE_S0B_BUILD_DIR):
	mkdir -p $@

$(SPINE_S0B_LIFECYCLE_OBJECTS): $(SPINE_S0B_BUILD_DIR)/%.o: %.c \
		spine_internal.h $(SPINE_S0A_PUBLIC_HEADERS) \
		spine.mk | $(SPINE_S0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0B_PROFILE_OBJECTS): $(SPINE_S0B_BUILD_DIR)/%.o: profiles/%.c \
		spine_internal.h \
		../../include/fwlab/portable/host_action_program_v0.h \
		../../include/fwlab/portable/nvme_codec.h spine.mk \
		| $(SPINE_S0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0B_C41_OBJECT): $(SPINE_S0B_C41_SOURCE) \
		../../include/fwlab/portable/nvme_codec.h \
		../../include/fwlab/portable/nvme_types.h spine.mk \
		| $(SPINE_S0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0B_FAKE_OBJECT): $(SPINE_S0B_FAKE_SOURCE) \
		fakes/spine_fake_adjacent.h spine_internal.h \
		../../include/fwlab/portable/host_action_program_v0.h spine.mk \
		| $(SPINE_S0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0B_TINY_OBJECT): $(SPINE_S0B_TINY_SOURCE) \
		fakes/spine_fake_adjacent.h spine_internal.h \
		../../include/fwlab/portable/host_action_program_v0.h \
		../../include/fwlab/portable/nvme_codec.h spine.mk \
		| $(SPINE_S0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0B_MATRIX_OBJECT): $(SPINE_S0B_MATRIX_SOURCE) \
		fakes/spine_fake_adjacent.h spine_internal.h \
		../../include/fwlab/portable/host_action_program_v0.h spine.mk \
		| $(SPINE_S0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(SPINE_S0B_LIFECYCLE_ARCHIVE): $(SPINE_S0B_LIFECYCLE_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(SPINE_S0B_LIFECYCLE_OBJECTS)

$(SPINE_S0B_PROFILE_ARCHIVE): $(SPINE_S0B_PROFILE_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(SPINE_S0B_PROFILE_OBJECTS)

$(SPINE_S0B_MATRIX): $(SPINE_S0B_MATRIX_OBJECT) $(SPINE_S0B_FAKE_OBJECT) \
		$(SPINE_S0B_TINY_OBJECT) $(SPINE_S0B_C41_OBJECT) \
		$(SPINE_S0B_LIFECYCLE_ARCHIVE) $(SPINE_S0B_PROFILE_ARCHIVE)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(SPINE_S0B_MATRIX_OBJECT) $(SPINE_S0B_FAKE_OBJECT) \
		$(SPINE_S0B_TINY_OBJECT) $(SPINE_S0B_C41_OBJECT) \
		-Wl,--whole-archive $(SPINE_S0B_LIFECYCLE_ARCHIVE) \
		$(SPINE_S0B_PROFILE_ARCHIVE) -Wl,--no-whole-archive

check-s0b-architecture: $(SPINE_S0B_LIFECYCLE_ARCHIVE) \
		$(SPINE_S0B_PROFILE_ARCHIVE) $(SPINE_S0B_MATRIX)
	@set -eu; \
	digest=$$(sha256sum $(addprefix $(SPINE_S0B_BUILD_DIR)/,\
		$(word 2,$(SPINE_S0B_LIFECYCLE_MEMBERS))) | \
		awk '{print $$1}'); \
	if ! receipts=$$($(SPINE_S0B_MATRIX) "$$digest"); then \
		echo "S0-B matrix execution failed" >&2; exit 1; \
	fi; \
	printf '%s\n' "$$receipts"; \
	printf '%s\n' "$$receipts" | \
		python3 ../../scripts/check_spine_architecture.py --phase s0b \
		--lifecycle $(SPINE_S0B_LIFECYCLE_ARCHIVE) \
		--profiles $(SPINE_S0B_PROFILE_ARCHIVE) \
		--matrix $(SPINE_S0B_MATRIX) --receipts -

check-s0b-owned: check-s0b-architecture

clean: clean-s0a-owned clean-s0b-owned

clean-s0a-owned:
	rm -f -- $(SPINE_S0A_CONTRACT_OBJECTS) \
		$(SPINE_S0A_PUBLIC_TEST_OBJECT) $(SPINE_S0A_FAKE_OBJECT) \
		$(SPINE_S0A_ARCHIVE) $(SPINE_S0A_PUBLIC_CONTRACTS) \
		$(SPINE_S0A_FAKE_ADJACENT)
	@rmdir -- $(SPINE_S0A_BUILD_DIR) build 2>/dev/null || true

clean-s0b-owned:
	rm -f -- $(SPINE_S0B_OWNED)
	@rmdir -- $(SPINE_S0B_BUILD_DIR) build 2>/dev/null || true
