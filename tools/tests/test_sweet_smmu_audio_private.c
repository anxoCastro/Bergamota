#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sweet_smmu_audio_private.h"

#define FAKE_MMIO_BYTES 0x80000u
#define FAKE_WRITE_CAPACITY 256u

struct fake_write {
	uint32_t offset;
	uint32_t value;
};

struct fake_mmio {
	uint32_t regs[FAKE_MMIO_BYTES / sizeof(uint32_t)];
	struct fake_write writes[FAKE_WRITE_CAPACITY];
	size_t write_count;
	uint32_t ignored_write_offset;
	bool ignore_write;
};

static uint32_t
fake_read(void *context, uint32_t offset)
{
	struct fake_mmio *mmio = context;
	assert(offset < FAKE_MMIO_BYTES && offset % sizeof(uint32_t) == 0);
	return mmio->regs[offset / sizeof(uint32_t)];
}

static void
fake_write(void *context, uint32_t offset, uint32_t value)
{
	struct fake_mmio *mmio = context;
	assert(offset < FAKE_MMIO_BYTES && offset % sizeof(uint32_t) == 0);
	assert(mmio->write_count < FAKE_WRITE_CAPACITY);
	mmio->writes[mmio->write_count++] = (struct fake_write){ offset, value };
	if (!mmio->ignore_write || offset != mmio->ignored_write_offset) {
		mmio->regs[offset / sizeof(uint32_t)] = value;
	}
}

static void
fake_write64(void *context, uint32_t offset, uint64_t value)
{
	fake_write(context, offset, (uint32_t)value);
	fake_write(context, offset + 4u, (uint32_t)(value >> 32));
}

static struct sweet_smmu_audio_io
fake_io(struct fake_mmio *mmio)
{
	return (struct sweet_smmu_audio_io){ mmio, fake_read, fake_write, fake_write64 };
}

static void
fake_init(struct fake_mmio *mmio)
{
	memset(mmio, 0, sizeof(*mmio));
	mmio->regs[SWEET_SMMU_AUDIO_ID0 / 4] = 76;
	mmio->regs[SWEET_SMMU_AUDIO_ID1 / 4] = 0x5000002d;
	mmio->regs[SWEET_SMMU_AUDIO_SCR0 / 4] = 0x210406;
	for (unsigned i = 0; i < 76; i++) {
		mmio->regs[SWEET_SMMU_AUDIO_S2CR(i) / 4] = SWEET_SMMU_AUDIO_S2CR_BYPASS;
	}
	/* Existing non-audio use: retain context bank 3 as owned by its S2CR. */
	mmio->regs[SWEET_SMMU_AUDIO_S2CR(10) / 4] = 3;
	mmio->regs[SWEET_SMMU_AUDIO_SMR(5) / 4] = SWEET_SMMU_AUDIO_SMR_VALID | 0x740;
	mmio->regs[SWEET_SMMU_AUDIO_SMR(0) / 4] = 0x123;
	/* Disabled candidate bank has nonzero stale config; it must be saved/restored. */
	mmio->regs[SWEET_SMMU_AUDIO_CBAR(0) / 4] = 0x13579;
	mmio->regs[SWEET_SMMU_AUDIO_CBA2R(0) / 4] = 0x2468a;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_SCTLR) / 4] = 2;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_ACTLR) / 4] = 0x12345678;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TCR2) / 4] = 0x89abcdef;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR0) / 4] = 0x11111000;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR0 + 4) / 4] = 1;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR1) / 4] = 0x22222000;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR1 + 4) / 4] = 2;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TCR) / 4] = 0x31415926;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_CONTEXTIDR) / 4] = 0x55;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_MAIR0) / 4] = 0x444;
	mmio->regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_MAIR1) / 4] = 0x555;
}

