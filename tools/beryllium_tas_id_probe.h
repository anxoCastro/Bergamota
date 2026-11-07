#ifndef BERYLLIUM_TAS_ID_PROBE_H
#define BERYLLIUM_TAS_ID_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BERYLLIUM_TAS_I2C_HALF_PERIOD_US 5u
#define BERYLLIUM_TAS_I2C_STRETCH_LIMIT_US 1000u

enum beryllium_tas_i2c_result {
	BERYLLIUM_TAS_I2C_OK = 0,
	BERYLLIUM_TAS_I2C_NACK = -1,
	BERYLLIUM_TAS_I2C_TIMEOUT = -2,
	BERYLLIUM_TAS_I2C_BUS_BUSY = -3,
	BERYLLIUM_TAS_I2C_INVALID = -4,
};

struct beryllium_tas_i2c_ops {
	void (*set_sda_release)(void *context, bool release);
	void (*set_scl_release)(void *context, bool release);
	bool (*read_sda)(void *context);
	bool (*read_scl)(void *context);
	void (*delay_us)(void *context, unsigned microseconds);
};

static bool
beryllium_tas_i2c_ops_valid(const struct beryllium_tas_i2c_ops *ops)
{
	return ops != 0 && ops->set_sda_release != 0 &&
	    ops->set_scl_release != 0 && ops->read_sda != 0 &&
	    ops->read_scl != 0 && ops->delay_us != 0;
}

static bool
beryllium_tas_i2c_scl_high(const struct beryllium_tas_i2c_ops *ops,
	void *context)
{
	ops->set_scl_release(context, true);
	for (unsigned elapsed = 0; elapsed < BERYLLIUM_TAS_I2C_STRETCH_LIMIT_US;
	    ++elapsed) {
		if (ops->read_scl(context)) {
			ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
			return true;
		}
		ops->delay_us(context, 1u);
	}
	return false;
}

static int
beryllium_tas_i2c_start(const struct beryllium_tas_i2c_ops *ops,
	void *context)
{
	ops->set_sda_release(context, true);
	if (!beryllium_tas_i2c_scl_high(ops, context))
		return BERYLLIUM_TAS_I2C_TIMEOUT;
	if (!ops->read_sda(context))
		return BERYLLIUM_TAS_I2C_BUS_BUSY;
	ops->set_sda_release(context, false);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
	ops->set_scl_release(context, false);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
	return BERYLLIUM_TAS_I2C_OK;
}

static void
beryllium_tas_i2c_stop(const struct beryllium_tas_i2c_ops *ops,
	void *context)
{
	ops->set_sda_release(context, false);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
	(void)beryllium_tas_i2c_scl_high(ops, context);
	ops->set_sda_release(context, true);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
}

static int
beryllium_tas_i2c_write_byte(const struct beryllium_tas_i2c_ops *ops,
	void *context, uint8_t value)
{
	for (int bit = 7; bit >= 0; --bit) {
		ops->set_sda_release(context, (value & (1u << bit)) != 0);
		ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
		if (!beryllium_tas_i2c_scl_high(ops, context))
			return BERYLLIUM_TAS_I2C_TIMEOUT;
		ops->set_scl_release(context, false);
	}
	ops->set_sda_release(context, true);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
	if (!beryllium_tas_i2c_scl_high(ops, context))
		return BERYLLIUM_TAS_I2C_TIMEOUT;
	bool ack = !ops->read_sda(context);
	ops->set_scl_release(context, false);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
	return ack ? BERYLLIUM_TAS_I2C_OK : BERYLLIUM_TAS_I2C_NACK;
}

static int
beryllium_tas_i2c_read_byte(const struct beryllium_tas_i2c_ops *ops,
	void *context, uint8_t *value)
{
	uint8_t result = 0;
	ops->set_sda_release(context, true);
	for (int bit = 7; bit >= 0; --bit) {
		ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
		if (!beryllium_tas_i2c_scl_high(ops, context))
			return BERYLLIUM_TAS_I2C_TIMEOUT;
		if (ops->read_sda(context))
			result |= (uint8_t)(1u << bit);
		ops->set_scl_release(context, false);
	}
	/* Master NACKs the one-byte identity read, then emits STOP. */
	ops->set_sda_release(context, true);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
	if (!beryllium_tas_i2c_scl_high(ops, context))
		return BERYLLIUM_TAS_I2C_TIMEOUT;
	ops->set_scl_release(context, false);
	ops->set_sda_release(context, true);
	*value = result;
	return BERYLLIUM_TAS_I2C_OK;
}

static int
beryllium_tas_i2c_read_reg(const struct beryllium_tas_i2c_ops *ops,
	void *context, uint8_t address, uint8_t reg, uint8_t *value)
{
	if (!beryllium_tas_i2c_ops_valid(ops) || context == 0 || value == 0 ||
	    address > 0x7fu)
		return BERYLLIUM_TAS_I2C_INVALID;
	int result = beryllium_tas_i2c_start(ops, context);
	if (result != BERYLLIUM_TAS_I2C_OK) {
		ops->set_sda_release(context, true);
		ops->set_scl_release(context, true);
		return result;
	}
	result = beryllium_tas_i2c_write_byte(ops, context,
	    (uint8_t)(address << 1));
	if (result == BERYLLIUM_TAS_I2C_OK)
		result = beryllium_tas_i2c_write_byte(ops, context, reg);
	if (result != BERYLLIUM_TAS_I2C_OK) {
		beryllium_tas_i2c_stop(ops, context);
		return result;
	}
	result = beryllium_tas_i2c_start(ops, context);
	if (result != BERYLLIUM_TAS_I2C_OK) {
		beryllium_tas_i2c_stop(ops, context);
		return result;
	}
	result = beryllium_tas_i2c_write_byte(ops, context,
	    (uint8_t)((address << 1) | 1u));
	if (result != BERYLLIUM_TAS_I2C_OK) {
		beryllium_tas_i2c_stop(ops, context);
		return result;
	}
	result = beryllium_tas_i2c_read_byte(ops, context, value);
	if (result != BERYLLIUM_TAS_I2C_OK) {
		beryllium_tas_i2c_stop(ops, context);
		return result;
	}
	beryllium_tas_i2c_stop(ops, context);
	return BERYLLIUM_TAS_I2C_OK;
}

