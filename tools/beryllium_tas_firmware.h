#ifndef BERYLLIUM_TAS_FIRMWARE_H
#define BERYLLIUM_TAS_FIRMWARE_H

/*
 * Bounds-checked reader for the TAS2559 PPC firmware container used by
 * Beryllium. This header is shared with the XNU patch as a single-file helper.
 * It deliberately validates only the Linux command subset needed by this
 * board and performs no allocation, MMIO, or I2C operations.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BERYLLIUM_TAS_MAX_INPUT (128u * 1024u)
#define BERYLLIUM_TAS_MAX_BLOCKS 32u
#define BERYLLIUM_TAS_MAGIC 0x35353532u
#define BERYLLIUM_TAS_DRIVER_FIRMWARE 0x00000401u
#define BERYLLIUM_TAS_DRIVER_CALIBRATION 0x00000100u

struct beryllium_tas_block {
	uint32_t type;
	uint8_t pcrc_present;
	uint8_t pcrc;
	uint8_t ycrc_present;
	uint8_t ycrc;
	uint32_t command_count;
	const uint8_t *commands;
};

struct beryllium_tas_data {
	uint16_t block_count;
	struct beryllium_tas_block blocks[BERYLLIUM_TAS_MAX_BLOCKS];
};

struct beryllium_tas_firmware {
	uint32_t checksum;
	uint32_t timestamp;
	uint32_t pll_count;
	uint32_t program_count;
	uint32_t configuration_count;
	uint8_t configuration_program;
	uint8_t configuration_pll;
	uint32_t sampling_rate;
	uint8_t pll_source;
	uint32_t pll_source_rate;
	struct beryllium_tas_block pll_zero;
	struct beryllium_tas_data program_zero;
	struct beryllium_tas_data configuration_zero;
};

struct beryllium_tas_calibration {
	uint8_t program;
	uint8_t configuration;
	struct beryllium_tas_data data;
};

struct beryllium_tas_cursor {
	const uint8_t *data;
	size_t size;
	size_t offset;
};

static inline bool
beryllium_tas_take(struct beryllium_tas_cursor *c, size_t n,
	const uint8_t **out)
{
	if (c == NULL || c->offset > c->size || n > c->size - c->offset)
		return false;
	if (out != NULL)
		*out = c->data + c->offset;
	c->offset += n;
	return true;
}

static inline bool
beryllium_tas_u8(struct beryllium_tas_cursor *c, uint8_t *out)
{
	const uint8_t *p;
	if (!beryllium_tas_take(c, 1, &p))
		return false;
	*out = p[0];
	return true;
}

static inline bool
beryllium_tas_u16(struct beryllium_tas_cursor *c, uint16_t *out)
{
	const uint8_t *p;
	if (!beryllium_tas_take(c, 2, &p))
		return false;
	*out = ((uint16_t)p[0] << 8) | p[1];
	return true;
}

static inline bool
beryllium_tas_u32(struct beryllium_tas_cursor *c, uint32_t *out)
{
	const uint8_t *p;
	if (!beryllium_tas_take(c, 4, &p))
		return false;
	*out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3];
	return true;
}

static inline bool
beryllium_tas_string(struct beryllium_tas_cursor *c)
{
	size_t n;
	if (c == NULL || c->offset > c->size)
		return false;
	for (n = 0; n < c->size - c->offset; ++n) {
		if (c->data[c->offset + n] == 0) {
			c->offset += n + 1;
			return true;
		}
	}
	return false;
}

static inline bool
beryllium_tas_validate_commands(const uint8_t *p, uint32_t count)
{
	uint32_t i = 0;
	while (i < count) {
		const uint8_t *cmd = p + (size_t)i * 4;
		uint8_t offset = cmd[2];
		if (offset <= 0x7f) {
			++i;
		} else if (offset == 0x81) {
			uint32_t delay_ms = ((uint32_t)cmd[0] << 8) | cmd[1];
			if (delay_ms > 1000)
				return false;
			++i;
		} else if (offset == 0x85) {
			uint32_t len, slots;
			const uint8_t *span;
			if (i + 1 >= count)
				return false;
			len = ((uint32_t)cmd[0] << 8) | cmd[1];
			if (len == 0 || len > 256)
				return false;
			if (len + 3 > ((size_t)count - i - 1) * 4)
				return false;
			span = cmd + 4;
			if (span[2] > 0x7f || len > 0x80u - span[2])
				return false;
			slots = 1 + (len > 3 ? (len - 3 + 3) / 4 : 0);
			if (slots > count - i - 1)
				return false;
			i += 1 + slots;
		} else {
			return false;
		}
	}
	return true;
}

static inline bool
beryllium_tas_parse_block(struct beryllium_tas_cursor *c,
	struct beryllium_tas_block *out, bool has_crc)
{
	struct beryllium_tas_block block = {0};
	const uint8_t *cmds;
	if (!beryllium_tas_u32(c, &block.type))
		return false;
	if (block.type > 0x0e || block.type == 0x02 || block.type == 0x07 ||
	    block.type == 0x09 || block.type == 0x0c)
		return false;
	if (has_crc) {
		if (c->offset > c->size || c->size - c->offset < 4)
			return false;
		block.pcrc_present = c->data[c->offset++];
		block.pcrc = c->data[c->offset++];
		block.ycrc_present = c->data[c->offset++];
		block.ycrc = c->data[c->offset++];
	}
	if (block.pcrc_present > 1 || block.ycrc_present > 1 ||
	    (!block.pcrc_present && block.pcrc != 0) ||
	    (!block.ycrc_present && block.ycrc != 0) ||
	    !beryllium_tas_u32(c, &block.command_count) || c->offset > c->size ||
	    block.command_count > (c->size - c->offset) / 4 ||
	    !beryllium_tas_take(c, (size_t)block.command_count * 4, &cmds) ||
	    !beryllium_tas_validate_commands(cmds, block.command_count))
		return false;
	block.commands = cmds;
	if (out != NULL)
		*out = block;
	return true;
}

static inline bool
beryllium_tas_parse_data(struct beryllium_tas_cursor *c,
	struct beryllium_tas_data *selected, bool has_crc)
{
	uint16_t count, i;
	if (!beryllium_tas_take(c, 64, NULL) || !beryllium_tas_string(c) ||
	    !beryllium_tas_u16(c, &count) || count > BERYLLIUM_TAS_MAX_BLOCKS)
		return false;
	if (selected != NULL)
		selected->block_count = count;
	for (i = 0; i < count; ++i) {
		if (!beryllium_tas_parse_block(c,
		    selected != NULL ? &selected->blocks[i] : NULL, has_crc))
			return false;
	}
	return true;
}

static inline bool
beryllium_tas_parse_header(struct beryllium_tas_cursor *c, uint32_t driver,
	uint32_t *checksum, uint32_t *timestamp)
{
	uint32_t magic, size, value, family, device;
	const uint8_t *ddc;
	static const char firmware_ddc[] = "E10";
	static const char calibration_ddc[] = "Calibration Data File";
	const char *expected_ddc = driver == BERYLLIUM_TAS_DRIVER_FIRMWARE ?
	    firmware_ddc : calibration_ddc;
	size_t ddc_len = driver == BERYLLIUM_TAS_DRIVER_FIRMWARE ? 3 : 21;
	size_t i;
	if (!beryllium_tas_u32(c, &magic) || magic != BERYLLIUM_TAS_MAGIC ||
	    !beryllium_tas_u32(c, &size) || size != c->size ||
	    !beryllium_tas_u32(c, checksum) ||
	    !beryllium_tas_u32(c, &value) || !beryllium_tas_u32(c, &value) ||
	    !beryllium_tas_u32(c, &value) || value != driver ||
	    !beryllium_tas_u32(c, timestamp) || !beryllium_tas_take(c, 64, &ddc))
		return false;
	for (i = 0; i < ddc_len; ++i) {
		if (ddc[i] != (uint8_t)expected_ddc[i])
			return false;
	}
	if (!beryllium_tas_string(c) || !beryllium_tas_u32(c, &family) || family != 0 ||
	    !beryllium_tas_u32(c, &device) || device != 4)
		return false;
	return true;
}

static inline bool
beryllium_tas_parse_firmware(const uint8_t *data, size_t size,
	struct beryllium_tas_firmware *out)
{
	struct beryllium_tas_cursor c = { data, size, 0 };
	uint32_t checksum, timestamp, i, count, pll, rate;
	uint16_t n;
	uint8_t program, source;
	if (data == NULL || out == NULL || size > BERYLLIUM_TAS_MAX_INPUT ||
	    size == 0)
		return false;
	*out = (struct beryllium_tas_firmware){0};
	if (!beryllium_tas_parse_header(&c, BERYLLIUM_TAS_DRIVER_FIRMWARE,
	    &checksum, &timestamp) || !beryllium_tas_u16(&c, &n))
		return false;
	count = n;
	if (count > 32)
		return false;
	out->pll_count = count;
	for (i = 0; i < count; ++i) {
		if (!beryllium_tas_take(&c, 64, NULL) || !beryllium_tas_string(&c) ||
		    !beryllium_tas_parse_block(&c, i == 0 ? &out->pll_zero : NULL, true))
			return false;
	}
	if (!beryllium_tas_u16(&c, &n) || (count = n) == 0 || count > 32)
		return false;
	out->program_count = count;
	for (i = 0; i < count; ++i) {
		if (!beryllium_tas_take(&c, 64, NULL) || !beryllium_tas_string(&c) ||
		    !beryllium_tas_u8(&c, &program) || !beryllium_tas_take(&c, 2, NULL))
			return false;
		if (!beryllium_tas_parse_data(&c, i == 0 ? &out->program_zero : NULL, true))
			return false;
	}
	if (!beryllium_tas_u16(&c, &n) || (count = n) == 0 || count > 64)
		return false;
	out->configuration_count = count;
	for (i = 0; i < count; ++i) {
		uint8_t index;
		if (!beryllium_tas_take(&c, 64, NULL) || !beryllium_tas_string(&c) ||
		    !beryllium_tas_take(&c, 2, NULL) || !beryllium_tas_u8(&c, &program) ||
		    !beryllium_tas_u8(&c, &index) || !beryllium_tas_u32(&c, &rate) ||
		    !beryllium_tas_u8(&c, &source) || !beryllium_tas_u32(&c, &pll))
			return false;
		if (program >= out->program_count || index >= out->pll_count || rate == 0 ||
		    pll == 0 || !beryllium_tas_parse_data(&c,
		    i == 0 ? &out->configuration_zero : NULL, true))
			return false;
		if (i == 0) {
			out->configuration_program = program;
			out->configuration_pll = index;
			out->sampling_rate = rate;
			out->pll_source = source;
			out->pll_source_rate = pll;
		}
	}
	if (c.offset != c.size || out->configuration_program != 0 ||
	    out->configuration_pll != 0 ||
	    out->sampling_rate != 48000)
		return false;
	out->checksum = checksum;
	out->timestamp = timestamp;
	return true;
}

static inline bool
beryllium_tas_parse_calibration(const uint8_t *data, size_t size,
	uint8_t program, uint8_t configuration,
	struct beryllium_tas_calibration *out)
{
	struct beryllium_tas_cursor c = { data, size, 0 };
	uint32_t checksum, timestamp, count, i;
	uint16_t n;
	bool found = false;
	if (data == NULL || out == NULL || size > BERYLLIUM_TAS_MAX_INPUT ||
	    !beryllium_tas_parse_header(&c, BERYLLIUM_TAS_DRIVER_CALIBRATION,
	    &checksum, &timestamp))
		return false;
	*out = (struct beryllium_tas_calibration){0};
	(void)checksum;
	(void)timestamp;
	if (!beryllium_tas_u16(&c, &n) || n != 0 || !beryllium_tas_u16(&c, &n) || n != 0 ||
	    !beryllium_tas_u16(&c, &n) || n != 0 || !beryllium_tas_u16(&c, &n) ||
	    n != 1)
		return false;
	count = n;
	for (i = 0; i < count; ++i) {
		uint8_t p, cfg;
		struct beryllium_tas_data parsed = {0};
		if (!beryllium_tas_take(&c, 64, NULL) || !beryllium_tas_string(&c) ||
		    !beryllium_tas_u8(&c, &p) || !beryllium_tas_u8(&c, &cfg) ||
	    !beryllium_tas_parse_data(&c, &parsed, false))
			return false;
		if (p == program && cfg == configuration) {
			if (found)
				return false;
			*out = (struct beryllium_tas_calibration){ p, cfg, parsed };
			found = true;
		}
	}
	return found && c.offset == c.size;
}

#endif
