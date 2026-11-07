#ifndef BERYLLIUM_TAS_WRITER_H
#define BERYLLIUM_TAS_WRITER_H

#include "beryllium_tas_firmware.h"
#include <stdbool.h>
#include <stdint.h>

#define BERYLLIUM_TAS_DEVA 0u
#define BERYLLIUM_TAS_DEVB 1u
#define BERYLLIUM_TAS_ADDRESS_DEVA 0x4cu
#define BERYLLIUM_TAS_ADDRESS_DEVB 0x4du
#define BERYLLIUM_TAS_BOOK_PAGE_REGISTER 0x00u
#define BERYLLIUM_TAS_BOOK_SELECT_REGISTER 0x7fu
#define BERYLLIUM_TAS_MAX_BLOCK_COMMANDS (BERYLLIUM_TAS_MAX_INPUT / 4u)
#define BERYLLIUM_TAS_RETRY 6u

struct beryllium_tas_io_ops {
	int (*write)(void *context, uint8_t address, uint8_t reg, uint8_t value);
	int (*read)(void *context, uint8_t address, uint8_t reg, uint8_t *value);
	int (*bulk_write)(void *context, uint8_t address, uint8_t reg,
	    const uint8_t *values, size_t count);
	int (*bulk_read)(void *context, uint8_t address, uint8_t reg,
	    uint8_t *values, size_t count);
	void (*delay_ms)(void *context, uint32_t milliseconds);
};

struct beryllium_tas_writer_device {
	int book;
	int page;
};

struct beryllium_tas_writer {
	const struct beryllium_tas_io_ops *ops;
	void *context;
	struct beryllium_tas_writer_device devices[2];
};

static inline bool
beryllium_tas_writer_init(struct beryllium_tas_writer *writer,
	const struct beryllium_tas_io_ops *ops, void *context)
{
	if (writer == 0 || ops == 0 || ops->write == 0 || context == 0)
		return false;
	writer->ops = ops;
	writer->context = context;
	for (unsigned i = 0; i < 2; ++i) {
		writer->devices[i].book = -1;
		writer->devices[i].page = -1;
	}
	return true;
}

static inline int
beryllium_tas_select_page(struct beryllium_tas_writer *writer,
	unsigned device, uint8_t book, uint8_t page)
{
	static const uint8_t addresses[2] = {
		BERYLLIUM_TAS_ADDRESS_DEVA, BERYLLIUM_TAS_ADDRESS_DEVB
	};
	struct beryllium_tas_writer_device *state;
	int result;
	if (writer == 0 || writer->ops == 0 || writer->ops->write == 0 ||
	    writer->context == 0 || device > BERYLLIUM_TAS_DEVB)
		return -1;
	state = &writer->devices[device];
	if (state->book == book) {
		if (state->page == page)
			return 0;
		result = writer->ops->write(writer->context, addresses[device],
		    BERYLLIUM_TAS_BOOK_PAGE_REGISTER, page);
		if (result == 0)
			state->page = page;
		return result;
	}
	result = writer->ops->write(writer->context, addresses[device],
	    BERYLLIUM_TAS_BOOK_PAGE_REGISTER, 0);
	if (result != 0)
		return result;
	state->page = 0;
	result = writer->ops->write(writer->context, addresses[device],
	    BERYLLIUM_TAS_BOOK_SELECT_REGISTER, book);
	if (result != 0)
		return result;
	state->book = book;
	if (page != 0) {
		result = writer->ops->write(writer->context, addresses[device],
		    BERYLLIUM_TAS_BOOK_PAGE_REGISTER, page);
		if (result != 0)
			return result;
		state->page = page;
	}
	return 0;
}

static inline int
beryllium_tas_write_register(struct beryllium_tas_writer *writer,
	unsigned device, uint8_t book, uint8_t page, uint8_t reg, uint8_t value)
{
	static const uint8_t addresses[2] = {
		BERYLLIUM_TAS_ADDRESS_DEVA, BERYLLIUM_TAS_ADDRESS_DEVB
	};
	int result;
	if (writer == 0 || device > BERYLLIUM_TAS_DEVB || reg > 0x7fu ||
	    reg == BERYLLIUM_TAS_BOOK_PAGE_REGISTER ||
	    (page == 0 && reg == BERYLLIUM_TAS_BOOK_SELECT_REGISTER))
		return -1;
	result = beryllium_tas_select_page(writer, device, book, page);
	if (result != 0)
		return result;
	return writer->ops->write(writer->context, addresses[device], reg, value);
}

