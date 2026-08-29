// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE

#include "c25_session.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define C25_INITIAL_OFFSET 113U
#define C25_WRITE_OFFSET 1021U
#define C25_PARALLEL_ROUNDS 24U

struct c25_parallel_arg {
	struct c25_session *session;
	pthread_barrier_t *barrier;
	unsigned int seed;
	int ret;
};

static int all_zero(const unsigned char *buffer, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
		if (buffer[index])
			return 0;
	return 1;
}

static int verify_ready_state(struct c25_session *session)
{
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	struct c25_state state;
	int ret;

	ret = c25_read_state(session, wire, &state);
	if (ret)
		return ret;
	if (state.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    !(state.flags & FWLAB_C21_ST_F_OPEN) ||
	    !(state.flags & FWLAB_C21_ST_F_ATTACHED) ||
	    !state.generation || !state.next_sequence)
		return -EPROTO;
	return 0;
}

static int verify_cleared_transition(struct c25_session *session,
				     const struct c25_state *before,
				     bool attached)
{
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	struct c25_result result;
	struct c25_state after;
	int ret;

	ret = c25_read_state(session, state_wire, &after);
	if (ret)
		return ret;
	if (after.generation != before->generation + 1U ||
	    after.last_sequence != 0 || after.next_sequence != 1 ||
	    (after.flags & FWLAB_C21_ST_F_RESULT_VALID) ||
	    !!(after.flags & FWLAB_C21_ST_F_ATTACHED) != attached ||
	    after.device_state !=
		    (attached ? FWLAB_C21_STATE_OPEN_ATTACHED :
				FWLAB_C21_STATE_OPEN_UNATTACHED))
		return -EPROTO;
	ret = c25_read_data(session, data, sizeof(data));
	if (ret || !all_zero(data, sizeof(data)))
		return ret ? ret : -EPROTO;
	ret = c25_read_result(session, result_wire, &result);
	return ret == -ENODATA ? 0 : -EPROTO;
}

static int assert_observer_unchanged(
	struct c25_session *observer,
	const struct c25_observation *before)
{
	struct c25_observation after;
	int ret;

	ret = c25_capture(observer, &after);
	if (ret)
		return ret;
	return c25_observation_equal(before, &after) ? 0 : -EXDEV;
}

