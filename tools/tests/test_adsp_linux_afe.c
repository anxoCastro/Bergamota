#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "beryllium_adsp_apr_probe.h"

static void
put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint16_t
get16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
make_afe_apr_header(uint8_t *p, uint16_t size, uint32_t opcode)
{
	memset(p, 0, size);
	put16(p, 0x0181); /* APR v1 response header, 32 bytes */
	put16(p + 2, size);
	p[4] = BERYLLIUM_ADSP_APR_AFE_SERVICE;
	p[5] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP;
	p[8] = BERYLLIUM_ADSP_APR_AFE_SERVICE;
	p[9] = BERYLLIUM_ADSP_APR_DOMAIN_APPS;
	put32(p + 12, BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN);
	put32(p + 16, opcode);
}

int
main(void)
{
	uint8_t clock_packet[BERYLLIUM_ADSP_AFE_CLOCK_SET_PACKET_BYTES];
	uint8_t port_packet[BERYLLIUM_ADSP_AFE_I2S_SET_PARAM_PACKET_BYTES];
	uint8_t start_packet[BERYLLIUM_ADSP_AFE_DEVICE_PACKET_BYTES];
	uint8_t stop_packet[BERYLLIUM_ADSP_AFE_DEVICE_PACKET_BYTES];
	uint8_t response_packet[40];
	uint8_t get_status_packet[80];
	struct beryllium_adsp_afe_basic_result basic_result = { 0 };
	struct beryllium_adsp_afe_response get_response = { 0 };
	size_t packet_bytes = 0;

	assert(beryllium_adsp_apr_encode_afe_clock_set(clock_packet,
	    sizeof(clock_packet), &packet_bytes, true));
	assert(packet_bytes == 68);
	assert(get16(clock_packet) == BERYLLIUM_ADSP_APR_SEQ_CMD_HDR_FIELD);
	assert(get16(clock_packet + 2) == packet_bytes);
	assert(clock_packet[4] == BERYLLIUM_ADSP_APR_AFE_SERVICE &&
	    clock_packet[5] == BERYLLIUM_ADSP_APR_DOMAIN_APPS);
	assert(clock_packet[8] == BERYLLIUM_ADSP_APR_AFE_SERVICE &&
	    clock_packet[9] == BERYLLIUM_ADSP_APR_DOMAIN_ADSP);
	assert(get32(clock_packet + 12) == BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN);
	assert(get32(clock_packet + 16) == BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM);
	assert(get32(clock_packet + 20) == 32);
	assert(get32(clock_packet + 24) == 0 && get32(clock_packet + 28) == 0 &&
	    get32(clock_packet + 32) == 0);
	assert(get32(clock_packet + 36) == BERYLLIUM_ADSP_AFE_MODULE_CLOCK_SET);
	assert(get32(clock_packet + 40) == BERYLLIUM_ADSP_AFE_PARAM_CLOCK_SET);
	assert(get16(clock_packet + 44) == 20 && get16(clock_packet + 46) == 0);
	assert(get32(clock_packet + 48) == 1);
	assert(get32(clock_packet + 52) == 0x106);
	assert(get32(clock_packet + 56) == 1536000);
	assert(get16(clock_packet + 60) == 1 && get16(clock_packet + 62) == 0);
	assert(get32(clock_packet + 64) == 1);

	assert(beryllium_adsp_apr_encode_afe_i2s_set_param(port_packet,
	    sizeof(port_packet), &packet_bytes));
	assert(packet_bytes == 84);
	assert(get16(port_packet) == BERYLLIUM_ADSP_APR_SEQ_CMD_HDR_FIELD);
	assert(get16(port_packet + 2) == packet_bytes);
	assert(get32(port_packet + 12) == BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN);
	assert(get32(port_packet + 16) == BERYLLIUM_ADSP_AFE_CMD_SET_PARAM_V2);
	assert(get16(port_packet + 20) == BERYLLIUM_ADSP_AFE_PORT_QUAT_MI2S_RX);
	assert(get16(port_packet + 22) == 48);
	assert(get32(port_packet + 24) == 0 && get32(port_packet + 28) == 0 &&
	    get32(port_packet + 32) == 0);
	assert(get32(port_packet + 36) == BERYLLIUM_ADSP_AFE_MODULE_AUDIO_DEV_INTERFACE);
	assert(get32(port_packet + 40) == BERYLLIUM_ADSP_AFE_PARAM_I2S_CONFIG);
	assert(get16(port_packet + 44) == 36 && get16(port_packet + 46) == 0);
	assert(get32(port_packet + 48) == 1);
	assert(get16(port_packet + 52) == 16);
	assert(get16(port_packet + 54) == 1);
	assert(get16(port_packet + 56) == 1);
	assert(get16(port_packet + 58) == 1);
	assert(get32(port_packet + 60) == 48000);
	assert(get16(port_packet + 64) == 0 && get16(port_packet + 66) == 0);
	for (size_t i = 68; i < sizeof(port_packet); i++) {
		assert(port_packet[i] == 0);
	}

	assert(beryllium_adsp_apr_encode_afe_device_command(start_packet,
	    sizeof(start_packet), &packet_bytes, true));
	assert(packet_bytes == 24);
	assert(get16(start_packet + 2) == packet_bytes);
	assert(get32(start_packet + 16) == BERYLLIUM_ADSP_AFE_CMD_DEVICE_START);
	assert(get16(start_packet + 20) == BERYLLIUM_ADSP_AFE_PORT_QUAT_MI2S_RX);
	assert(get16(start_packet + 22) == 0);
	assert(beryllium_adsp_apr_encode_afe_device_command(stop_packet,
	    sizeof(stop_packet), &packet_bytes, false));
	assert(packet_bytes == 24);
	assert(get16(stop_packet + 2) == packet_bytes);
	assert(get32(stop_packet + 16) == BERYLLIUM_ADSP_AFE_CMD_DEVICE_STOP);
	assert(get16(stop_packet + 20) == BERYLLIUM_ADSP_AFE_PORT_QUAT_MI2S_RX);
	assert(get16(stop_packet + 22) == 0);

	make_afe_apr_header(response_packet, sizeof(response_packet),
	    BERYLLIUM_ADSP_APR_BASIC_RSP_RESULT);
	put32(response_packet + 32, BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM);
	put32(response_packet + 36, 0);
	put32(response_packet + 12, 1024u); /* Global clock token is not used by QUAT. */
	assert(beryllium_adsp_apr_parse_afe_basic_result(response_packet,
	    sizeof(response_packet), BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM,
	    &basic_result) == BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	put32(response_packet + 12, BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN);
	assert(beryllium_adsp_apr_parse_afe_basic_result(response_packet,
	    sizeof(response_packet), BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM,
	    &basic_result) == BERYLLIUM_ADSP_AFE_RESPONSE_BASIC_RESULT);
	assert(basic_result.request_opcode == BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM &&
	    basic_result.status == 0);
	put32(response_packet + 36, 2);
	assert(beryllium_adsp_apr_parse_afe_basic_result(response_packet,
	    sizeof(response_packet), BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM,
	    &basic_result) == BERYLLIUM_ADSP_AFE_RESPONSE_BASIC_RESULT);
	assert(basic_result.status == 2);
	assert(beryllium_adsp_apr_parse_afe_basic_result(response_packet,
	    sizeof(response_packet), BERYLLIUM_ADSP_AFE_CMD_SET_PARAM_V2,
	    &basic_result) == BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	put32(response_packet + 36, 0);
	put32(response_packet + 12, BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN + 1);
	assert(beryllium_adsp_apr_parse_afe_basic_result(response_packet,
	    sizeof(response_packet), BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM,
	    &basic_result) == BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	put32(response_packet + 12, BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN);
	response_packet[4]++;
	assert(beryllium_adsp_apr_parse_afe_basic_result(response_packet,
	    sizeof(response_packet), BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM,
	    &basic_result) == BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	response_packet[4]--;
	assert(beryllium_adsp_apr_parse_afe_basic_result(response_packet,
	    sizeof(response_packet) - 1, BERYLLIUM_ADSP_AFE_CMD_SVC_SET_PARAM,
	    &basic_result) == BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);

	make_afe_apr_header(get_status_packet, sizeof(get_status_packet),
	    BERYLLIUM_ADSP_AFE_RSP_GET_PARAM_V2);
	put32(get_status_packet + 32, 2);
	put32(get_status_packet + 36, BERYLLIUM_ADSP_AFE_MODULE_AUDIO_DEV_INTERFACE);
	put32(get_status_packet + 40, BERYLLIUM_ADSP_AFE_PARAM_I2S_CONFIG);
	put16(get_status_packet + 44, 0);
	put16(get_status_packet + 46, 0);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(get_status_packet,
	    sizeof(get_status_packet), &get_response) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_STATUS);
	assert(get_response.status == 2 && get_response.i2s.sample_rate == 0);

	puts("Linux AFE packet helper checks passed");
	return 0;
}
