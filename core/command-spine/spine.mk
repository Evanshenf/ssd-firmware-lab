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