static inline int
beryllium_tas_read_register(struct beryllium_tas_writer *writer,
	unsigned device, uint8_t book, uint8_t page, uint8_t reg, uint8_t *value)
{
	static const uint8_t addresses[2] = {
		BERYLLIUM_TAS_ADDRESS_DEVA, BERYLLIUM_TAS_ADDRESS_DEVB
	};
	int result;
	if (writer == 0 || writer->ops == 0 || writer->ops->read == 0 ||
	    writer->context == 0 || device > BERYLLIUM_TAS_DEVB || value == 0 ||
	    reg == BERYLLIUM_TAS_BOOK_PAGE_REGISTER ||
	    (page == 0 && reg == BERYLLIUM_TAS_BOOK_SELECT_REGISTER))
		return -1;
	result = beryllium_tas_select_page(writer, device, book, page);
	if (result != 0)
		return result;
	return writer->ops->read(writer->context, addresses[device], reg, value);
}

static inline int
beryllium_tas_write_bulk(struct beryllium_tas_writer *writer,
	unsigned device, uint8_t book, uint8_t page, uint8_t reg,
	const uint8_t *values, size_t count)
{
	static const uint8_t addresses[2] = {
		BERYLLIUM_TAS_ADDRESS_DEVA, BERYLLIUM_TAS_ADDRESS_DEVB
	};
	if (writer == 0 || writer->ops == 0 || writer->ops->bulk_write == 0 ||
	    writer->context == 0 || device > BERYLLIUM_TAS_DEVB || values == 0 ||
	    count == 0 || count > 256 || reg == 0 || (size_t)reg + count > 0x80u)
		return -1;
	int result = beryllium_tas_select_page(writer, device, book, page);
	if (result != 0)
		return result;
	return writer->ops->bulk_write(writer->context, addresses[device], reg,
	    values, count);
}

static inline int
beryllium_tas_read_bulk(struct beryllium_tas_writer *writer,
	unsigned device, uint8_t book, uint8_t page, uint8_t reg,
	uint8_t *values, size_t count)
{
	static const uint8_t addresses[2] = {
		BERYLLIUM_TAS_ADDRESS_DEVA, BERYLLIUM_TAS_ADDRESS_DEVB
	};
	if (writer == 0 || writer->ops == 0 || writer->ops->bulk_read == 0 ||
	    writer->context == 0 || device > BERYLLIUM_TAS_DEVB || values == 0 ||
	    count == 0 || count > 256 || reg == 0 || (size_t)reg + count > 0x80u)
		return -1;
	int result = beryllium_tas_select_page(writer, device, book, page);
	if (result != 0)
		return result;
	return writer->ops->bulk_read(writer->context, addresses[device], reg,
	    values, count);
}

static inline bool
beryllium_tas_is_yram(unsigned device, uint8_t book, uint8_t page, uint8_t reg)
{
	if (device == BERYLLIUM_TAS_DEVA) {
		if (book == 140 && page == 42 && reg >= 88)
			return true;
		if (book == 140 && page == 56 && reg >= 8 && reg <= 47)
			return true;
		if (book == 140 && page >= 43 && page <= 55 && reg >= 8)
			return true;
		if (book == 0 && page >= 50 && page <= 60 && reg >= 8) {
			if (page == 53 && reg >= 44 && reg <= 48)
				return false;
			return true;
		}
		if (book == 0 && page == 61 && reg >= 8 && reg <= 27)
			return true;
		if (book == 120 && page == 12 && reg >= 104 && reg <= 107)
			return true;
	} else if (device == BERYLLIUM_TAS_DEVB && book == 0 &&
	    page >= 50 && page <= 52 && reg >= 8) {
		return true;
	}
	return false;
}

static inline uint8_t
beryllium_tas_crc8_byte(uint8_t value)
{
	uint8_t crc = value;
	for (unsigned bit = 0; bit < 8; ++bit) {
		uint16_t shifted = (uint16_t)crc << 1;
		crc = (uint8_t)((crc & 0x80u) ? shifted ^ 0x4du : shifted);
	}
	return crc;
}

