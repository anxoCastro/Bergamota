#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../beryllium_tas_writer.h"

struct write_event {
	uint8_t address;
	uint8_t reg;
	uint8_t value;
};

struct fake_bus {
	struct write_event events[32];
	size_t count;
	int fail_at;
	uint8_t read_value;
};

static int
fake_write(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
	struct fake_bus *bus = context;
	if ((int)bus->count == bus->fail_at)
		return -7;
	if (bus->count >= sizeof(bus->events) / sizeof(bus->events[0]))
		return -8;
	bus->events[bus->count++] = (struct write_event){ address, reg, value };
	return 0;
}

static int
fake_read(void *context, uint8_t address, uint8_t reg, uint8_t *value)
{
	struct fake_bus *bus = context;
	(void)address;
	(void)reg;
	*value = bus->read_value;
	return 0;
}

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)

int
main(void)
{
	const struct beryllium_tas_io_ops ops = {
		.write = fake_write,
		.read = fake_read,
	};
	struct beryllium_tas_writer writer;
	struct fake_bus bus = { .fail_at = -1 };
	CHECK(beryllium_tas_writer_init(&writer, &ops, &bus));
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    0, 0, 0x03, 0xa0) == 0);
	CHECK(bus.count == 3);
	CHECK(bus.events[0].address == 0x4c && bus.events[0].reg == 0x00 &&
	    bus.events[0].value == 0x00);
	CHECK(bus.events[1].address == 0x4c && bus.events[1].reg == 0x7f &&
	    bus.events[1].value == 0x00);
	CHECK(bus.events[2].address == 0x4c && bus.events[2].reg == 0x03 &&
	    bus.events[2].value == 0xa0);
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    0, 0, 0x04, 0x55) == 0);
	CHECK(bus.count == 4 && bus.events[3].reg == 0x04 &&
	    bus.events[3].value == 0x55);
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    2, 3, 0x05, 0x66) == 0);
	CHECK(bus.count == 8);
	CHECK(bus.events[4].reg == 0x00 && bus.events[4].value == 0x00);
	CHECK(bus.events[5].reg == 0x7f && bus.events[5].value == 0x02);
	CHECK(bus.events[6].reg == 0x00 && bus.events[6].value == 0x03);
	CHECK(bus.events[7].reg == 0x05 && bus.events[7].value == 0x66);
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVB,
	    0, 0, 0x03, 0) == 0);
	CHECK(bus.count == 11 && bus.events[8].address == 0x4d &&
	    bus.events[8].reg == 0x00 && bus.events[9].reg == 0x7f &&
	    bus.events[10].address == 0x4d && bus.events[10].reg == 0x03);
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    0, 0, 0x00, 1) != 0);
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    0, 0, 0x7f, 1) != 0);
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    2, 3, 0x7f, 0x55) == 0);
	CHECK(bus.count == 12 && bus.events[11].reg == 0x7f &&
	    bus.events[11].value == 0x55);

	memset(&bus, 0, sizeof(bus));
	bus.fail_at = 1;
	CHECK(beryllium_tas_writer_init(&writer, &ops, &bus));
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    0, 0, 0x03, 0xa0) == -7);
	CHECK(bus.count == 1);
	bus.fail_at = -1;
	CHECK(beryllium_tas_write_register(&writer, BERYLLIUM_TAS_DEVA,
	    0, 0, 0x03, 0xa0) == 0);
	CHECK(bus.count == 4 && bus.events[1].reg == 0x00 &&
	    bus.events[2].reg == 0x7f && bus.events[3].reg == 0x03);

	memset(&bus, 0, sizeof(bus));
	bus.fail_at = -1;
	bus.read_value = 1;
	CHECK(beryllium_tas_writer_init(&writer, &ops, &bus));
	const uint8_t yram_command[] = { 0, 50, 8, 1 };
	const struct beryllium_tas_block yram_block = {
		.type = 8,
		.ycrc_present = 1,
		.ycrc = 0x4d,
		.command_count = 1,
		.commands = yram_command,
	};
	CHECK(beryllium_tas_load_block(&writer, &yram_block, true) == 0);
	CHECK(bus.count == 4 && bus.events[0].address == 0x4d &&
	    bus.events[1].reg == 0x7f && bus.events[2].reg == 0x00 &&
	    bus.events[2].value == 50 && bus.events[3].reg == 8);
	const struct beryllium_tas_block invalid_block = { .type = 7 };
	size_t before_invalid = bus.count;
	CHECK(beryllium_tas_load_block(&writer, &invalid_block, true) != 0);
	CHECK(bus.count == before_invalid);
	puts("PASS: TAS register selection and cached page state");
	return 0;
}
