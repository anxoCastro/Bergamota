#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "beryllium_tas_id_probe.h"

struct fake_bus {
	bool scl_high;
	unsigned sda_releases;
	unsigned sda_drives_low;
	unsigned delay_us;
	unsigned sda_index;
	unsigned sda_count;
	bool sda_script[16];
};

static void
fake_set_sda(void *context, bool release)
{
	struct fake_bus *bus = context;
	if (release)
		bus->sda_releases++;
	else
		bus->sda_drives_low++;
}

static void
fake_set_scl(void *context, bool release)
{
	(void)context;
	(void)release;
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

static int
test_read_ack(void)
{
	struct fake_bus bus = { .scl_high = true };
	const bool response[] = { true, false, false, true, false,
		true, false, true, false, false, true, false, true };
	for (unsigned i = 0; i < sizeof(response) / sizeof(response[0]); ++i)
		bus.sda_script[i] = response[i];
	bus.sda_count = sizeof(response) / sizeof(response[0]);
	uint8_t value = 0;
	int result = beryllium_tas_i2c_read_reg(&fake_ops, &bus, 0x4c, 0x03, &value);
	if (result != BERYLLIUM_TAS_I2C_OK || value != 0xa5 ||
	    bus.sda_index != bus.sda_count || bus.sda_drives_low == 0 ||
	    bus.sda_releases == 0) {
		fprintf(stderr, "ACK read failed: result=%d value=%02x script=%u/%u\n",
		    result, value, bus.sda_index, bus.sda_count);
		return 1;
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
test_stuck_scl_times_out(void)
{
	struct fake_bus bus = { .scl_high = false };
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
	if (test_read_ack() || test_nack() || test_stuck_scl_times_out() ||
	    test_bus_busy_and_invalid_arguments())
		return 1;
	puts("TAS I2C helper checks passed");
	return 0;
}
