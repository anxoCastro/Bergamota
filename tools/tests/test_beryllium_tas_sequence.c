#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "beryllium_tas_sequence.h"

struct fake {
	char events[64];
	unsigned event_count;
	uint32_t blocks[16];
	unsigned block_count;
	uint32_t fail_type;
	unsigned fail_block_at;
	char fail_event;
	bool ycrc_all;
};

static int
event(struct fake *fake, char value)
{
	if (fake->event_count < sizeof(fake->events))
		fake->events[fake->event_count++] = value;
	return fake->fail_event == value ? -1 : 0;
}

static int
set_reset(void *context, bool release)
{
	return event(context, release ? '1' : '0');
}

static int prepare_bus(void *context) { return event(context, 'B'); }
static int identify(void *context) { return event(context, 'I'); }
static int software_reset_defaults(void *context) { return event(context, 'S'); }
static int startup(void *context) { return event(context, 'U'); }
static int unmute(void *context) { return event(context, 'N'); }
static int mute(void *context) { return event(context, 'M'); }
static int shutdown(void *context) { return event(context, 'D'); }
static int restore_bus(void *context) { return event(context, 'T'); }

static int
load_block(void *context, const struct beryllium_tas_block *block, bool ycrc)
{
	struct fake *fake = context;
	fake->blocks[fake->block_count++] = block->type;
	fake->ycrc_all = fake->block_count == 1 ? ycrc : fake->ycrc_all && ycrc;
	return (fake->fail_block_at != 0 &&
	    fake->fail_block_at == fake->block_count) ||
	    (fake->fail_type != 0 && fake->fail_type == block->type) ? -1 : 0;
}

static const struct beryllium_tas_sequence_ops ops = {
	.set_reset = set_reset,
	.prepare_bus = prepare_bus,
	.identify = identify,
	.software_reset_defaults = software_reset_defaults,
	.load_block = load_block,
	.startup = startup,
	.unmute = unmute,
	.mute = mute,
	.shutdown = shutdown,
	.restore_bus = restore_bus,
};

static void
init_profile(struct beryllium_tas_firmware *firmware,
	struct beryllium_tas_calibration *calibration,
	struct beryllium_tas_block fw_blocks[7], struct beryllium_tas_block cal_block[1])
{
	static const uint32_t types[] = { 1, 8, 0, 4, 11, 3, 14 };
	for (unsigned i = 0; i < 7; ++i)
		fw_blocks[i].type = types[i];
	cal_block[0].type = 3;
	firmware->sampling_rate = 48000;
	firmware->program_zero.block_count = 2;
	firmware->program_zero.blocks[0] = fw_blocks[0];
	firmware->program_zero.blocks[1] = fw_blocks[1];
	firmware->pll_zero = fw_blocks[2];
	firmware->configuration_zero.block_count = 4;
	for (unsigned i = 0; i < 4; ++i)
		firmware->configuration_zero.blocks[i] = fw_blocks[i + 3];
	calibration->data.block_count = 1;
	calibration->data.blocks[0] = cal_block[0];
}

static int
test_success_order(void)
{
	struct fake fake = { 0 };
	struct beryllium_tas_sequence sequence;
	struct beryllium_tas_firmware firmware = { 0 };
	struct beryllium_tas_calibration calibration = { 0 };
	struct beryllium_tas_block fw_blocks[7] = { 0 }, cal_block[1] = { 0 };
	static const char expected[] = "0B1ISUNMD0T";
	init_profile(&firmware, &calibration, fw_blocks, cal_block);
	if (!beryllium_tas_sequence_init(&sequence, &ops, &fake) ||
	    !beryllium_tas_sequence_prepare(&sequence, &firmware, &calibration) ||
	    !beryllium_tas_sequence_start(&sequence) ||
	    !beryllium_tas_sequence_finish(&sequence)) {
		fputs("valid TAS profile sequence failed\n", stderr);
		return 1;
	}
	/* Compare structured operations; the seven block loads are verified below. */
	if (fake.event_count != strlen(expected) ||
	    memcmp(fake.events, expected, strlen(expected)) != 0 ||
	    fake.block_count != 8 || !fake.ycrc_all ||
	    fake.blocks[0] != 1 || fake.blocks[1] != 8 || fake.blocks[2] != 0 ||
	    fake.blocks[3] != 4 || fake.blocks[4] != 11 || fake.blocks[5] != 3 ||
	    fake.blocks[6] != 3 || fake.blocks[7] != 14) {
		fprintf(stderr, "unexpected event/block sequence: events=%u blocks=%u\n",
		    fake.event_count, fake.block_count);
		return 2;
	}
	return 0;
}

static int
test_missing_profile_fails_before_reset_or_bus(void)
{
	struct fake fake = { 0 };
	struct beryllium_tas_sequence sequence;
	struct beryllium_tas_firmware firmware = { .sampling_rate = 48000 };
	struct beryllium_tas_calibration calibration = { 0 };
	if (!beryllium_tas_sequence_init(&sequence, &ops, &fake) ||
	    beryllium_tas_sequence_prepare(&sequence, &firmware, &calibration) ||
	    fake.event_count != 0) {
		fputs("incomplete profile touched hardware before rejection\n", stderr);
		return 3;
	}
	return 0;
}

