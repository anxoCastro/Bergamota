#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "beryllium_adsp_glink_survey.h"

#define ADSP_SMEM_SIZE 16384u
#define ADSP_SMEM_TOC_MAGIC 0x434f5424u
#define ADSP_SMEM_PART_MAGIC 0x54525024u
#define ADSP_SMEM_PRIVATE_CANARY 0xa5a5u
#define ADSP_SMEM_TOC_BYTES 32u
#define ADSP_SMEM_TOC_ENTRY_BYTES 48u
#define ADSP_SMEM_PART_HDR_BYTES 32u
#define ADSP_SMEM_PRIVATE_HDR 16u
#define ADSP_SMEM_MAX_PARTITIONS 32u
#define ADSP_SMEM_MAX_ITEMS 512u
#define ADSP_LOCAL_PID 0u
#define ADSP_REMOTE_PID 2u
#define PART_OFF 1024u
#define PART_SIZE 2048u
#define TOC_OFF (ADSP_SMEM_SIZE - 0x1000u)

#define ADSP_SMP2P_LOG(...) ((void)0)

static volatile uint8_t smem_bytes[ADSP_SMEM_SIZE];
static volatile uint8_t *adsp_smem = smem_bytes;

static bool
adsp_range(uint32_t total, uint32_t off, uint64_t size)
{
	return off <= total && size <= (uint64_t)total - off;
}

static uint16_t
adsp_smem_rd16(uint32_t off)
{
	return (uint16_t)smem_bytes[off] | ((uint16_t)smem_bytes[off + 1] << 8);
}

static uint32_t
adsp_smem_rd32(uint32_t off)
{
	return (uint32_t)smem_bytes[off] | ((uint32_t)smem_bytes[off + 1] << 8) |
	    ((uint32_t)smem_bytes[off + 2] << 16) |
	    ((uint32_t)smem_bytes[off + 3] << 24);
}

static void
put16(uint32_t off, uint16_t value)
{
	smem_bytes[off] = (uint8_t)value;
	smem_bytes[off + 1] = (uint8_t)(value >> 8);
}

static void
put32(uint32_t off, uint32_t value)
{
	put16(off, (uint16_t)value);
	put16(off + 2, (uint16_t)(value >> 16));
}

/* PRODUCTION_FUNCTION */

static void
set_private_item(uint32_t header_off, uint16_t item, uint32_t size)
{
	put32(header_off, ((uint32_t)item << 16) | ADSP_SMEM_PRIVATE_CANARY);
	put32(header_off + 4, size);
	put32(header_off + 8, 0);
	put32(header_off + 12, 0);
}

static void
reset_fixture(void)
{
	const uint32_t entry = TOC_OFF + ADSP_SMEM_TOC_BYTES;
	memset((void *)smem_bytes, 0, sizeof(smem_bytes));
	put32(TOC_OFF, ADSP_SMEM_TOC_MAGIC);
	put32(TOC_OFF + 8, 1);
	put32(entry, PART_OFF);
	put32(entry + 4, PART_SIZE);
	put32(entry + 12, ADSP_REMOTE_PID << 16 | ADSP_LOCAL_PID);
	put32(entry + 16, 32);
	put32(PART_OFF, ADSP_SMEM_PART_MAGIC);
	put16(PART_OFF + 4, ADSP_LOCAL_PID);
	put16(PART_OFF + 6, ADSP_REMOTE_PID);
	put32(PART_OFF + 8, PART_SIZE);
	put32(PART_OFF + 12, ADSP_SMEM_PART_HDR_BYTES);
	put32(PART_OFF + 16, PART_SIZE);
}

int
main(void)
{
	uint32_t data_off = 0, data_size = 0;
	uint32_t cached_header, cached_header2, cached_data, cached_free;

	reset_fixture();
	set_private_item(PART_OFF + ADSP_SMEM_PART_HDR_BYTES, 478, 32);
	put32(PART_OFF + 12, ADSP_SMEM_PART_HDR_BYTES + ADSP_SMEM_PRIVATE_HDR + 32);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_FOUND);
	assert(data_off == PART_OFF + ADSP_SMEM_PART_HDR_BYTES + ADSP_SMEM_PRIVATE_HDR);
	assert(data_size == 32);

	reset_fixture();
	cached_header = PART_OFF + PART_SIZE - 32;
	cached_header2 = cached_header - 32 - 32;
	cached_data = cached_header - 32;
	cached_free = cached_header2 - 32 - 32 - PART_OFF;
	set_private_item(cached_header, 478, 32);
	set_private_item(cached_header2, 479, 32);
	put32(PART_OFF + 16, cached_free);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_FOUND);
	assert(data_off == cached_data);
	assert(data_size == 32);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 479,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_FOUND);
	assert(data_off == cached_header2 - 32);
	assert(data_size == 32);

	reset_fixture();
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_ABSENT);

	reset_fixture();
	put32(PART_OFF + 8, 0);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_MALFORMED);

	reset_fixture();
	put32(PART_OFF + 8, 64);
	set_private_item(PART_OFF + ADSP_SMEM_PART_HDR_BYTES, 478, 32);
	put32(PART_OFF + 12, ADSP_SMEM_PART_HDR_BYTES + ADSP_SMEM_PRIVATE_HDR + 32);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_MALFORMED);

	reset_fixture();
	cached_header = PART_OFF + PART_SIZE - 32;
	cached_free = cached_header - 32 - 32 - PART_OFF;
	set_private_item(cached_header, 478, 32);
	put32(PART_OFF + 16, cached_free);
	put32(TOC_OFF + ADSP_SMEM_TOC_BYTES + 16, 0);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_MALFORMED);

	reset_fixture();
	put32(TOC_OFF, 0);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_MALFORMED);

	reset_fixture();
	set_private_item(PART_OFF + ADSP_SMEM_PART_HDR_BYTES, 478, 32);
	put32(PART_OFF + 12, ADSP_SMEM_PART_HDR_BYTES + ADSP_SMEM_PRIVATE_HDR + 32);
	cached_header = PART_OFF + PART_SIZE - 32;
	cached_data = cached_header - 32;
	cached_free = cached_header - 32 - 32 - PART_OFF;
	set_private_item(cached_header, 478, 32);
	put32(PART_OFF + 16, cached_free);
	assert(adsp_smem_private_item(ADSP_LOCAL_PID, ADSP_REMOTE_PID, 478,
	    &data_off, &data_size) == BERYLLIUM_ADSP_ITEM_MALFORMED);

	puts("SMEM private item parser checks passed");
	return 0;
}