static int populate_from_ioas(struct c25_session *session,
			      unsigned int page_seed,
			      unsigned int data_seed,
			      size_t page_offset)
{
	unsigned char data_before[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char data_after[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char page_before[C25_PAGE_SIZE];
	int ret;

	if (page_offset > C25_PAGE_SIZE - C25_COPY_LENGTH)
		return -ERANGE;
	c25_fill_pattern(session->page, C25_PAGE_SIZE, page_seed);
	c25_fill_pattern(data_before, sizeof(data_before), data_seed);
	memcpy(page_before, session->page, sizeof(page_before));
	ret = c25_write_data(session, data_before, sizeof(data_before));
	if (ret)
		return ret;
	ret = c25_submit(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			 session->iova + page_offset, C25_COPY_LENGTH, 0);
	if (ret)
		return ret;
	ret = c25_read_data(session, data_after, sizeof(data_after));
	if (ret)
		return ret;
	if (memcmp(data_after, session->page + page_offset,
		   C25_COPY_LENGTH) ||
	    memcmp(data_after + C25_COPY_LENGTH,
		   data_before + C25_COPY_LENGTH,
		   sizeof(data_after) - C25_COPY_LENGTH) ||
	    memcmp(page_before, session->page, sizeof(page_before)))
		return -EIO;
	return 0;
}

static int copy_to_ioas_isolated(struct c25_session *actor,
				 struct c25_session *observer,
				 unsigned int data_seed,
				 unsigned int page_seed)
{
	struct c25_observation observer_before;
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char data_after[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char page_before[C25_PAGE_SIZE];
	size_t index;
	int ret;

	c25_fill_pattern(data, sizeof(data), data_seed);
	c25_fill_pattern(actor->page, C25_PAGE_SIZE, page_seed);
	memcpy(page_before, actor->page, sizeof(page_before));
	ret = c25_write_data(actor, data, sizeof(data));
	if (ret)
		return ret;
	ret = c25_capture(observer, &observer_before);
	if (ret)
		return ret;
	ret = c25_submit(actor, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
			 actor->iova + C25_WRITE_OFFSET, C25_COPY_LENGTH, 0);
	if (ret)
		return ret;
	for (index = 0; index < C25_PAGE_SIZE; index++) {
		unsigned char expected =
			index >= C25_WRITE_OFFSET &&
			index < C25_WRITE_OFFSET + C25_COPY_LENGTH ?
				data[index - C25_WRITE_OFFSET] :
				page_before[index];

		if (actor->page[index] != expected)
			return -EIO;
	}
	ret = c25_read_data(actor, data_after, sizeof(data_after));
	if (ret || memcmp(data, data_after, sizeof(data)))
		return ret ? ret : -EIO;
	return assert_observer_unchanged(observer, &observer_before);
}

static int exercise_nonterminal_isolation(struct c25_session *actor,
					  struct c25_session *observer,
					  unsigned int seed)
{
	struct c25_observation observer_before;
	unsigned char actor_page[FWLAB_C21_IOAS_PAGE_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char actor_data[FWLAB_C21_DATA_REGION_SIZE];
	struct c25_state before;
	struct c25_state observer_state;
	int ret;

	/* RESET must clear and advance only the actor. */
	ret = c25_capture(observer, &observer_before);
	if (ret)
		return ret;
	memcpy(actor_page, actor->page, sizeof(actor_page));
	ret = c25_read_state(actor, state_wire, &before);
	if (ret)
		return ret;
	ret = c25_session_reset(actor);
	if (ret)
		return ret;
	ret = verify_cleared_transition(actor, &before, true);
	if (ret || memcmp(actor_page, actor->page, sizeof(actor_page)))
		return ret ? ret : -EIO;
	ret = populate_from_ioas(actor, seed + 1U, seed + 2U,
				 C25_INITIAL_OFFSET);
	if (ret)
		return ret;
	ret = assert_observer_unchanged(observer, &observer_before);
	if (ret)
		return ret;

	/* Exact UNMAP may affect only the actor's provider result. */
	ret = c25_capture(observer, &observer_before);
	if (ret)
		return ret;
	ret = c25_read_data(actor, actor_data, sizeof(actor_data));
	if (ret)
		return ret;
	ret = c25_session_unmap(actor);
	if (ret)
		return ret;
	ret = c25_submit(actor, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			 actor->iova + C25_INITIAL_OFFSET,
			 C25_COPY_LENGTH, -ENOENT);
	if (ret)
		return ret;
	{
		unsigned char after_data[FWLAB_C21_DATA_REGION_SIZE];

		ret = c25_read_data(actor, after_data, sizeof(after_data));
		if (ret || memcmp(actor_data, after_data, sizeof(actor_data)))
			return ret ? ret : -EIO;
	}
	ret = c25_session_map(actor);
	if (ret)
		return ret;
	ret = populate_from_ioas(actor, seed + 3U, seed + 4U,
				 C25_INITIAL_OFFSET + 17U);
	if (ret)
		return ret;
	ret = assert_observer_unchanged(observer, &observer_before);
	if (ret)
		return ret;

	/* DETACH and reattach advance only the actor's generation. */
	ret = c25_capture(observer, &observer_before);
	if (ret)
		return ret;
	ret = c25_read_state(actor, state_wire, &before);
	if (ret)
		return ret;
	ret = c25_session_detach(actor);
	if (ret)
		return ret;
	ret = verify_cleared_transition(actor, &before, false);
	if (ret)
		return ret;
	ret = c25_read_state(actor, state_wire, &before);
	if (ret)
		return ret;
	ret = c25_session_attach(actor);
	if (ret)
		return ret;
	ret = verify_cleared_transition(actor, &before, true);
	if (ret)
		return ret;
	ret = populate_from_ioas(actor, seed + 5U, seed + 6U,
				 C25_INITIAL_OFFSET + 29U);
	if (ret)
		return ret;
	ret = assert_observer_unchanged(observer, &observer_before);
	if (ret)
		return ret;

	ret = c25_read_state(observer, state_wire, &observer_state);
	if (ret ||
	    observer_state.device_state != FWLAB_C21_STATE_OPEN_ATTACHED)
		return ret ? ret : -EPROTO;
	printf("C2.5 actor=%s observer=%s reset/unmap/detach isolation: PASS\n",
	       actor->label, observer->label);
	return 0;
}

static int parallel_one_direction(struct c25_session *session,
				  unsigned int seed,
				  unsigned int round)
{
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char observed[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char page_before[C25_PAGE_SIZE];
	size_t offset = 64U + ((size_t)round * 97U) %
				(C25_PAGE_SIZE - C25_COPY_LENGTH - 64U);
	size_t index;
	int ret;

	if (!(round & 1U)) {
		c25_fill_pattern(session->page, C25_PAGE_SIZE,
				 seed + round * 3U);
		c25_fill_pattern(data, sizeof(data), seed + round * 3U + 1U);
		memcpy(page_before, session->page, sizeof(page_before));
		ret = c25_write_data(session, data, sizeof(data));
		if (ret)
			return ret;
		ret = c25_submit(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
				 session->iova + offset, C25_COPY_LENGTH, 0);
		if (ret)
			return ret;
		ret = c25_read_data(session, observed, sizeof(observed));
		if (ret || memcmp(observed, session->page + offset,
				  C25_COPY_LENGTH) ||
		    memcmp(observed + C25_COPY_LENGTH,
			   data + C25_COPY_LENGTH,
			   sizeof(observed) - C25_COPY_LENGTH) ||
		    memcmp(page_before, session->page, sizeof(page_before)))
			return ret ? ret : -EIO;
		return 0;
	}

	c25_fill_pattern(data, sizeof(data), seed + round * 3U);
	c25_fill_pattern(session->page, C25_PAGE_SIZE, seed + round * 3U + 1U);
	memcpy(page_before, session->page, sizeof(page_before));
	ret = c25_write_data(session, data, sizeof(data));
	if (ret)
		return ret;
	ret = c25_submit(session, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
			 session->iova + offset, C25_COPY_LENGTH, 0);
	if (ret)
		return ret;
	for (index = 0; index < C25_PAGE_SIZE; index++) {
		unsigned char expected =
			index >= offset && index < offset + C25_COPY_LENGTH ?
				data[index - offset] : page_before[index];

		if (session->page[index] != expected)
			return -EIO;
	}
	ret = c25_read_data(session, observed, sizeof(observed));
	return ret ? ret : (memcmp(data, observed, sizeof(data)) ? -EIO : 0);
}

static void *parallel_worker(void *context)
{
	struct c25_parallel_arg *arg = context;
	unsigned int round;
	int barrier_ret;

	barrier_ret = pthread_barrier_wait(arg->barrier);
	if (barrier_ret && barrier_ret != PTHREAD_BARRIER_SERIAL_THREAD) {
		arg->ret = -barrier_ret;
		return NULL;
	}
	for (round = 0; round < C25_PARALLEL_ROUNDS; round++) {
		arg->ret = parallel_one_direction(arg->session, arg->seed, round);
		if (arg->ret)
			break;
	}
	return NULL;
}

static int run_parallel_smoke(struct c25_session *base,
			      struct c25_session *peer)
{
	pthread_barrier_t barrier;
	pthread_t threads[2];
	struct c25_parallel_arg args[2] = {
		{ .session = base, .barrier = &barrier, .seed = 0x41U },
		{ .session = peer, .barrier = &barrier, .seed = 0xb2U },
	};
	int created = 0;
	int ret;

	ret = pthread_barrier_init(&barrier, NULL, 2);
	if (ret)
		return -ret;
	ret = pthread_create(&threads[0], NULL, parallel_worker, &args[0]);
	if (ret)
		goto out_destroy;
	created = 1;
	ret = pthread_create(&threads[1], NULL, parallel_worker, &args[1]);
	if (ret) {
		/* Release the first worker from its two-party barrier. */
		(void)pthread_barrier_wait(&barrier);
		goto out_join;
	}
	created = 2;

out_join:
	while (created > 0) {
		int join_ret;

		created--;
		join_ret = pthread_join(threads[created], NULL);
		if (join_ret && !ret)
			ret = join_ret;
	}
	if (!ret && (args[0].ret || args[1].ret))
		ret = args[0].ret ? args[0].ret : args[1].ret;
out_destroy:
	(void)pthread_barrier_destroy(&barrier);
	if (ret)
		return ret > 0 ? -ret : ret;
	printf("C2.5 bounded parallel two-cdev copy: PASS (%u rounds each)\n",
	       C25_PARALLEL_ROUNDS);
	return 0;
}

static int close_peer_while_base_survives(struct c25_session *base,
					  struct c25_session *peer)
{
	struct c25_observation base_before;
	int ret;

	ret = c25_capture(base, &base_before);
	if (ret)
		return ret;
	/* Deliberately close an attached peer; C2.4 froze close ordering. */
	ret = c25_session_close_device(peer);
	if (ret)
		return ret;
	ret = c25_session_unmap(peer);
	if (ret)
		return ret;
	ret = c25_session_destroy_ioas(peer);
	if (ret)
		return ret;
	ret = assert_observer_unchanged(base, &base_before);
	if (ret)
		return ret;
	ret = populate_from_ioas(base, 0xd1U, 0xd2U,
				 C25_INITIAL_OFFSET + 41U);
	if (ret)
		return ret;
	printf("C2.5 peer close/IOAS-destroy with base survivor: PASS\n");
	return 0;
}

static int run_two_instance(const char *base_path, const char *peer_path)
{
	struct c25_observation base_observation;
	struct c25_observation peer_observation;
	struct c25_owner owner = { .iommu_fd = -1 };
	struct c25_session base;
	struct c25_session peer;
	bool matrix_complete = false;
	int ret;

	memset(&base, 0, sizeof(base));
	memset(&peer, 0, sizeof(peer));
	base.device_fd = -1;
	peer.device_fd = -1;
	if (!strcmp(base_path, peer_path))
		return -EINVAL;
	ret = c25_owner_open(&owner);
	if (ret)
		goto out;
	ret = c25_session_begin(&base, &owner, "base", base_path,
				C25_SHARED_IOVA);
	if (ret)
		goto out;
	ret = c25_session_begin(&peer, &owner, "peer", peer_path,
				C25_SHARED_IOVA);
	if (ret)
		goto out;
	if (base.ioas_id == peer.ioas_id || base.page == peer.page)
		goto protocol_error;
	ret = c25_session_map(&base);
	if (ret)
		goto out;
	ret = c25_session_map(&peer);
	if (ret)
		goto out;
	ret = c25_session_attach(&base);
	if (ret)
		goto out;
	ret = c25_session_attach(&peer);
	if (ret)
		goto out;
	ret = verify_ready_state(&base);
	if (ret)
		goto out;
	ret = verify_ready_state(&peer);
	if (ret)
		goto out;
	printf("C2.5 topology: two cdevs, one iommufd, distinct IOAS=%" PRIu32
	       "/%" PRIu32 ", same IOVA=0x%" PRIx64 ": PASS\n",
	       base.ioas_id, peer.ioas_id, C25_SHARED_IOVA);

	ret = populate_from_ioas(&base, 0x11U, 0x12U,
				 C25_INITIAL_OFFSET);
	if (ret)
		goto out;
	ret = populate_from_ioas(&peer, 0xa1U, 0xa2U,
				 C25_INITIAL_OFFSET);
	if (ret)
		goto out;
	ret = c25_capture(&base, &base_observation);
	if (ret)
		goto out;
	ret = c25_capture(&peer, &peer_observation);
	if (ret)
		goto out;
	if (!memcmp(base_observation.data, peer_observation.data,
		    sizeof(base_observation.data)) ||
	    !memcmp(base_observation.page, peer_observation.page,
		    sizeof(base_observation.page)))
		goto protocol_error;
	printf("C2.5 distinct full-page/data patterns at equal IOVA: PASS\n");

	ret = copy_to_ioas_isolated(&base, &peer, 0x31U, 0x32U);
	if (ret)
		goto out;
	ret = copy_to_ioas_isolated(&peer, &base, 0xc1U, 0xc2U);
	if (ret)
		goto out;
	printf("C2.5 bidirectional data/sequence isolation: PASS\n");

	ret = exercise_nonterminal_isolation(&base, &peer, 0x50U);
	if (ret)
		goto out;
	ret = exercise_nonterminal_isolation(&peer, &base, 0xd0U);
	if (ret)
		goto out;
	ret = run_parallel_smoke(&base, &peer);
	if (ret)
		goto out;
	ret = close_peer_while_base_survives(&base, &peer);
	if (ret)
		goto out;
	matrix_complete = true;
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	{
		int cleanup_ret;

		cleanup_ret = c25_session_cleanup(&peer);
		if (!ret && cleanup_ret)
			ret = cleanup_ret;
		cleanup_ret = c25_session_cleanup(&base);
		if (!ret && cleanup_ret)
			ret = cleanup_ret;
		cleanup_ret = c25_owner_close(&owner);
		if (!ret && cleanup_ret)
			ret = cleanup_ret;
	}
	if (!ret && matrix_complete)
		printf("C2.5 two-instance real-provider oracle: PASS\n");
	return ret;
}

static int read_one_byte(void)
{
	unsigned char byte;
	ssize_t done;

	do {
		done = read(STDIN_FILENO, &byte, 1);
	} while (done < 0 && errno == EINTR);
	if (done < 0)
		return -errno;
	return done == 1 ? 0 : -EPIPE;
}

static int run_survivor_hold(const char *base_path)
{
	struct c25_owner owner = { .iommu_fd = -1 };
	struct c25_session base;
	struct c25_observation before;
	bool post_remove_complete = false;
	int ret;

	memset(&base, 0, sizeof(base));
	base.device_fd = -1;
	ret = c25_owner_open(&owner);
	if (ret)
		goto out;
	ret = c25_session_begin(&base, &owner, "base-survivor", base_path,
				C25_SHARED_IOVA);
	if (ret)
		goto out;
	ret = c25_session_map(&base);
	if (ret)
		goto out;
	ret = c25_session_attach(&base);
	if (ret)
		goto out;
	ret = populate_from_ioas(&base, 0x61U, 0x62U,
				 C25_INITIAL_OFFSET);
	if (ret)
		goto out;
	ret = c25_capture(&base, &before);
	if (ret)
		goto out;
	printf("C2.5 survivor READY\n");
	if (fflush(stdout)) {
		ret = -errno;
		goto out;
	}
	ret = read_one_byte();
	if (ret)
		goto out;
	ret = assert_observer_unchanged(&base, &before);
	if (ret)
		goto out;
	ret = populate_from_ioas(&base, 0x71U, 0x72U,
				 C25_INITIAL_OFFSET + 53U);
	if (ret)
		goto out;
	post_remove_complete = true;

out:
	{
		int cleanup_ret;

		cleanup_ret = c25_session_cleanup(&base);
		if (!ret && cleanup_ret)
			ret = cleanup_ret;
		cleanup_ret = c25_owner_close(&owner);
		if (!ret && cleanup_ret)
			ret = cleanup_ret;
	}
	if (!ret && post_remove_complete)
		printf("C2.5 survivor after peer remove: PASS\n");
	return ret;
}

static int report_failure(const char *label, int ret)
{
	if (ret >= 0)
		ret = -EIO;
	errno = -ret;
	perror(label);
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	int ret;

	if (argc == 2 && !strcmp(argv[1], "--selftest"))
		return c25_selftest() ? EXIT_FAILURE : EXIT_SUCCESS;
	if (argc == 3 && !strcmp(argv[1], "--hold-survivor")) {
		ret = run_survivor_hold(argv[2]);
		return ret ? report_failure("C2.5 survivor", ret) : EXIT_SUCCESS;
	}
	if (argc == 3) {
		ret = run_two_instance(argv[1], argv[2]);
		return ret ? report_failure("C2.5 two-instance", ret) :
			     EXIT_SUCCESS;
	}
	fprintf(stderr,
		"usage: %s --selftest | <base-cdev> <peer-cdev> | "
		"--hold-survivor <base-cdev>\n",
		argv[0]);
	return EXIT_FAILURE;
}
