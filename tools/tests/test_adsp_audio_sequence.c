#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "beryllium_adsp_glink_start.h"
#include "beryllium_adsp_audio_sequence.h"

static bool
begin_complete(struct beryllium_adsp_audio_sequence *sequence,
	enum beryllium_adsp_audio_command command)
{
	return beryllium_adsp_audio_sequence_begin(sequence, command,
	    BERYLLIUM_ADSP_AUDIO_NO_PERIOD) &&
	    beryllium_adsp_audio_sequence_complete(sequence, command, true);
}

static bool
start_writes(struct beryllium_adsp_audio_sequence *sequence)
{
	return begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ASM_MAP) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ASM_OPEN) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ADM_OPEN) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ADM_MATRIX) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_PCM_FORMAT) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_AFE_CLOCK) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_AFE_CONFIG) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_AFE_START) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ASM_RUN);
}

static bool
finish_route(struct beryllium_adsp_audio_sequence *sequence)
{
	return begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ASM_CLOSE) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ASM_UNMAP) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_ADM_CLOSE) &&
	    begin_complete(sequence, BERYLLIUM_ADSP_AUDIO_AFE_STOP);
}

int
main(void)
{
	const bool valid[] = { false, true, true, true, true, true, false, true };
	const bool pas[] = { false, false, true, true, true, true, false, true };
	const bool glink[] = { false, false, false, true, true, true, false, true };
	for (int mode = 1; mode <= 7; mode++) {
		if (beryllium_adsp_glink_mode_valid(mode) != valid[mode] ||
		    beryllium_adsp_glink_mode_requests_pas(mode) != pas[mode] ||
		    beryllium_adsp_glink_mode_requests_glink(mode) != glink[mode]) {
			fprintf(stderr, "mode %d flags changed unexpectedly\n", mode);
			return 1;
		}
	}

	struct beryllium_adsp_audio_sequence failed = { 0 };
	beryllium_adsp_audio_sequence_init(&failed);
	if (!beryllium_adsp_audio_sequence_begin(&failed,
	    BERYLLIUM_ADSP_AUDIO_ASM_MAP, BERYLLIUM_ADSP_AUDIO_NO_PERIOD) ||
	    beryllium_adsp_audio_sequence_complete(&failed,
	    BERYLLIUM_ADSP_AUDIO_ASM_MAP, false) || !failed.failed ||
	    beryllium_adsp_audio_sequence_begin(&failed,
	    BERYLLIUM_ADSP_AUDIO_ASM_OPEN, BERYLLIUM_ADSP_AUDIO_NO_PERIOD)) {
		fprintf(stderr, "failed APR response did not stop the sequence\n");
		return 2;
	}

	struct beryllium_adsp_audio_sequence mismatch = { 0 };
	beryllium_adsp_audio_sequence_init(&mismatch);
	if (!start_writes(&mismatch) || !beryllium_adsp_audio_sequence_begin(&mismatch,
	    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, 0) ||
	    !beryllium_adsp_audio_sequence_complete(&mismatch,
	    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, true) ||
	    !beryllium_adsp_audio_sequence_begin(&mismatch,
	    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, 1) ||
	    beryllium_adsp_audio_sequence_complete(&mismatch,
	    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, false) || !mismatch.failed ||
	    beryllium_adsp_audio_sequence_begin(&mismatch,
	    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, 2)) {
		fprintf(stderr, "mismatched WRITE_DONE allowed another period\n");
		return 3;
	}

	struct beryllium_adsp_audio_sequence sequence = { 0 };
	beryllium_adsp_audio_sequence_init(&sequence);
	if (!start_writes(&sequence)) {
		fprintf(stderr, "could not reach the bounded PCM write loop\n");
		return 4;
	}
	for (unsigned period = 0; period < BERYLLIUM_ADSP_ASM_PERIOD_COUNT; period++) {
		if (!beryllium_adsp_audio_sequence_begin(&sequence,
		    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, period) ||
		    !beryllium_adsp_audio_sequence_complete(&sequence,
		    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, true)) {
			fprintf(stderr, "period %u did not complete in sequence\n", period);
			return 5;
		}
	}
	struct beryllium_adsp_audio_sequence extra = sequence;
	if (sequence.writes_sent != BERYLLIUM_ADSP_ASM_PERIOD_COUNT ||
	    beryllium_adsp_audio_sequence_begin(&extra,
	    BERYLLIUM_ADSP_AUDIO_ASM_WRITE, 4) || !extra.failed || sequence.failed ||
	    !finish_route(&sequence) || !sequence.safe_to_restore ||
	    !beryllium_adsp_audio_sequence_restored(&sequence, true) ||
	    !sequence.complete || sequence.failed) {
		fprintf(stderr, "four-write success/restore gate failed\n");
		return 6;
	}
	puts("mode 7 sequence checks passed");
	return 0;
}
