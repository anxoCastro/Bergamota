#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "beryllium_adsp_glink_start.h"

int
main(void)
{
	struct beryllium_adsp_glink_indices zero = { 0, 0, 0, 0 };
	struct beryllium_adsp_glink_indices nonzero = { 0, 8, 0, 0 };
	struct beryllium_adsp_glink_indices unaligned = { 0, 1, 0, 0 };
	struct beryllium_adsp_glink_indices out_of_range = { 16384, 0, 0, 0 };
	struct beryllium_adsp_glink_message decoded = { 0, 0, 0 };
	uint8_t wire[8];
	uint32_t available = 0, next = 0;
	uint32_t used = 0;

	assert(beryllium_adsp_glink_mode_valid(1));
	assert(beryllium_adsp_glink_mode_valid(2));
	assert(beryllium_adsp_glink_mode_valid(3));
	assert(!beryllium_adsp_glink_mode_valid(0));
	assert(!beryllium_adsp_glink_mode_valid(4));
	assert(!beryllium_adsp_glink_mode_requests_pas(1));
	assert(beryllium_adsp_glink_mode_requests_pas(2));
	assert(beryllium_adsp_glink_mode_requests_pas(3));
	assert(!beryllium_adsp_glink_mode_requests_glink(2));
	assert(beryllium_adsp_glink_mode_requests_glink(3));

	assert(!beryllium_adsp_glink_tx_fifo_size_valid(0));
	assert(!beryllium_adsp_glink_tx_fifo_size_valid(23));
	assert(!beryllium_adsp_glink_tx_fifo_size_valid(25));
	assert(beryllium_adsp_glink_tx_fifo_size_valid(16384));
	assert(!beryllium_adsp_glink_rx_fifo_size_valid(0));
	assert(!beryllium_adsp_glink_rx_fifo_size_valid(7));
	assert(!beryllium_adsp_glink_rx_fifo_size_valid(9));
	assert(beryllium_adsp_glink_rx_fifo_size_valid(16384));

	assert(beryllium_adsp_glink_cold_indices_valid(&zero, 16384, 16384));
	assert(beryllium_adsp_glink_indices_valid(&nonzero, 16384, 16384));
	assert(!beryllium_adsp_glink_cold_indices_valid(&nonzero, 16384, 16384));
	assert(!beryllium_adsp_glink_indices_valid(&unaligned, 16384, 16384));
	assert(!beryllium_adsp_glink_cold_indices_valid(&unaligned, 16384, 16384));
	assert(!beryllium_adsp_glink_cold_indices_valid(&out_of_range, 16384, 16384));
	assert(!beryllium_adsp_glink_cold_indices_valid(&zero, 0, 16384));
	assert(!beryllium_adsp_glink_cold_indices_valid(&zero, 16384, 0));

	assert(beryllium_adsp_glink_ring_available(0, 0, 16384, &available));
	assert(available == 16368);
	assert(beryllium_adsp_glink_ring_available(16376, 0, 16384, &available));
	assert(available == 0);
	assert(beryllium_adsp_glink_ring_available(16376, 8, 16384, &available));
	assert(available == 0);
	assert(!beryllium_adsp_glink_ring_available(16384, 0, 16384, &available));
	assert(beryllium_adsp_glink_ring_used(24, 8, 16384, &used));
	assert(used == 16);
	assert(beryllium_adsp_glink_ring_used(8, 16376, 16384, &used));
	assert(used == 16);
	assert(!beryllium_adsp_glink_ring_used(16384, 0, 16384, &used));
	assert(beryllium_adsp_glink_ring_advance(16376, 16, 16384, &next));
	assert(next == 8);
	assert(!beryllium_adsp_glink_ring_advance(0, 16385, 16384, &next));

	beryllium_adsp_glink_encode(wire, BERYLLIUM_ADSP_GLINK_COMMAND_VERSION, 1, 0);
	assert(wire[0] == 0 && wire[1] == 0 && wire[2] == 1 && wire[3] == 0);
	assert(beryllium_adsp_glink_decode(wire, sizeof(wire), &decoded) ==
	    BERYLLIUM_ADSP_GLINK_PACKET_VERSION);
	assert(decoded.command == BERYLLIUM_ADSP_GLINK_COMMAND_VERSION);
	assert(decoded.version == 1 && decoded.features == 0);

	beryllium_adsp_glink_encode(wire, BERYLLIUM_ADSP_GLINK_COMMAND_VERSION_ACK, 1, 0x21);
	assert(beryllium_adsp_glink_decode(wire, sizeof(wire), &decoded) ==
	    BERYLLIUM_ADSP_GLINK_PACKET_VERSION_ACK);
	assert(decoded.features == 0x21);
	assert(beryllium_adsp_glink_decode(wire, sizeof(wire) - 1, &decoded) ==
	    BERYLLIUM_ADSP_GLINK_PACKET_MALFORMED);

	beryllium_adsp_glink_encode(wire, 15, 1, 0);
	assert(beryllium_adsp_glink_decode(wire, sizeof(wire), &decoded) ==
	    BERYLLIUM_ADSP_GLINK_PACKET_UNSUPPORTED);
	beryllium_adsp_glink_encode(wire, BERYLLIUM_ADSP_GLINK_COMMAND_VERSION, 2, 0);
	assert(beryllium_adsp_glink_decode(wire, sizeof(wire), &decoded) ==
	    BERYLLIUM_ADSP_GLINK_PACKET_UNSUPPORTED);

	puts("GLINK mode-3 helper checks passed");
	return 0;
}
