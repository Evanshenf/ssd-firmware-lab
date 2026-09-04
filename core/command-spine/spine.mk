# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# This is the sole ordered S0-A construction manifest.  Keep every source,
# artifact and future M4 membership list literal: no wildcard or registry may
# select an implementation.
override SPINE_S0A_BUILD_DIR := build/s0a

override SPINE_S0A_CONTRACT_SOURCES := spine_contracts.c spine_construction.c
override SPINE_S0A_CONTRACT_MEMBERS := spine_contracts.o spine_construction.o
override SPINE_S0A_CONTRACT_OBJECTS := \
	build/s0a/spine_contracts.o build/s0a/spine_construction.o
override SPINE_S0A_PUBLIC_TEST_SOURCE := tests/test_s0a_contracts.c
override SPINE_S0A_FAKE_SOURCE := fakes/s0a_fake_main.c
override SPINE_S0A_NEGATIVE_SOURCE := tests/negative_token_substitution.c
override SPINE_S0A_PUBLIC_TEST_OBJECT := build/s0a/test_s0a_contracts.o
override SPINE_S0A_FAKE_OBJECT := build/s0a/s0a_fake_main.o

override SPINE_S0A_ARCHIVE := build/s0a/libfwlab_spine_contracts_v0.a
override SPINE_S0A_PUBLIC_CONTRACTS := build/s0a/s0a_public_contracts
override SPINE_S0A_FAKE_ADJACENT := build/s0a/s0a_fake_adjacent_link

override SPINE_S0A_PUBLIC_HEADERS := \
	../../include/fwlab/portable/host_action_program_v0.h \
	../../include/fwlab/contracts/controller_buffer_v0.h \
	../../include/fwlab/contracts/host_data_v0.h \
	../../include/fwlab/contracts/block_service_v0.h \
	../../include/fwlab/contracts/owner_control_v0.h

override SPINE_S0A_ANCHOR_HEADER := spine_anchor_internal.h
override SPINE_S0A_ANCHOR_SYMBOLS := \
	fwlab_authoritative_sq_consumer_v0 \
	fwlab_authoritative_cqe_publisher_v0

# Sole literal S0-B construction manifest.  It is the second and final
# component-only checkpoint; every intermediate and artifact is explicit.
override SPINE_S0B_BUILD_DIR := build/s0b
override SPINE_S0B_LIFECYCLE_SOURCES := \
	spine_contracts.c spine_lifecycle.c
override SPINE_S0B_LIFECYCLE_MEMBERS := \
	spine_contracts.o spine_lifecycle.o
override SPINE_S0B_LIFECYCLE_OBJECTS := \
	build/s0b/spine_contracts.o build/s0b/spine_lifecycle.o
override SPINE_S0B_PROFILE_SOURCES := \
	profiles/c43_p1_adapter.c profiles/linux_profile_v1_adapter.c
override SPINE_S0B_PROFILE_MEMBERS := \
	c43_p1_adapter.o linux_profile_v1_adapter.o
override SPINE_S0B_PROFILE_OBJECTS := \
	build/s0b/c43_p1_adapter.o build/s0b/linux_profile_v1_adapter.o
override SPINE_S0B_C41_SOURCE := ../c4-nvme/c41_codec.c
override SPINE_S0B_C41_OBJECT := build/s0b/c41_codec.o
override SPINE_S0B_FAKE_SOURCE := fakes/spine_fake_adjacent.c
override SPINE_S0B_FAKE_OBJECT := build/s0b/spine_fake_adjacent.o
override SPINE_S0B_TINY_SOURCE := tests/tiny_profile_fixture.c
override SPINE_S0B_TINY_OBJECT := build/s0b/tiny_profile_fixture.o
override SPINE_S0B_MATRIX_SOURCE := tests/test_s0b_lifecycle.c
override SPINE_S0B_MATRIX_OBJECT := build/s0b/test_s0b_lifecycle.o
override SPINE_S0B_INTERMEDIATES := \
	build/s0b/spine_contracts.o \
	build/s0b/spine_lifecycle.o \
	build/s0b/c43_p1_adapter.o \
	build/s0b/linux_profile_v1_adapter.o \
	build/s0b/c41_codec.o \
	build/s0b/spine_fake_adjacent.o \
	build/s0b/tiny_profile_fixture.o \
	build/s0b/test_s0b_lifecycle.o
override SPINE_S0B_LIFECYCLE_ARCHIVE := \
	build/s0b/libfwlab_spine_lifecycle_v0.a
override SPINE_S0B_PROFILE_ARCHIVE := \
	build/s0b/libfwlab_spine_profiles_v0.a
override SPINE_S0B_MATRIX := build/s0b/s0b_profile_matrix
override SPINE_S0B_ARTIFACTS := \
	build/s0b/libfwlab_spine_lifecycle_v0.a \
	build/s0b/libfwlab_spine_profiles_v0.a \
	build/s0b/s0b_profile_matrix
override SPINE_S0B_OWNED := \
	build/s0b/spine_contracts.o \
	build/s0b/spine_lifecycle.o \
	build/s0b/c43_p1_adapter.o \
	build/s0b/linux_profile_v1_adapter.o \
	build/s0b/c41_codec.o \
	build/s0b/spine_fake_adjacent.o \
	build/s0b/tiny_profile_fixture.o \
	build/s0b/test_s0b_lifecycle.o \
	build/s0b/libfwlab_spine_lifecycle_v0.a \
	build/s0b/libfwlab_spine_profiles_v0.a \
	build/s0b/s0b_profile_matrix

# Reserved construction metadata only.  S0-A builds neither kernel module.
override SPINE_M4_AUTHORITATIVE_TARGET := ssd_fwlab_m4_spine_v0
override SPINE_M4_AUTHORITATIVE_MEMBERS := \
	m4_main.o m4_pci_transport.o linux_hif_binding_v0.o \
	m4_authoritative_construct.o
override SPINE_M4_FIXTURE_TARGET := ssd_fwlab_m4_poc_fixture
override SPINE_M4_FIXTURE_MEMBERS := \
	fixture/m4_pci_main.o fixture/m4_pci.o fixture/m4_frontend.o \
	fixture/m4_nvme.o fixture/m4_media_fixture.o
override SPINE_M4_LEGACY_FIXTURE_MEMBERS := \
	fixture/m4_frontend.o fixture/m4_nvme.o fixture/m4_media_fixture.o
