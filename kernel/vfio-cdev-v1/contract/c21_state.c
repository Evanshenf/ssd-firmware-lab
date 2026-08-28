// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: GPL-2.0-only

#include "c21_state.h"

static void fwlab_c21_lock(struct fwlab_c21_device *device)
{
	device->lock_ops->lock(device->lock_context);
}

static void fwlab_c21_unlock(struct fwlab_c21_device *device)
{
	device->lock_ops->unlock(device->lock_context);
}

static bool fwlab_c21_is_open(c21_u16 state)
{
	return state == FWLAB_C21_STATE_OPEN_UNATTACHED ||
	       state == FWLAB_C21_STATE_OPEN_ATTACHED;
}

static void fwlab_c21_clear_session_locked(struct fwlab_c21_device *device)
{
	memset(&device->current_request, 0, sizeof(device->current_request));
	memset(&device->result, 0, sizeof(device->result));
	memset(device->data, 0, sizeof(device->data));
	device->last_sequence = 0;
	device->result_valid = false;
	device->sequence_exhausted = false;
}

static int fwlab_c21_advance_generation_locked(
	struct fwlab_c21_device *device)
{
	if (device->generation == C21_U64_MAX) {
		fwlab_c21_clear_session_locked(device);
		device->state = FWLAB_C21_STATE_DEAD;
		return -EOVERFLOW;
	}
	device->generation++;
	fwlab_c21_clear_session_locked(device);
	return 0;
}

static int fwlab_c21_validate_device(const struct fwlab_c21_device *device)
{
	return device && device->initialized ? 0 : -EINVAL;
}

static int fwlab_c21_normalize_errno(int value)
{
	if (value > 0 || value < -(int)FWLAB_C21_MAX_ERRNO)
		return -EPROTO;
	return value;
}

int fwlab_c21_device_init(struct fwlab_c21_device *device,
			  const struct fwlab_c21_lock_ops *lock_ops,
			  void *lock_context,
			  const struct fwlab_c21_copy_provider *provider)
{
	if (!device || !lock_ops || !lock_ops->lock || !lock_ops->unlock ||
	    !provider || !provider->ops || !provider->ops->ioas_to_buffer ||
	    !provider->ops->buffer_to_ioas)
		return -EINVAL;

	memset(device, 0, sizeof(*device));
	device->lock_ops = lock_ops;
	device->lock_context = lock_context;
	device->provider = *provider;
	device->state = FWLAB_C21_STATE_CLOSED;
	device->initialized = true;
	return 0;
}

int fwlab_c21_device_open(struct fwlab_c21_device *device)
{
	int ret;

	if (fwlab_c21_validate_device(device))
		return -EINVAL;
	fwlab_c21_lock(device);
	if (device->state == FWLAB_C21_STATE_DEAD)
		ret = -ESHUTDOWN;
	else if (device->state != FWLAB_C21_STATE_CLOSED)
		ret = -EBUSY;
	else {
		ret = fwlab_c21_advance_generation_locked(device);
		if (!ret)
			device->state = FWLAB_C21_STATE_OPEN_UNATTACHED;
	}
	fwlab_c21_unlock(device);
	return ret;
}

