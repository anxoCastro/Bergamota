#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "beryllium_tas_id_probe.h"

struct fake_bus {
	bool scl_high;
	bool scl_stuck_low;
	unsigned sda_releases;
	unsigned sda_drives_low;
	bool sda_trace[96];
	unsigned sda_trace_count;
	unsigned scl_rises;
	unsigned delay_us;
	unsigned sda_index;
	unsigned sda_count;
	bool sda_script[16];
};

static void
fake_set_sda(void *context, bool release)
{
	struct fake_bus *bus = context;
	if (bus->sda_trace_count < sizeof(bus->sda_trace) / sizeof(bus->sda_trace[0]))
		bus->sda_trace[bus->sda_trace_count++] = release;
	if (release)
		bus->sda_releases++;
	else
		bus->sda_drives_low++;
}

static void
fake_set_scl(void *context, bool release)
{
	struct fake_bus *bus = context;
	if (release && !bus->scl_high && !bus->scl_stuck_low)
		bus->scl_rises++;
	bus->scl_high = release && !bus->scl_stuck_low;
}

static bool
fake_read_sda(void *context)
{
	struct fake_bus *bus = context;
	if (bus->sda_index >= bus->sda_count)
		return true;
	return bus->sda_script[bus->sda_index++];
}

static bool
fake_read_scl(void *context)
{
	return ((struct fake_bus *)context)->scl_high;
}

static void
fake_delay(void *context, unsigned microseconds)
{
	((struct fake_bus *)context)->delay_us += microseconds;
}

static const struct beryllium_tas_i2c_ops fake_ops = {
	.set_sda_release = fake_set_sda,
	.set_scl_release = fake_set_scl,
	.read_sda = fake_read_sda,
	.read_scl = fake_read_scl,
	.delay_us = fake_delay,
};

static void
expect_sda(bool *trace, unsigned *count, bool release)
{
	trace[(*count)++] = release;
}

static void
expect_write_byte(bool *trace, unsigned *count, uint8_t value)
{
	for (int bit = 7; bit >= 0; --bit)
		expect_sda(trace, count, (value & (1u << bit)) != 0);
	expect_sda(trace, count, true); /* release SDA for ACK */
}

static int
test_read_ack(uint8_t address, uint8_t reg)
{
	struct fake_bus bus = { .scl_high = false };
	const bool response[] = { true, false, false, true, false,
		true, false, true, false, false, true, false, true };
	for (unsigned i = 0; i < sizeof(response) / sizeof(response[0]); ++i)
		bus.sda_script[i] = response[i];
	bus.sda_count = sizeof(response) / sizeof(response[0]);
	uint8_t value = 0;
	int result = beryllium_tas_i2c_read_reg(&fake_ops, &bus, address, reg, &value);
	bool expected[96];
	unsigned expected_count = 0;
	expect_sda(expected, &expected_count, true); /* bus idle / START setup */
	expect_sda(expected, &expected_count, false); /* START */
	expect_write_byte(expected, &expected_count, (uint8_t)(address << 1));
	expect_write_byte(expected, &expected_count, reg);
	expect_sda(expected, &expected_count, true);
	expect_sda(expected, &expected_count, false); /* repeated START */
	expect_write_byte(expected, &expected_count, (uint8_t)((address << 1) | 1u));
	expect_sda(expected, &expected_count, true); /* release for receive */
	expect_sda(expected, &expected_count, true); /* master NACK */
	expect_sda(expected, &expected_count, true); /* release before STOP */
	expect_sda(expected, &expected_count, false); /* STOP */
	expect_sda(expected, &expected_count, true);
	if (result != BERYLLIUM_TAS_I2C_OK || value != 0xa5 ||
	    bus.sda_index != bus.sda_count || bus.sda_drives_low == 0 ||
	    bus.sda_releases == 0 || bus.sda_trace_count != expected_count ||
	    bus.scl_rises != 39) {
		fprintf(stderr, "ACK read failed: result=%d value=%02x script=%u/%u trace=%u/%u scl-rises=%u\n",
		    result, value, bus.sda_index, bus.sda_count,
		    bus.sda_trace_count, expected_count, bus.scl_rises);
		for (unsigned i = 0; i < bus.sda_trace_count; ++i)
			fprintf(stderr, "%u", bus.sda_trace[i]);
		fputc('\n', stderr);
		for (unsigned i = 0; i < expected_count; ++i)
			fprintf(stderr, "%u", expected[i]);
		fputc('\n', stderr);
		return 1;
	}
	for (unsigned i = 0; i < expected_count; ++i) {
		if (bus.sda_trace[i] != expected[i]) {
			fprintf(stderr, "wrong SDA bit/event %u: got=%u expected=%u\n",
			    i, bus.sda_trace[i], expected[i]);
			return 1;
		}
	}
	return 0;
}

static int
test_nack(void)
{
	struct fake_bus bus = { .scl_high = true };
	bus.sda_script[0] = true;  /* idle bus */
	bus.sda_script[1] = true;  /* address NACK */
	bus.sda_count = 2;
	uint8_t value = 0;
	int result = beryllium_tas_i2c_read_reg(&fake_ops, &bus, 0x4d, 0x7d, &value);
	if (result != BERYLLIUM_TAS_I2C_NACK || bus.sda_index != 2) {
		fprintf(stderr, "NACK not propagated: result=%d reads=%u\n",
		    result, bus.sda_index);
		return 2;
	}
	return 0;
}

