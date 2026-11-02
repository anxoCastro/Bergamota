#include <stdint.h>
#include <stdio.h>

#include "beryllium_smp2p_negotiation.h"

static void
expect_header(uint32_t input, uint32_t remote, enum beryllium_smp2p_action expected_action,
    uint32_t expected_header)
{
	uint32_t local = input;
	enum beryllium_smp2p_action action =
	    beryllium_smp2p_negotiate_header(&local, remote);
	if (action != expected_action || local != expected_header) {
		fprintf(stderr, "input=%#x remote=%#x action=%u local=%#x expected_action=%u expected=%#x\n",
		    input, remote, action, local, expected_action, expected_header);
		__builtin_trap();
	}
}

int
main(void)
{
	expect_header(0x00000102u, 0x00000101u, BERYLLIUM_SMP2P_DOWNGRADE, 0x00000101u);
	expect_header(0x00000101u, 0x00000001u, BERYLLIUM_SMP2P_FEATURES_CHANGED, 0x00000001u);
	expect_header(0x00000001u, 0x00000001u, BERYLLIUM_SMP2P_COMPLETE, 0x00000001u);
	expect_header(0x00000001u, 0x00000102u, BERYLLIUM_SMP2P_WAIT_REMOTE, 0x00000001u);
	expect_header(0x00000102u, 0x00000000u, BERYLLIUM_SMP2P_INVALID, 0x00000102u);
	expect_header(0x00000102u, 0x00000103u, BERYLLIUM_SMP2P_INVALID, 0x00000102u);
	expect_header(0x00000100u, 0x00000101u, BERYLLIUM_SMP2P_INVALID, 0x00000100u);
	expect_header(0x00000103u, 0x00000101u, BERYLLIUM_SMP2P_INVALID, 0x00000103u);
	puts("SMP2P negotiation transitions passed");
	return 0;
}