int fwlab_c21_device_transition(struct fwlab_c21_device *device,
				enum fwlab_c21_transition transition,
				fwlab_c21_transition_fn transition_fn,
				void *transition_context)
{
	c21_u16 target_state;
	int ret;

	if (fwlab_c21_validate_device(device) || !transition_fn)
		return -EINVAL;
	fwlab_c21_lock(device);
	if (device->state == FWLAB_C21_STATE_DEAD) {
		ret = -ESHUTDOWN;
		goto out_unlock;
	}
	switch (transition) {
	case FWLAB_C21_TRANSITION_ATTACH:
		if (device->state != FWLAB_C21_STATE_OPEN_UNATTACHED) {
			ret = -EINVAL;
			goto out_unlock;
		}
		target_state = FWLAB_C21_STATE_OPEN_ATTACHED;
		break;
	case FWLAB_C21_TRANSITION_REPLACE:
		if (device->state != FWLAB_C21_STATE_OPEN_ATTACHED) {
			ret = -EINVAL;
			goto out_unlock;
		}
		target_state = FWLAB_C21_STATE_OPEN_ATTACHED;
		break;
	case FWLAB_C21_TRANSITION_DETACH:
		if (device->state != FWLAB_C21_STATE_OPEN_ATTACHED) {
			ret = -EINVAL;
			goto out_unlock;
		}
		target_state = FWLAB_C21_STATE_OPEN_UNATTACHED;
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	/* Never perform an adjacent transition if its identity cannot advance. */
	if (device->generation == C21_U64_MAX) {
		fwlab_c21_clear_session_locked(device);
		device->state = FWLAB_C21_STATE_DEAD;
		ret = -EOVERFLOW;
		goto out_unlock;
	}
	ret = fwlab_c21_normalize_errno(
		transition_fn(transition_context, transition));
	if (ret)
		goto out_unlock;
	ret = fwlab_c21_advance_generation_locked(device);
	if (!ret)
		device->state = target_state;

out_unlock:
	fwlab_c21_unlock(device);
	return ret;
}

int fwlab_c21_device_reset(struct fwlab_c21_device *device)
{
	c21_u16 old_state;
	int ret;

	if (fwlab_c21_validate_device(device))
		return -EINVAL;
	fwlab_c21_lock(device);
	old_state = device->state;
	if (!fwlab_c21_is_open(old_state))
		ret = old_state == FWLAB_C21_STATE_DEAD ? -ESHUTDOWN : -EINVAL;
	else {
		ret = fwlab_c21_advance_generation_locked(device);
		if (!ret)
			device->state = old_state;
	}
	fwlab_c21_unlock(device);
	return ret;
}

int fwlab_c21_device_close(struct fwlab_c21_device *device)
{
	int ret;

	if (fwlab_c21_validate_device(device))
		return -EINVAL;
	fwlab_c21_lock(device);
	if (!fwlab_c21_is_open(device->state))
		ret = device->state == FWLAB_C21_STATE_DEAD ? -ESHUTDOWN : -EINVAL;
	else {
		device->state = FWLAB_C21_STATE_CLOSING;
		ret = fwlab_c21_advance_generation_locked(device);
		if (!ret)
			device->state = FWLAB_C21_STATE_CLOSED;
	}
	fwlab_c21_unlock(device);
	return ret;
}

static int fwlab_c21_check_submit_state_locked(
	struct fwlab_c21_device *device,
	const struct fwlab_c21_request *request)
{
	if (device->state == FWLAB_C21_STATE_DEAD ||
	    device->state == FWLAB_C21_STATE_CLOSING ||
	    device->state == FWLAB_C21_STATE_CLOSED)
		return -ESHUTDOWN;
	if (device->state != FWLAB_C21_STATE_OPEN_ATTACHED)
		return -ENOTCONN;
	if (request->expected_generation != device->generation)
		return -ESTALE;
	if (device->sequence_exhausted || device->last_sequence == C21_U64_MAX)
		return -EOVERFLOW;
	if (request->sequence <= device->last_sequence)
		return -EALREADY;
	if (request->sequence != device->last_sequence + 1U)
		return -ERANGE;
	return 0;
}

int fwlab_c21_control_write(struct fwlab_c21_device *device,
			    c21_u32 offset, const unsigned char *wire,
			    size_t wire_size)
{
	unsigned char snapshot[FWLAB_C21_RECORD_SIZE];
	unsigned char scratch[FWLAB_C21_MAX_COPY_LENGTH];
	struct fwlab_c21_request request;
	struct fwlab_c21_result result;
	int op_ret;
	int ret;

	if (fwlab_c21_validate_device(device))
		return -EINVAL;
	if (offset != FWLAB_C21_SUBMIT_OFFSET)
		return -EINVAL;
	if (wire_size != FWLAB_C21_RECORD_SIZE)
		return -EMSGSIZE;
	if (!wire)
		return -EFAULT;
	memcpy(snapshot, wire, sizeof(snapshot));
	ret = fwlab_c21_decode_request(snapshot, sizeof(snapshot), &request);
	if (ret)
		return ret;

	fwlab_c21_lock(device);
	ret = fwlab_c21_check_submit_state_locked(device, &request);
	if (ret)
		goto out_unlock;

	device->last_sequence = request.sequence;
	device->sequence_exhausted = request.sequence == C21_U64_MAX;
	device->current_request = request;
	memset(&result, 0, sizeof(result));
	result.operation = request.operation;
	result.flags = FWLAB_C21_RES_F_VALID;
	result.sequence = request.sequence;
	result.generation = device->generation;
	result.iova = request.iova;
	result.requested_length = request.length;

	if (request.operation == FWLAB_C21_OP_COPY_IOAS_TO_BUFFER) {
		memset(scratch, 0, sizeof(scratch));
		op_ret = device->provider.ops->ioas_to_buffer(
			device->provider.context, request.iova, scratch,
			request.length);
		op_ret = fwlab_c21_normalize_errno(op_ret);
		if (!op_ret)
			memcpy(device->data, scratch, request.length);
	} else {
		op_ret = device->provider.ops->buffer_to_ioas(
			device->provider.context, request.iova, device->data,
			request.length);
		op_ret = fwlab_c21_normalize_errno(op_ret);
		if (op_ret)
			result.flags |=
				FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL;
	}
	result.op_errno = op_ret;
	device->result = result;
	device->result_valid = true;
	ret = FWLAB_C21_RECORD_SIZE;

out_unlock:
	fwlab_c21_unlock(device);
	return ret;
}

static void fwlab_c21_snapshot_state_locked(
	const struct fwlab_c21_device *device,
	struct fwlab_c21_state_snapshot *state)
{
	memset(state, 0, sizeof(*state));
	state->device_state = device->state;
	state->generation = device->generation;
	state->last_sequence = device->last_sequence;
	state->max_copy_length = FWLAB_C21_MAX_COPY_LENGTH;
	state->data_region_size = FWLAB_C21_DATA_REGION_SIZE;
	if (fwlab_c21_is_open(device->state))
		state->flags |= FWLAB_C21_ST_F_OPEN;
	if (device->state == FWLAB_C21_STATE_OPEN_ATTACHED)
		state->flags |= FWLAB_C21_ST_F_ATTACHED;
	if (device->result_valid)
		state->flags |= FWLAB_C21_ST_F_RESULT_VALID;
	if (device->sequence_exhausted)
		state->flags |= FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED;
	if (device->state == FWLAB_C21_STATE_DEAD)
		state->flags |= FWLAB_C21_ST_F_DEAD;
	if (!device->sequence_exhausted &&
	    device->last_sequence != C21_U64_MAX)
		state->next_sequence = device->last_sequence + 1U;
}

int fwlab_c21_control_read(struct fwlab_c21_device *device, c21_u32 offset,
			   unsigned char *wire, size_t wire_size)
{
	struct fwlab_c21_state_snapshot state;
	int ret = FWLAB_C21_RECORD_SIZE;

	if (fwlab_c21_validate_device(device))
		return -EINVAL;
	if (!wire)
		return -EFAULT;
	if (wire_size != FWLAB_C21_RECORD_SIZE)
		return -EMSGSIZE;
	if (offset != FWLAB_C21_RESULT_OFFSET &&
	    offset != FWLAB_C21_STATE_OFFSET)
		return -EINVAL;

	fwlab_c21_lock(device);
	if (offset == FWLAB_C21_RESULT_OFFSET) {
		if (!device->result_valid)
			ret = -ENODATA;
		else
			fwlab_c21_encode_result(wire, &device->result);
	} else {
		fwlab_c21_snapshot_state_locked(device, &state);
		fwlab_c21_encode_state(wire, &state);
	}
	fwlab_c21_unlock(device);
	return ret;
}

static int fwlab_c21_data_access_allowed_locked(
	const struct fwlab_c21_device *device)
{
	if (device->state == FWLAB_C21_STATE_DEAD ||
	    device->state == FWLAB_C21_STATE_CLOSING ||
	    device->state == FWLAB_C21_STATE_CLOSED)
		return -ESHUTDOWN;
	return 0;
}

int fwlab_c21_data_write(struct fwlab_c21_device *device, c21_u32 offset,
			 const unsigned char *source, size_t count)
{
	size_t available;
	int ret;

	if (fwlab_c21_validate_device(device))
		return -EINVAL;
	if (!count)
		return 0;
	if (!source)
		return -EFAULT;
	fwlab_c21_lock(device);
	ret = fwlab_c21_data_access_allowed_locked(device);
	if (ret)
		goto out_unlock;
	if (offset >= FWLAB_C21_DATA_REGION_SIZE) {
		ret = 0;
		goto out_unlock;
	}
	available = FWLAB_C21_DATA_REGION_SIZE - offset;
	if (count > available)
		count = available;
	memcpy(device->data + offset, source, count);
	ret = (int)count;
out_unlock:
	fwlab_c21_unlock(device);
	return ret;
}

int fwlab_c21_data_read(struct fwlab_c21_device *device, c21_u32 offset,
			unsigned char *destination, size_t count)
{
	size_t available;
	int ret;

	if (fwlab_c21_validate_device(device))
		return -EINVAL;
	if (!count)
		return 0;
	if (!destination)
		return -EFAULT;
	fwlab_c21_lock(device);
	ret = fwlab_c21_data_access_allowed_locked(device);
	if (ret)
		goto out_unlock;
	if (offset >= FWLAB_C21_DATA_REGION_SIZE) {
		ret = 0;
		goto out_unlock;
	}
	available = FWLAB_C21_DATA_REGION_SIZE - offset;
	if (count > available)
		count = available;
	memcpy(destination, device->data + offset, count);
	ret = (int)count;
out_unlock:
	fwlab_c21_unlock(device);
	return ret;
}