static void
test_exact_masked_and_duplicate_sid_matches_are_rejected(void)
{
	struct fake_mmio mmio;
	struct sweet_smmu_audio_mapping mapping;
	struct sweet_smmu_audio_io io;

	fake_init(&mmio);
	io = fake_io(&mmio);
	mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] = SWEET_SMMU_AUDIO_SMR_VALID | 0x1821;
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_EEXIST);
	assert(mmio.write_count == 0);

	fake_init(&mmio);
	io = fake_io(&mmio);
	mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] = SWEET_SMMU_AUDIO_SMR_VALID | (0x21u << 16) | 0x1800;
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_EEXIST);
	assert(mmio.write_count == 0);

	fake_init(&mmio);
	io = fake_io(&mmio);
	mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] = SWEET_SMMU_AUDIO_SMR_VALID | 0x1821;
	mmio.regs[SWEET_SMMU_AUDIO_SMR(1) / 4] = SWEET_SMMU_AUDIO_SMR_VALID | 0x1821;
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_EEXIST);
	assert(mmio.write_count == 0);
}

static void
test_no_invalid_smr_and_no_unowned_context_bank_are_rejected(void)
{
	struct fake_mmio mmio;
	struct sweet_smmu_audio_mapping mapping;
	struct sweet_smmu_audio_io io;

	fake_init(&mmio);
	io = fake_io(&mmio);
	for (unsigned i = 0; i < 76; i++) {
		mmio.regs[SWEET_SMMU_AUDIO_SMR(i) / 4] = SWEET_SMMU_AUDIO_SMR_VALID | i;
	}
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_ENOSPC);
	assert(mmio.write_count == 0);

	fake_init(&mmio);
	io = fake_io(&mmio);
	for (unsigned i = 0; i < 76; i++) {
		mmio.regs[SWEET_SMMU_AUDIO_S2CR(i) / 4] = i % 45;
	}
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_ENOSPC);
	assert(mmio.write_count == 0);

	fake_init(&mmio);
	io = fake_io(&mmio);
	for (unsigned i = 0; i < 76; i++) {
		mmio.regs[SWEET_SMMU_AUDIO_S2CR(i) / 4] = i == 0 ? SWEET_SMMU_AUDIO_S2CR_BYPASS : (i % 44) + 1;
	}
	mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_SCTLR) / 4] = 3;
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_ENOSPC);
	assert(mmio.write_count == 0);
}

static void
test_install_reprobes_sid_immediately_before_mutation(void)
{
	struct fake_mmio mmio;
	struct sweet_smmu_audio_mapping mapping;
	struct sweet_smmu_audio_io io;

	fake_init(&mmio);
	io = fake_io(&mmio);
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_OK);
	mmio.regs[SWEET_SMMU_AUDIO_SMR(1) / 4] = SWEET_SMMU_AUDIO_SMR_VALID | 0x1821;
	assert(sweet_smmu_audio_install(&io, 0x1821, &mapping, 0x500000) == SWEET_SMMU_AUDIO_EEXIST);
	assert(mmio.write_count == 0);
}

static void
test_install_reports_context_bank_tlb_timeout_without_mapping_sid(void)
{
	struct fake_mmio mmio;
	struct sweet_smmu_audio_mapping mapping;
	struct sweet_smmu_audio_io io;
	enum sweet_smmu_audio_map_stage stage = SWEET_SMMU_AUDIO_MAP_STAGE_COMPLETE;

	fake_init(&mmio);
	io = fake_io(&mmio);
	const uint32_t saved_s2cr = mmio.regs[SWEET_SMMU_AUDIO_S2CR(0) / 4];
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_OK);
	mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TLBSTATUS) / 4] = 1;
	assert(sweet_smmu_audio_install_diagnostic(&io, 0x1821, &mapping,
	    0x500000, &stage, NULL) == SWEET_SMMU_AUDIO_EIO);
	assert(stage == SWEET_SMMU_AUDIO_MAP_STAGE_CONTEXT_BANK_TLB_SYNC);
	assert(strcmp(sweet_smmu_audio_map_stage_name(stage), "context-bank-tlb-sync") == 0);
	assert(mapping.uncertain && !mapping.installed);
	assert(mmio.regs[SWEET_SMMU_AUDIO_S2CR(0) / 4] == saved_s2cr);
	assert(mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] == 0x123);
}

