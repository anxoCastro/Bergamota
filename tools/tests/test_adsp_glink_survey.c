#include <assert.h>
#include <stdio.h>

#include "beryllium_adsp_glink_survey.h"

int
main(void)
{
	struct beryllium_adsp_glink_indices a = { 0, 0, 0, 0 };
	struct beryllium_adsp_glink_indices b = { 0, 0, 0, 0 };

	assert(!beryllium_adsp_glink_descriptor_size_valid(0));
	assert(!beryllium_adsp_glink_descriptor_size_valid(16));
	assert(!beryllium_adsp_glink_descriptor_size_valid(31));
	assert(beryllium_adsp_glink_descriptor_size_valid(32));
	assert(!beryllium_adsp_glink_descriptor_size_valid(33));

	assert(beryllium_adsp_glink_check_index(0, false, 0) ==
	    BERYLLIUM_ADSP_GLINK_INDEX_UNCHECKED_FIFO_MISSING);
	assert(beryllium_adsp_glink_check_index(99, false, 16384) ==
	    BERYLLIUM_ADSP_GLINK_INDEX_UNCHECKED_FIFO_MISSING);
	assert(beryllium_adsp_glink_check_index(0, true, 16384) ==
	    BERYLLIUM_ADSP_GLINK_INDEX_VALID);
	assert(beryllium_adsp_glink_check_index(16383, true, 16384) ==
	    BERYLLIUM_ADSP_GLINK_INDEX_VALID);
	assert(beryllium_adsp_glink_check_index(16384, true, 16384) ==
	    BERYLLIUM_ADSP_GLINK_INDEX_OUT_OF_RANGE);
	assert(beryllium_adsp_glink_check_index(16385, true, 16384) ==
	    BERYLLIUM_ADSP_GLINK_INDEX_OUT_OF_RANGE);
	assert(beryllium_adsp_glink_check_index(0, true, 0) ==
	    BERYLLIUM_ADSP_GLINK_INDEX_OUT_OF_RANGE);

	assert(beryllium_adsp_glink_indices_equal(&a, &b));
	b.tx_tail = 1;
	assert(!beryllium_adsp_glink_indices_equal(&a, &b));
	b.tx_tail = 0;
	b.tx_head = 1;
	assert(!beryllium_adsp_glink_indices_equal(&a, &b));
	b.tx_head = 0;
	b.rx_tail = 1;
	assert(!beryllium_adsp_glink_indices_equal(&a, &b));
	b.rx_tail = 0;
	b.rx_head = 1;
	assert(!beryllium_adsp_glink_indices_equal(&a, &b));

	puts("GLINK survey helper checks passed");
	return 0;
}