static int
test_write_reg_ack(void)
{
	struct fake_bus bus = { .scl_high = false };
	const bool response[] = { true, false, false, false };
	for (unsigned i = 0; i < sizeof(response) / sizeof(response[0]); ++i)
		bus.sda_script[i] = response[i];
	bus.sda_count = sizeof(response) / sizeof(response[0]);
	int result = beryllium_tas_i2c_write_reg(&fake_ops, &bus, 0x4c, 0x02,
	    0xa5);
	bool expected[96];
	unsigned expected_count = 0;
	expect_sda(expected, &expected_count, true); /* idle / START setup */
	expect_sda(expected, &expected_count, false); /* START */
	expect_write_byte(expected, &expected_count, 0x98);
	expect_write_byte(expected, &expected_count, 0x02);
	expect_write_byte(expected, &expected_count, 0xa5);
	expect_sda(expected, &expected_count, false); /* STOP */
	expect_sda(expected, &expected_count, true);
	if (result != BERYLLIUM_TAS_I2C_OK ||
	    bus.sda_index != bus.sda_count ||
	    bus.sda_trace_count != expected_count || bus.scl_rises != 29) {
		fprintf(stderr, "register write failed: result=%d reads=%u/%u trace=%u/%u scl-rises=%u\n",
		    result, bus.sda_index, bus.sda_count, bus.sda_trace_count,
		    expected_count, bus.scl_rises);
		return 6;
	}
	for (unsigned i = 0; i < expected_count; ++i) {
		if (bus.sda_trace[i] != expected[i]) {
			fprintf(stderr, "wrong write SDA event %u: got=%u expected=%u\n",
			    i, bus.sda_trace[i], expected[i]);
			return 7;
		}
	}
	return 0;
}

static int
test_write_reg_nack_and_invalid_arguments(void)
{
	struct fake_bus bus = { .scl_high = true };
	bus.sda_script[0] = true;  /* idle */
	bus.sda_script[1] = false; /* address ACK */
	bus.sda_script[2] = true;  /* register NACK */
	bus.sda_count = 3;
	int result = beryllium_tas_i2c_write_reg(&fake_ops, &bus, 0x4d, 0x10,
	    0x77);
	if (result != BERYLLIUM_TAS_I2C_NACK || bus.sda_index != 3 ||
	    bus.sda_trace_count == 0 || bus.sda_trace[bus.sda_trace_count - 1] != true) {
		fprintf(stderr, "write NACK not propagated/stopped: result=%d reads=%u trace=%u\n",
		    result, bus.sda_index, bus.sda_trace_count);
		return 8;
	}
	if (beryllium_tas_i2c_write_reg(&fake_ops, 0, 0x4c, 0x03, 0x01) !=
	    BERYLLIUM_TAS_I2C_INVALID ||
	    beryllium_tas_i2c_write_reg(&fake_ops, &bus, 0x80, 0x03, 0x01) !=
	    BERYLLIUM_TAS_I2C_INVALID) {
		fputs("invalid write arguments were accepted\n", stderr);
		return 9;
	}
	return 0;
}

static int
test_stuck_scl_times_out(void)
{
	struct fake_bus bus = { .scl_high = false, .scl_stuck_low = true };
	uint8_t value = 0;
	int result = beryllium_tas_i2c_read_reg(&fake_ops, &bus, 0x4c, 0x03, &value);
	if (result != BERYLLIUM_TAS_I2C_TIMEOUT || bus.delay_us > 1100) {
		fprintf(stderr, "stuck SCL did not time out: result=%d delay=%u\n",
		    result, bus.delay_us);
		return 3;
	}
	return 0;
}

static int
test_bus_busy_and_invalid_arguments(void)
{
	struct fake_bus bus = { .scl_high = true };
	bus.sda_script[0] = false;
	bus.sda_count = 1;
	uint8_t value = 0;
	int result = beryllium_tas_i2c_read_reg(&fake_ops, &bus, 0x4c, 0x03, &value);
	if (result != BERYLLIUM_TAS_I2C_BUS_BUSY || bus.sda_drives_low != 0) {
		fprintf(stderr, "busy bus was driven: result=%d drives=%u\n",
		    result, bus.sda_drives_low);
		return 4;
	}
	if (beryllium_tas_i2c_read_reg(&fake_ops, 0, 0x4c, 0x03, &value) !=
	    BERYLLIUM_TAS_I2C_INVALID ||
	    beryllium_tas_i2c_read_reg(&fake_ops, &bus, 0x80, 0x03, &value) !=
	    BERYLLIUM_TAS_I2C_INVALID) {
		fputs("invalid bus arguments were accepted\n", stderr);
		return 5;
	}
	return 0;
}

int
main(void)
{
	if (test_read_ack(0x4c, 0x03) || test_read_ack(0x4d, 0x7d) ||
	    test_nack() || test_write_reg_ack() ||
	    test_write_reg_nack_and_invalid_arguments() ||
	    test_stuck_scl_times_out() ||
	    test_bus_busy_and_invalid_arguments())
		return 1;
	puts("TAS I2C helper checks passed");
	return 0;
}