static void
test_install_reports_first_context_bank_readback_mismatch(void)
{
	struct fake_mmio mmio;
	struct sweet_smmu_audio_mapping mapping;
	struct sweet_smmu_audio_io io;
	struct sweet_smmu_audio_readback readback = { 0 };
	enum sweet_smmu_audio_map_stage stage = SWEET_SMMU_AUDIO_MAP_STAGE_COMPLETE;

	fake_init(&mmio);
	io = fake_io(&mmio);
	const uint32_t saved_s2cr = mmio.regs[SWEET_SMMU_AUDIO_S2CR(0) / 4];
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_OK);
	mmio.ignore_write = true;
	mmio.ignored_write_offset = SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TCR2);
	assert(sweet_smmu_audio_install_diagnostic(&io, 0x1821, &mapping,
	    0x500000, &stage, &readback) == SWEET_SMMU_AUDIO_EIO);
	assert(stage == SWEET_SMMU_AUDIO_MAP_STAGE_CONTEXT_BANK_READBACK);
	assert(readback.valid);
	assert(readback.offset == SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TCR2));
	assert(readback.expected == ((7u << 15) | 2u));
	assert(readback.actual == 0x89abcdefu);
	assert(mapping.uncertain && !mapping.installed);
	assert(mmio.regs[SWEET_SMMU_AUDIO_S2CR(0) / 4] == saved_s2cr);
	assert(mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] == 0x123);
}

static void
test_map_restore_preserves_disabled_bank_and_route_registers(void)
{
	struct fake_mmio mmio;
	struct sweet_smmu_audio_mapping mapping;
	struct sweet_smmu_audio_io io;

	fake_init(&mmio);
	io = fake_io(&mmio);
	const uint32_t saved_smr = mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4];
	const uint32_t saved_s2cr = mmio.regs[SWEET_SMMU_AUDIO_S2CR(0) / 4];
	const uint32_t saved_cbar = mmio.regs[SWEET_SMMU_AUDIO_CBAR(0) / 4];
	const uint32_t saved_cba2r = mmio.regs[SWEET_SMMU_AUDIO_CBA2R(0) / 4];
	assert(sweet_smmu_audio_prepare(&io, 0x1821, &mapping) == SWEET_SMMU_AUDIO_OK);
	assert(mapping.smr_index == 0 && mapping.cb_index == 0);
	assert(mapping.saved_cb.cbar == saved_cbar && mapping.saved_cb.cba2r == saved_cba2r);
	assert(mapping.saved_s2cr == saved_s2cr && mapping.saved_smr == saved_smr);
	assert(sweet_smmu_audio_restore(&io, &mapping, 0) == SWEET_SMMU_AUDIO_EBUSY);
	assert(mmio.write_count == 0);
	mmio.regs[SWEET_SMMU_AUDIO_CBAR(0) / 4] ^= 1;
	assert(sweet_smmu_audio_install(&io, 0x1821, &mapping, 0x500000) == SWEET_SMMU_AUDIO_EBUSY);
	assert(mmio.write_count == 0);
	mmio.regs[SWEET_SMMU_AUDIO_CBAR(0) / 4] = saved_cbar;

	assert(sweet_smmu_audio_install(&io, 0x1821, &mapping, 0x500000) == SWEET_SMMU_AUDIO_OK);
	assert(mapping.installed);
	assert(mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] ==
	    (SWEET_SMMU_AUDIO_SMR_VALID | 0x1821));
	assert(mmio.regs[SWEET_SMMU_AUDIO_S2CR(0) / 4] == 0);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_SCTLR) / 4] & 1);
	uint32_t owned_smr = mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4];
	mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] = 0;
	size_t writes_before_refused_restore = mmio.write_count;
	assert(sweet_smmu_audio_restore(&io, &mapping,
	    SWEET_SMMU_AUDIO_ACK_RESTORE_REQUIRED) == SWEET_SMMU_AUDIO_EBUSY);
	assert(mmio.write_count == writes_before_refused_restore);
	mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] = owned_smr;

	assert(sweet_smmu_audio_restore(&io, &mapping,
	    SWEET_SMMU_AUDIO_ACK_RESTORE_REQUIRED) == SWEET_SMMU_AUDIO_OK);
	assert(!mapping.prepared && !mapping.installed && !mapping.uncertain);
	assert(mmio.regs[SWEET_SMMU_AUDIO_SMR(0) / 4] == saved_smr);
	assert(mmio.regs[SWEET_SMMU_AUDIO_S2CR(0) / 4] == saved_s2cr);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CBAR(0) / 4] == saved_cbar);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CBA2R(0) / 4] == saved_cba2r);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_SCTLR) / 4] == 2);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_ACTLR) / 4] == 0x12345678);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TCR2) / 4] == 0x89abcdef);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR0) / 4] == 0x11111000);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR0 + 4) / 4] == 1);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR1) / 4] == 0x22222000);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TTBR1 + 4) / 4] == 2);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_TCR) / 4] == 0x31415926);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_CONTEXTIDR) / 4] == 0x55);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_MAIR0) / 4] == 0x444);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(0, SWEET_SMMU_AUDIO_CB_MAIR1) / 4] == 0x555);
	assert(mmio.regs[SWEET_SMMU_AUDIO_SMR(5) / 4] ==
	    (SWEET_SMMU_AUDIO_SMR_VALID | 0x740));
	assert(mmio.regs[SWEET_SMMU_AUDIO_S2CR(10) / 4] == 3);
	assert(mmio.regs[SWEET_SMMU_AUDIO_CB(3, SWEET_SMMU_AUDIO_CB_SCTLR) / 4] == 0);
}