static inline int
beryllium_tas_load_block_once(struct beryllium_tas_writer *writer,
	const struct beryllium_tas_block *block, unsigned device, bool ycrc_enabled)
{
	uint8_t ycrc = 0;
	uint32_t command = 0;
	if (block->pcrc_present) {
		uint8_t reset_reg = device == BERYLLIUM_TAS_DEVA ? 0x21u : 0x7eu;
		int result = beryllium_tas_write_register(writer, device, 0, 0,
		    reset_reg, 1);
		if (result != 0)
			return result;
	}
	while (command < block->command_count) {
		const uint8_t *cmd = block->commands + (size_t)command * 4u;
		uint8_t book = cmd[0], page = cmd[1], reg = cmd[2], value = cmd[3];
		if (reg <= 0x7fu) {
			int result = beryllium_tas_write_register(writer, device, book,
			    page, reg, value);
			if (result != 0)
				return result;
			if (ycrc_enabled && block->ycrc_present &&
			    beryllium_tas_is_yram(device, book, page, reg)) {
				uint8_t readback;
				result = beryllium_tas_read_register(writer, device, book,
				    page, reg, &readback);
				if (result != 0)
					return result;
				if (readback != value)
					return -11;
				ycrc = (uint8_t)(ycrc + beryllium_tas_crc8_byte(readback));
			}
			++command;
			continue;
		}
		if (reg == 0x81u) {
			uint32_t delay_ms = ((uint32_t)book << 8) | page;
			if (delay_ms > 1000 || writer->ops->delay_ms == 0)
				return -1;
			writer->ops->delay_ms(writer->context, delay_ms);
			++command;
			continue;
		}
		if (reg == 0x85u) {
			uint32_t length = ((uint32_t)book << 8) | page;
			const uint8_t *payload;
			uint32_t slots;
			if (length == 0 || length > 256 || command + 1u >= block->command_count)
				return -1;
			payload = cmd + 4;
			if ((size_t)length + 3u >
			    ((size_t)block->command_count - command - 1u) * 4u)
				return -1;
			book = payload[0]; page = payload[1]; reg = payload[2];
			if (reg > 0x7fu || reg == 0 || (page == 0 && reg == 0x7fu) ||
			    length > 0x80u - reg)
				return -1;
			slots = length == 1u ? 1u : 2u + (length - 2u) / 4u;
			if (slots > block->command_count - command - 1u)
				return -1;
			int result;
			if (length == 1u) {
				result = beryllium_tas_write_register(writer, device, book,
				    page, reg, payload[3]);
			} else {
				result = beryllium_tas_write_bulk(writer, device, book, page,
				    reg, payload + 3u, length);
			}
			if (result != 0)
				return result;
			if (ycrc_enabled && block->ycrc_present) {
				uint32_t start = 0;
				while (start < length) {
					while (start < length && !beryllium_tas_is_yram(device,
					    book, page, (uint8_t)(reg + start)))
						++start;
					uint32_t end = start;
					while (end < length && beryllium_tas_is_yram(device,
					    book, page, (uint8_t)(reg + end)))
						++end;
					if (end == start)
						continue;
					uint8_t readback[256];
					result = beryllium_tas_read_bulk(writer, device, book,
					    page, (uint8_t)(reg + start), readback, end - start);
					if (result != 0)
						return result;
					for (uint32_t i = start; i < end; ++i) {
						uint8_t actual = readback[i - start];
						if (actual != payload[3u + i])
							return -11;
						ycrc = (uint8_t)(ycrc +
						    beryllium_tas_crc8_byte(actual));
					}
					start = end;
				}
			}
			command += 1u + slots;
			continue;
		}
		return -1;
	}
	if (ycrc_enabled && block->ycrc_present && ycrc != block->ycrc)
		return -11;
	if (block->pcrc_present) {
		uint8_t crc_reg = device == BERYLLIUM_TAS_DEVA ? 0x20u : 0x7eu;
		uint8_t actual;
		int result = beryllium_tas_read_register(writer, device, 0, 0,
		    crc_reg, &actual);
		if (result != 0)
			return result;
		if (actual != block->pcrc)
			return -11;
	}
	return 0;
}

static inline int
beryllium_tas_load_block(struct beryllium_tas_writer *writer,
	const struct beryllium_tas_block *block, bool ycrc_enabled)
{
	unsigned device;
	if (writer == 0 || block == 0 || block->command_count >
	    BERYLLIUM_TAS_MAX_BLOCK_COMMANDS ||
	    (block->command_count != 0 && block->commands == 0))
		return -1;
	switch (block->type) {
	case 0: case 1: case 3: case 4:
		device = BERYLLIUM_TAS_DEVA;
		break;
	case 8: case 11: case 14:
		device = BERYLLIUM_TAS_DEVB;
		break;
	default:
		return -1;
	}
	for (unsigned attempt = 0; attempt < BERYLLIUM_TAS_RETRY; ++attempt) {
		int result = beryllium_tas_load_block_once(writer, block, device,
		    ycrc_enabled);
		if (result != -11)
			return result;
	}
	return -11;
}

#endif /* BERYLLIUM_TAS_WRITER_H */