static int
test_setup_and_start_failures_reset_and_restore(void)
{
	struct beryllium_tas_firmware firmware = { 0 };
	struct beryllium_tas_calibration calibration = { 0 };
	struct beryllium_tas_block fw_blocks[7] = { 0 }, cal_block[1] = { 0 };
	init_profile(&firmware, &calibration, fw_blocks, cal_block);
	struct fake failed_load = { .fail_type = 11 };
	struct beryllium_tas_sequence first;
	if (!beryllium_tas_sequence_init(&first, &ops, &failed_load) ||
	    beryllium_tas_sequence_prepare(&first, &firmware, &calibration) ||
	    failed_load.events[failed_load.event_count - 2] != '0' ||
	    failed_load.events[failed_load.event_count - 1] != 'T') {
		fputs("profile-load failure did not assert reset and restore pins\n", stderr);
		return 4;
	}
	struct fake failed_start = { .fail_event = 'U' };
	struct beryllium_tas_sequence second;
	if (!beryllium_tas_sequence_init(&second, &ops, &failed_start) ||
	    !beryllium_tas_sequence_prepare(&second, &firmware, &calibration) ||
	    beryllium_tas_sequence_start(&second) ||
	    failed_start.events[failed_start.event_count - 4] != 'M' ||
	    failed_start.events[failed_start.event_count - 3] != 'D' ||
	    failed_start.events[failed_start.event_count - 2] != '0' ||
	    failed_start.events[failed_start.event_count - 1] != 'T') {
		fputs("startup failure did not mute/shutdown/reset/restore\n", stderr);
		return 5;
	}
	return 0;
}

static int
test_every_block_failure_fails_closed(void)
{
	struct beryllium_tas_firmware firmware = { 0 };
	struct beryllium_tas_calibration calibration = { 0 };
	struct beryllium_tas_block fw_blocks[7] = { 0 }, cal_block[1] = { 0 };
	init_profile(&firmware, &calibration, fw_blocks, cal_block);
	for (unsigned failed_block = 1; failed_block <= 8; ++failed_block) {
		struct fake fake = { .fail_block_at = failed_block };
		struct beryllium_tas_sequence sequence;
		if (!beryllium_tas_sequence_init(&sequence, &ops, &fake))
			return 7;
		bool prepared = beryllium_tas_sequence_prepare(&sequence,
		    &firmware, &calibration);
		if (failed_block <= 7) {
			if (prepared || fake.events[fake.event_count - 2] != '0' ||
			    fake.events[fake.event_count - 1] != 'T' ||
			    sequence.powered || !sequence.failed) {
				fprintf(stderr, "profile block %u failure was not reset/restored\n",
				    failed_block);
				return 8;
			}
		} else if (!prepared || beryllium_tas_sequence_start(&sequence) ||
		    fake.events[fake.event_count - 4] != 'M' ||
		    fake.events[fake.event_count - 3] != 'D' ||
		    fake.events[fake.event_count - 2] != '0' ||
		    fake.events[fake.event_count - 1] != 'T' || !sequence.failed) {
			fputs("post-power-up block failure was not muted/shutdown/reset/restored\n",
			    stderr);
			return 9;
		}
	}
	return 0;
}

static int
test_bus_and_reset_failures_restore_state(void)
{
	static const char injected[] = { 'B', '1', 'I', 'S' };
	struct beryllium_tas_firmware firmware = { 0 };
	struct beryllium_tas_calibration calibration = { 0 };
	struct beryllium_tas_block fw_blocks[7] = { 0 }, cal_block[1] = { 0 };
	init_profile(&firmware, &calibration, fw_blocks, cal_block);
	for (unsigned i = 0; i < sizeof(injected); ++i) {
		struct fake fake = { .fail_event = injected[i] };
		struct beryllium_tas_sequence sequence;
		if (!beryllium_tas_sequence_init(&sequence, &ops, &fake) ||
		    beryllium_tas_sequence_prepare(&sequence, &firmware, &calibration) ||
		    fake.events[fake.event_count - 2] != '0' ||
		    fake.events[fake.event_count - 1] != 'T') {
			fprintf(stderr, "bus/reset prerequisite failure %c did not clean up\n",
			    injected[i]);
			return 10;
		}
	}
	return 0;
}

static int
test_abort_after_route_failure_mutes_before_shutdown(void)
{
	struct fake fake = { 0 };
	struct beryllium_tas_sequence sequence;
	struct beryllium_tas_firmware firmware = { 0 };
	struct beryllium_tas_calibration calibration = { 0 };
	struct beryllium_tas_block fw_blocks[7] = { 0 }, cal_block[1] = { 0 };
	init_profile(&firmware, &calibration, fw_blocks, cal_block);
	if (!beryllium_tas_sequence_init(&sequence, &ops, &fake) ||
	    !beryllium_tas_sequence_prepare(&sequence, &firmware, &calibration) ||
	    !beryllium_tas_sequence_start(&sequence) ||
	    !beryllium_tas_sequence_abort(&sequence) ||
	    fake.events[fake.event_count - 4] != 'M' ||
	    fake.events[fake.event_count - 3] != 'D' ||
	    fake.events[fake.event_count - 2] != '0' ||
	    fake.events[fake.event_count - 1] != 'T') {
		fputs("route failure abort order is unsafe\n", stderr);
		return 6;
	}
	return 0;
}

int
main(void)
{
	if (test_success_order() || test_missing_profile_fails_before_reset_or_bus() ||
	    test_setup_and_start_failures_reset_and_restore() ||
	    test_every_block_failure_fails_closed() ||
	    test_bus_and_reset_failures_restore_state() ||
	    test_abort_after_route_failure_mutes_before_shutdown())
		return 1;
	puts("TAS ordered sequence checks passed");
	return 0;
}