static void
test_five_page_stage1_descriptors_and_q6asm_address(void)
{
	uint64_t l1[512], l2[512], l3[512];
	uint64_t encoded = 0;
	memset(l1, 0xa5, sizeof(l1));
	memset(l2, 0xa5, sizeof(l2));
	memset(l3, 0xa5, sizeof(l3));
	assert(sweet_smmu_audio_build_tables(l1, 0x500000, l2, 0x501000,
	    l3, 0x502000, 0x10000000, 0x110000000ULL, 20480) == SWEET_SMMU_AUDIO_OK);
	assert(l1[0] == 0x501003 && l2[128] == 0x502003);
	assert(l1[1] == 0 && l2[127] == 0 && l2[129] == 0);
	for (unsigned i = 0; i < 5; i++) {
		assert(l3[i] == (0x110000000ULL + ((uint64_t)i << 12) + SWEET_SMMU_AUDIO_PTE_PAGE_NC));
	}
	assert(l3[5] == 0 && l3[511] == 0);
	assert(sweet_smmu_audio_build_tables(l1, 0x500000, l2, 0x501000,
	    l3, 0x502000, 0x10000000, 0x110000000ULL, 8192) == SWEET_SMMU_AUDIO_EINVAL);
	assert(sweet_smmu_audio_build_tables(l1, 0x500000, l2, 0x501000,
	    l3, 0x502000, 0x10001000, 0x110000000ULL, 20480) == SWEET_SMMU_AUDIO_EINVAL);
	assert(sweet_smmu_audio_build_tables(l1, 0x500001, l2, 0x501000,
	    l3, 0x502000, 0x10000000, 0x110000000ULL, 20480) == SWEET_SMMU_AUDIO_EINVAL);
	assert(sweet_smmu_audio_build_tables(l1, 0x500000, l2, 0x501000,
	    l3, 0x502000, 0x10000000, 0x10000000000ULL, 20480) == SWEET_SMMU_AUDIO_EINVAL);
	assert(sweet_smmu_audio_encode_q6asm_address(0x10000000, &encoded) == SWEET_SMMU_AUDIO_OK);
	assert(encoded == 0x110000000ULL);
	for (unsigned i = 0; i < 4; i++) {
		uint64_t period_iova = 0x10000000ULL + (uint64_t)i * 4800u;
		assert(sweet_smmu_audio_encode_q6asm_address(period_iova, &encoded) == SWEET_SMMU_AUDIO_OK);
		assert(encoded == ((1ULL << 32) | period_iova));
	}
	assert(sweet_smmu_audio_encode_q6asm_address(0x10005000, &encoded) == SWEET_SMMU_AUDIO_EINVAL);
}

int
main(void)
{
	test_exact_masked_and_duplicate_sid_matches_are_rejected();
	test_no_invalid_smr_and_no_unowned_context_bank_are_rejected();
	test_install_reprobes_sid_immediately_before_mutation();
	test_install_reports_context_bank_tlb_timeout_without_mapping_sid();
	test_install_reports_first_context_bank_readback_mismatch();
	test_map_restore_preserves_disabled_bank_and_route_registers();
	test_five_page_stage1_descriptors_and_q6asm_address();
	puts("private audio SMMU fake-MMIO checks passed");
	return 0;
}
