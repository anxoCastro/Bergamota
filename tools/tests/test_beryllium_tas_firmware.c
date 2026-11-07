#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../beryllium_tas_firmware.h"

static uint8_t image[2048];
static size_t used;
static size_t crc_flag_offset;
static size_t program_count_offset;
static size_t configuration_program_offset;

static size_t
put(const void *p, size_t n)
{
	memcpy(image + used, p, n);
	used += n;
	return used - n;
}

static void
u8(uint8_t n)
{
	put(&n, 1);
}

static void
u16(uint16_t n)
{
	uint8_t b[] = { (uint8_t)(n >> 8), (uint8_t)n };
	put(b, sizeof(b));
}

static void
u32(uint32_t n)
{
	uint8_t b[] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
	put(b, sizeof(b));
}

static void
fixed_name(void)
{
	uint8_t name[64] = {0};
	memcpy(name, "synthetic", 9);
	put(name, sizeof(name));
}

static void
string(const char *s)
{
	put(s, strlen(s) + 1);
}

static void
header(uint32_t driver)
{
	uint8_t ddc[64] = {0};
	u32(BERYLLIUM_TAS_MAGIC);
	u32(0);
	u32(0x1234);
	u32(1);
	u32(1);
	u32(driver);
	u32(10);
	memcpy(ddc, driver == BERYLLIUM_TAS_DRIVER_FIRMWARE ? "E10" :
	    "Calibration Data File", driver == BERYLLIUM_TAS_DRIVER_FIRMWARE ? 3 : 21);
	put(ddc, sizeof(ddc));
	string("synthetic test data");
	u32(0);
	u32(4);
}

static void
block(uint32_t type, uint8_t op, int has_crc)
{
	u32(type);
	if (has_crc) {
		crc_flag_offset = used;
		u8(0);
		u8(0);
		u8(0);
		u8(0);
	}
	u32(1);
	u8(0); u8(0); u8(op); u8(0x55);
}

static void
data_block(uint32_t type, uint8_t op, int has_crc)
{
	fixed_name();
	string("synthetic block");
	u16(1);
	block(type, op, has_crc);
}

static size_t
make_firmware(uint8_t config_program, uint8_t config_op)
{
	used = 0;
	memset(image, 0, sizeof(image));
	header(BERYLLIUM_TAS_DRIVER_FIRMWARE);
	u16(1);
	fixed_name(); string("pll"); block(0, 1, 1);
	program_count_offset = used;
	u16(1);
	fixed_name(); string("program"); u8(0); u16(0); data_block(1, 1, 1);
	u16(1);
	fixed_name(); string("configuration"); u16(3);
	configuration_program_offset = used;
	u8(config_program); u8(0);
	u32(48000); u8(1); u32(1536000);
	data_block(4, config_op, 1);
	image[4] = (uint8_t)(used >> 24);
	image[5] = (uint8_t)(used >> 16);
	image[6] = (uint8_t)(used >> 8);
	image[7] = (uint8_t)used;
	return used;
}

static size_t
make_calibration(void)
{
	used = 0;
	memset(image, 0, sizeof(image));
	header(BERYLLIUM_TAS_DRIVER_CALIBRATION);
	u16(0); u16(0); u16(0); u16(1);
	fixed_name(); string("calibration"); u8(0); u8(0);
	data_block(3, 1, 0);
	image[4] = (uint8_t)(used >> 24);
	image[5] = (uint8_t)(used >> 16);
	image[6] = (uint8_t)(used >> 8);
	image[7] = (uint8_t)used;
	return used;
}

int
main(void)
{
	struct beryllium_tas_firmware fw;
	struct beryllium_tas_calibration cal;
	const uint8_t bulk_length_2[] = {
		0, 2, 0x85, 0,
		0, 0, 8, 0xaa,
		0xbb, 0xcc, 0xdd, 0xee,
	};
	const uint8_t bulk_length_3[] = {
		0, 3, 0x85, 0,
		0, 0, 8, 0xaa,
		0xbb, 0xcc, 0xdd, 0xee,
	};
	const uint8_t reserved_register[] = {
		0, 0, 4, 1,
		0, 0, 0, 0,
	};
	const uint8_t reserved_book_select[] = {
		0, 0, 4, 1,
		0, 0, 0x7f, 0,
	};
	const uint8_t page_zero_bulk_book_select[] = {
		0, 2, 0x85, 0,
		0, 0, 126, 1,
		2, 0, 0, 0,
	};
	const uint8_t page_one_data_book_select_offset[] = {
		0, 2, 0x85, 0,
		0, 1, 126, 1,
		2, 0, 0, 0,
	};
	if (!beryllium_tas_validate_commands(bulk_length_2, 3) ||
	    !beryllium_tas_validate_commands(bulk_length_3, 3) ||
	    beryllium_tas_validate_commands(bulk_length_2, 2) ||
	    beryllium_tas_validate_commands(reserved_register, 2) ||
	    beryllium_tas_validate_commands(reserved_book_select, 2) ||
	    beryllium_tas_validate_commands(page_zero_bulk_book_select, 3) ||
	    !beryllium_tas_validate_commands(page_one_data_book_select_offset, 3))
		return 10;
	size_t n = make_firmware(0, 1);
	if (!beryllium_tas_parse_firmware(image, n, &fw) || fw.sampling_rate != 48000 ||
	    fw.configuration_program != 0 || fw.program_zero.block_count != 1 ||
	    fw.configuration_zero.blocks[0].type != 4)
		return 1;
	for (size_t i = 0; i < n; ++i) {
		if (beryllium_tas_parse_firmware(image, i, &fw))
			return 2;
	}
	image[n - 2] = 0x80;
	if (beryllium_tas_parse_firmware(image, n, &fw))
		return 3;
	n = make_firmware(0, 1);
	image[crc_flag_offset] = 2;
	if (beryllium_tas_parse_firmware(image, n, &fw))
		return 4;
	n = make_firmware(1, 1);
	if (beryllium_tas_parse_firmware(image, n, &fw))
		return 7;
	n = make_firmware(0, 0x85);
	if (beryllium_tas_parse_firmware(image, n, &fw))
		return 8;
	n = make_firmware(0, 1);
	image[program_count_offset] = 0xff;
	image[program_count_offset + 1] = 0xff;
	if (beryllium_tas_parse_firmware(image, n, &fw) ||
	    beryllium_tas_parse_firmware(image, BERYLLIUM_TAS_MAX_INPUT + 1, &fw))
		return 9;
	n = make_calibration();
	if (!beryllium_tas_parse_calibration(image, n, 0, 0, &cal) ||
	    cal.data.block_count != 1 || cal.data.blocks[0].type != 3)
		return 5;
	if (beryllium_tas_parse_calibration(image, n - 1, 0, 0, &cal) ||
	    beryllium_tas_parse_calibration(image, n, 1, 0, &cal))
		return 6;
	puts("synthetic firmware/calibration parser: PASS");
	return 0;
}