static int
beryllium_tas_i2c_write_reg(const struct beryllium_tas_i2c_ops *ops,
	void *context, uint8_t address, uint8_t reg, uint8_t value)
{
	if (!beryllium_tas_i2c_ops_valid(ops) || context == 0 || address > 0x7fu)
		return BERYLLIUM_TAS_I2C_INVALID;
	int result = beryllium_tas_i2c_start(ops, context);
	if (result != BERYLLIUM_TAS_I2C_OK) {
		ops->set_sda_release(context, true);
		ops->set_scl_release(context, true);
		return result;
	}
	result = beryllium_tas_i2c_write_byte(ops, context,
	    (uint8_t)(address << 1));
	if (result == BERYLLIUM_TAS_I2C_OK)
		result = beryllium_tas_i2c_write_byte(ops, context, reg);
	if (result == BERYLLIUM_TAS_I2C_OK)
		result = beryllium_tas_i2c_write_byte(ops, context, value);
	beryllium_tas_i2c_stop(ops, context);
	return result;
}

static int
beryllium_tas_i2c_write_bulk(const struct beryllium_tas_i2c_ops *ops,
	void *context, uint8_t address, uint8_t reg, const uint8_t *values,
	size_t count)
{
	if (!beryllium_tas_i2c_ops_valid(ops) || context == 0 || values == 0 ||
	    count == 0 || count > 256 || address > 0x7fu ||
	    (size_t)reg + count > 0x80u)
		return BERYLLIUM_TAS_I2C_INVALID;
	int result = beryllium_tas_i2c_start(ops, context);
	if (result != BERYLLIUM_TAS_I2C_OK) {
		ops->set_sda_release(context, true);
		ops->set_scl_release(context, true);
		return result;
	}
	result = beryllium_tas_i2c_write_byte(ops, context,
	    (uint8_t)(address << 1));
	if (result == BERYLLIUM_TAS_I2C_OK)
		result = beryllium_tas_i2c_write_byte(ops, context, reg);
	for (size_t i = 0; result == BERYLLIUM_TAS_I2C_OK && i < count; ++i)
		result = beryllium_tas_i2c_write_byte(ops, context, values[i]);
	beryllium_tas_i2c_stop(ops, context);
	return result;
}

static int
beryllium_tas_i2c_read_byte_master(const struct beryllium_tas_i2c_ops *ops,
	void *context, uint8_t *value, bool acknowledge)
{
	uint8_t result = 0;
	ops->set_sda_release(context, true);
	for (int bit = 7; bit >= 0; --bit) {
		ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
		if (!beryllium_tas_i2c_scl_high(ops, context))
			return BERYLLIUM_TAS_I2C_TIMEOUT;
		if (ops->read_sda(context))
			result |= (uint8_t)(1u << bit);
		ops->set_scl_release(context, false);
	}
	ops->set_sda_release(context, !acknowledge);
	ops->delay_us(context, BERYLLIUM_TAS_I2C_HALF_PERIOD_US);
	if (!beryllium_tas_i2c_scl_high(ops, context))
		return BERYLLIUM_TAS_I2C_TIMEOUT;
	ops->set_scl_release(context, false);
	ops->set_sda_release(context, true);
	*value = result;
	return BERYLLIUM_TAS_I2C_OK;
}

static int
beryllium_tas_i2c_read_bulk(const struct beryllium_tas_i2c_ops *ops,
	void *context, uint8_t address, uint8_t reg, uint8_t *values,
	size_t count)
{
	if (!beryllium_tas_i2c_ops_valid(ops) || context == 0 || values == 0 ||
	    count == 0 || count > 256 || address > 0x7fu ||
	    (size_t)reg + count > 0x80u)
		return BERYLLIUM_TAS_I2C_INVALID;
	int result = beryllium_tas_i2c_start(ops, context);
	if (result != BERYLLIUM_TAS_I2C_OK) {
		ops->set_sda_release(context, true);
		ops->set_scl_release(context, true);
		return result;
	}
	result = beryllium_tas_i2c_write_byte(ops, context,
	    (uint8_t)(address << 1));
	if (result == BERYLLIUM_TAS_I2C_OK)
		result = beryllium_tas_i2c_write_byte(ops, context, reg);
	if (result == BERYLLIUM_TAS_I2C_OK)
		result = beryllium_tas_i2c_start(ops, context);
	if (result == BERYLLIUM_TAS_I2C_OK)
		result = beryllium_tas_i2c_write_byte(ops, context,
		    (uint8_t)((address << 1) | 1u));
	for (size_t i = 0; result == BERYLLIUM_TAS_I2C_OK && i < count; ++i)
		result = beryllium_tas_i2c_read_byte_master(ops, context, &values[i],
		    i + 1u < count);
	beryllium_tas_i2c_stop(ops, context);
	return result;
}

#endif /* BERYLLIUM_TAS_ID_PROBE_H */
