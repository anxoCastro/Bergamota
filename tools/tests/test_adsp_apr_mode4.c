#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "beryllium_adsp_glink_start.h"
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

static void
make_apr_header(uint8_t *p, uint16_t size, uint32_t opcode)
{
	memset(p, 0, size);
	put16(p, BERYLLIUM_ADSP_APR_RSP_HDR_FIELD);
	put16(p + 2, size);
	p[4] = BERYLLIUM_ADSP_APR_Q6CORE_SERVICE;
	p[5] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP;
	p[8] = BERYLLIUM_ADSP_APR_Q6CORE_SERVICE;
	p[9] = BERYLLIUM_ADSP_APR_DOMAIN_APPS;
	put32(p + 16, opcode);
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

static void
make_linux_apr_header(uint8_t *p, uint16_t size, uint16_t src_port,
	uint16_t dest_port, uint32_t token, uint32_t opcode)
{
	memset(p, 0, size);
	put16(p, 0x0181); /* APR v1 response with a 32-byte header */
	put16(p + 2, size);
	put16(p + 6, src_port);
	put16(p + 10, dest_port);
	put32(p + 12, token);
	put32(p + 16, opcode);
}

static void
set_linux_apr_reply_route(uint8_t *p, uint8_t service)
{
	p[4] = service;
	p[5] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP;
	p[8] = service;
	p[9] = BERYLLIUM_ADSP_APR_DOMAIN_APPS;
}

static void
test_linux_asm_adm_packets(void)
{
	uint8_t packet[64] = { 0 };
	uint8_t response[64] = { 0 };
	size_t packet_bytes = 0;
	uint32_t value = 0;
	uint16_t copp_id = 0;
	struct beryllium_adsp_apr_response service_table = { 0 };
	uint8_t malformed_map_response[36];
	const uint64_t dma_base = 0x0000000110000000ULL;
	const uint64_t dma_period2 = 0x0000000110002580ULL;
	const uint8_t captured_asm_map_response[36] = {
	    0x81, 0x01, 0x24, 0x00, 0x07, 0x04, 0x00, 0x00,
	    0x07, 0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	    0x93, 0x0d, 0x01, 0x00, 0x02, 0x02, 0x00, 0x04,
	    0x95, 0x0f, 0xbb, 0x03, 0x00, 0x00, 0x00, 0x00,
	    0xb8, 0x3f, 0xa9, 0xb0,
	};
	const uint8_t captured_asm_map_echo_token_response[36] = {
	    0x81, 0x01, 0x24, 0x00, 0x07, 0x04, 0x00, 0x00,
	    0x07, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	    0x93, 0x0d, 0x01, 0x00, 0x02, 0x02, 0x00, 0x04,
	    0x32, 0xe7, 0xd6, 0xe4, 0x00, 0x00, 0x00, 0x00,
	    0xb8, 0x3f, 0xa9, 0xb0,
	};
	const uint8_t captured_adm_open_zero_copp_response[40] = {
	    0x81, 0x01, 0x28, 0x00, 0x08, 0x04, 0x06, 0x10,
	    0x08, 0x05, 0x06, 0x10, 0x00, 0x00, 0x16, 0x00,
	    0x29, 0x03, 0x01, 0x00, 0x02, 0x04, 0x00, 0x04,
	    0x89, 0xc6, 0x1c, 0x03, 0x00, 0x00, 0x00, 0x00,
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	const uint8_t captured_asm_write_done_event[48] = {
	    0x81, 0x00, 0x30, 0x00, 0x07, 0x04, 0x01, 0x01,
	    0x07, 0x05, 0x01, 0x01, 0x00, 0x00, 0xc0, 0x12,
	    0x99, 0x0d, 0x01, 0x00, 0x02, 0x0b, 0x00, 0x04,
	    0xb3, 0x9b, 0xd5, 0x03, 0x00, 0x00, 0x00, 0x00,
	    0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x00, 0x00,
	    0xb8, 0x3f, 0xa9, 0xb0, 0x00, 0x00, 0x00, 0x00,
	};

	assert(beryllium_adsp_apr_encode_asm_map_regions(packet, sizeof(packet),
	    &packet_bytes, dma_base, 20480));
	assert(packet_bytes == 40 && beryllium_adsp_apr_get16(packet) == 0x0250);
	assert(beryllium_adsp_apr_get16(packet + 2) == 40);
	assert(beryllium_adsp_apr_get16(packet + 6) == 0 &&
	    beryllium_adsp_apr_get16(packet + 10) == 0);
	assert(beryllium_adsp_apr_get32(packet + 12) == 0x100);
	assert(beryllium_adsp_apr_get32(packet + 16) == 0x10d92);
	assert(beryllium_adsp_apr_get16(packet + 20) == 3 &&
	    beryllium_adsp_apr_get16(packet + 22) == 1);
	assert(beryllium_adsp_apr_get32(packet + 24) == 0);
	assert(beryllium_adsp_apr_get32(packet + 28) == 0x10000000 &&
	    beryllium_adsp_apr_get32(packet + 32) == 1 &&
	    beryllium_adsp_apr_get32(packet + 36) == 20480);
	assert(!beryllium_adsp_apr_encode_asm_map_regions(packet,
	    39, &packet_bytes, dma_base, 20480));
	assert(!beryllium_adsp_apr_encode_asm_map_regions(packet, sizeof(packet),
	    &packet_bytes, dma_base, 20479));

	assert(beryllium_adsp_apr_encode_asm_open_write(packet, sizeof(packet),
	    &packet_bytes));
	assert(packet_bytes == 36 && beryllium_adsp_apr_get16(packet + 6) == 0x101 &&
	    beryllium_adsp_apr_get16(packet + 10) == 0x101);
	assert(beryllium_adsp_apr_get32(packet + 12) == 1 &&
	    beryllium_adsp_apr_get32(packet + 16) == 0x10db3);
	assert(beryllium_adsp_apr_get32(packet + 20) == 0 &&
	    beryllium_adsp_apr_get16(packet + 24) == 0 &&
	    beryllium_adsp_apr_get16(packet + 26) == 16 &&
	    beryllium_adsp_apr_get32(packet + 28) == 0x10c68 &&
	    beryllium_adsp_apr_get32(packet + 32) == 0x10da5);

	assert(beryllium_adsp_apr_encode_asm_pcm_format(packet, sizeof(packet),
	    &packet_bytes));
	assert(packet_bytes == 44 && beryllium_adsp_apr_get16(packet + 6) == 0x101 &&
	    beryllium_adsp_apr_get32(packet + 16) == 0x10d98);
	assert(beryllium_adsp_apr_get32(packet + 20) == 20 &&
	    beryllium_adsp_apr_get16(packet + 24) == 2 &&
	    beryllium_adsp_apr_get16(packet + 26) == 16 &&
	    beryllium_adsp_apr_get32(packet + 28) == 48000 &&
	    beryllium_adsp_apr_get16(packet + 32) == 1 &&
	    beryllium_adsp_apr_get16(packet + 34) == 0);
	assert(packet[36] == 1 && packet[37] == 2 && packet[38] == 0 &&
	    packet[43] == 0);

	assert(beryllium_adsp_apr_encode_adm_open_quat(packet, sizeof(packet),
	    &packet_bytes, 0));
	assert(packet_bytes == 48 && beryllium_adsp_apr_get16(packet + 6) == 0x1006 &&
	    beryllium_adsp_apr_get16(packet + 10) == 0x1006);
	assert(beryllium_adsp_apr_get32(packet + 16) == 0x10326 &&
	    beryllium_adsp_apr_get32(packet + 12) == (22u << 16));
	assert(beryllium_adsp_apr_get16(packet + 22) == 1 &&
	    beryllium_adsp_apr_get16(packet + 24) == 0x1006 &&
	    beryllium_adsp_apr_get32(packet + 28) == 0x10312 &&
	    beryllium_adsp_apr_get16(packet + 32) == 2 &&
	    beryllium_adsp_apr_get16(packet + 34) == 16 &&
	    beryllium_adsp_apr_get32(packet + 36) == 48000);
	assert(packet[40] == 1 && packet[41] == 2 && packet[42] == 0 &&
	    packet[47] == 0);

	assert(beryllium_adsp_apr_encode_adm_matrix_map(packet, sizeof(packet),
	    &packet_bytes, 7));
	assert(packet_bytes == 36 && beryllium_adsp_apr_get32(packet + 16) == 0x10325 &&
	    beryllium_adsp_apr_get32(packet + 20) == 0 &&
	    beryllium_adsp_apr_get32(packet + 24) == 1 &&
	    beryllium_adsp_apr_get16(packet + 28) == 1 &&
	    beryllium_adsp_apr_get16(packet + 30) == 1 &&
	    beryllium_adsp_apr_get16(packet + 32) == 7 &&
	    beryllium_adsp_apr_get16(packet + 34) == 0);
	assert(beryllium_adsp_apr_encode_adm_matrix_map(packet, sizeof(packet),
	    &packet_bytes, 0));
	assert(packet_bytes == 36 && beryllium_adsp_apr_get16(packet + 32) == 0);
	assert(!beryllium_adsp_apr_encode_adm_matrix_map(packet, sizeof(packet),
	    &packet_bytes, 0x00ffu));

	assert(beryllium_adsp_apr_encode_asm_run(packet, sizeof(packet),
	    &packet_bytes));
	assert(packet_bytes == 32 && beryllium_adsp_apr_get16(packet + 6) == 0x101 &&
	    beryllium_adsp_apr_get32(packet + 12) == 1 &&
	    beryllium_adsp_apr_get32(packet + 16) == 0x10daa);
	assert(beryllium_adsp_apr_get32(packet + 20) == 0 &&
	    beryllium_adsp_apr_get32(packet + 24) == 0 &&
	    beryllium_adsp_apr_get32(packet + 28) == 0);

	assert(beryllium_adsp_apr_encode_asm_write(packet, sizeof(packet),
	    &packet_bytes, dma_period2, 0x55, 4800, 2));
	assert(packet_bytes == 52 && beryllium_adsp_apr_get16(packet + 6) == 0x101 &&
	    beryllium_adsp_apr_get16(packet + 10) == 0x101);
	assert(beryllium_adsp_apr_get32(packet + 12) == 0x12c00002 &&
	    beryllium_adsp_apr_get32(packet + 16) == 0x10dab);
	assert(beryllium_adsp_apr_get32(packet + 20) == 0x10002580 &&
	    beryllium_adsp_apr_get32(packet + 24) == 1 &&
	    beryllium_adsp_apr_get32(packet + 28) == 0x55 &&
	    beryllium_adsp_apr_get32(packet + 32) == 4800 &&
	    beryllium_adsp_apr_get32(packet + 36) == 2 &&
	    beryllium_adsp_apr_get32(packet + 40) == 0 &&
	    beryllium_adsp_apr_get32(packet + 44) == 0 &&
	    beryllium_adsp_apr_get32(packet + 48) == 0);

	assert(beryllium_adsp_apr_encode_asm_close(packet, sizeof(packet),
	    &packet_bytes));
	assert(packet_bytes == 20 && beryllium_adsp_apr_get16(packet + 6) == 0x101 &&
	    beryllium_adsp_apr_get32(packet + 12) == 1 &&
	    beryllium_adsp_apr_get32(packet + 16) == 0x10bcd);
	assert(beryllium_adsp_apr_encode_asm_unmap(packet, sizeof(packet),
	    &packet_bytes, 0x55));
	assert(packet_bytes == 24 && beryllium_adsp_apr_get16(packet + 6) == 0 &&
	    beryllium_adsp_apr_get32(packet + 12) == 0x100 &&
	    beryllium_adsp_apr_get32(packet + 16) == 0x10d94 &&
	    beryllium_adsp_apr_get32(packet + 20) == 0x55);
	assert(beryllium_adsp_apr_encode_adm_close(packet, sizeof(packet),
	    &packet_bytes, 7, 0));
	assert(packet_bytes == 20 && beryllium_adsp_apr_get16(packet + 6) == 22 &&
	    beryllium_adsp_apr_get16(packet + 10) == 7 &&
	    beryllium_adsp_apr_get32(packet + 12) == (22u << 16) &&
	    beryllium_adsp_apr_get32(packet + 16) == 0x10327);
	assert(beryllium_adsp_apr_encode_adm_close(packet, sizeof(packet),
	    &packet_bytes, 0, 0));
	assert(packet_bytes == 20 && beryllium_adsp_apr_get16(packet + 10) == 0);
	assert(!beryllium_adsp_apr_encode_adm_close(packet, sizeof(packet),
	    &packet_bytes, 0x00ffu, 0));

	make_linux_apr_header(response, 40, 0, 0, 0x100, 0x10d93);
	set_linux_apr_reply_route(response, BERYLLIUM_ADSP_APR_ASM_SERVICE);
	put32(response + 32, 0x55);
	assert(beryllium_adsp_apr_parse_asm_map_result(response, 40, 0x100,
	    &value) && value == 0x55);
	assert(beryllium_adsp_apr_parse_asm_map_result(captured_asm_map_response,
	    sizeof(captured_asm_map_response), 0x100, &value) &&
	    value == 0xb0a93fb8);
	assert(beryllium_adsp_apr_parse_asm_map_result(
	    captured_asm_map_echo_token_response,
	    sizeof(captured_asm_map_echo_token_response), 0x100, &value) &&
	    value == 0xb0a93fb8);
	memcpy(malformed_map_response, captured_asm_map_response,
	    sizeof(malformed_map_response));
	put32(malformed_map_response + 12, 2);
	assert(!beryllium_adsp_apr_parse_asm_map_result(malformed_map_response,
	    sizeof(malformed_map_response), 0x100, &value));
	memcpy(malformed_map_response, captured_asm_map_echo_token_response,
	    sizeof(malformed_map_response));
	put32(malformed_map_response + 12, 2);
	assert(!beryllium_adsp_apr_parse_asm_map_result(malformed_map_response,
	    sizeof(malformed_map_response), 0x100, &value));
	memcpy(malformed_map_response, captured_asm_map_response,
	    sizeof(malformed_map_response));
	put32(malformed_map_response + 32, 0);
	assert(!beryllium_adsp_apr_parse_asm_map_result(malformed_map_response,
	    sizeof(malformed_map_response), 0x100, &value));
	assert(!beryllium_adsp_apr_parse_asm_map_result(captured_asm_map_response,
	    sizeof(captured_asm_map_response) - 1, 0x100, &value));
	memset(response + 4, 0, 2);
	memset(response + 8, 0, 2);
	assert(!beryllium_adsp_apr_parse_asm_map_result(response, 40, 0x100,
	    &value));
	set_linux_apr_reply_route(response, BERYLLIUM_ADSP_APR_ASM_SERVICE);
	put32(response + 12, 0x101);
	assert(!beryllium_adsp_apr_parse_asm_map_result(response, 40, 0x100,
	    &value));

	make_linux_apr_header(response, 40, 0, 0, 22u << 16, 0x10329);
	set_linux_apr_reply_route(response, BERYLLIUM_ADSP_APR_ADM_SERVICE);
	put32(response + 32, 0);
	beryllium_adsp_apr_put16(response + 36, 7);
	beryllium_adsp_apr_put16(response + 38, 0);
	assert(beryllium_adsp_apr_parse_adm_open_result(response, 40, 22u << 16,
	    &copp_id) && copp_id == 7);
	response[4] = BERYLLIUM_ADSP_APR_ASM_SERVICE;
	assert(!beryllium_adsp_apr_parse_adm_open_result(response, 40,
	    22u << 16, &copp_id));
	set_linux_apr_reply_route(response, BERYLLIUM_ADSP_APR_ADM_SERVICE);
	put32(response + 32, 1);
	assert(!beryllium_adsp_apr_parse_adm_open_result(response, 40, 22u << 16,
	    &copp_id));
	assert(beryllium_adsp_apr_parse_adm_open_result(
	    captured_adm_open_zero_copp_response,
	    sizeof(captured_adm_open_zero_copp_response), 0x00160000u,
	    &copp_id) && copp_id == 0);
	memcpy(response, captured_adm_open_zero_copp_response,
	    sizeof(captured_adm_open_zero_copp_response));
	response[36] = 0xff;
	assert(!beryllium_adsp_apr_parse_adm_open_result(response, 40,
	    0x00160000u, &copp_id));

	make_linux_apr_header(response, 40, 0, 0x101, 1, 0x110e8);
	set_linux_apr_reply_route(response, BERYLLIUM_ADSP_APR_ASM_SERVICE);
	put32(response + 32, 0x10db3);
	put32(response + 36, 0);
	assert(beryllium_adsp_apr_parse_linux_basic_result(response, 40,
	    BERYLLIUM_ADSP_APR_ASM_SERVICE, 0x101, 1, 0x10db3, &value) &&
	    value == 0);
	response[4] = BERYLLIUM_ADSP_APR_ADM_SERVICE;
	assert(!beryllium_adsp_apr_parse_linux_basic_result(response, 40,
	    BERYLLIUM_ADSP_APR_ASM_SERVICE, 0x101, 1, 0x10db3, &value));
	set_linux_apr_reply_route(response, BERYLLIUM_ADSP_APR_ASM_SERVICE);
	assert(!beryllium_adsp_apr_parse_linux_basic_result(response, 40,
	    9, 0x101, 1, 0x10db3, &value));
	put32(response + 32, 0x10db4);
	assert(!beryllium_adsp_apr_parse_linux_basic_result(response, 40,
	    BERYLLIUM_ADSP_APR_ASM_SERVICE, 0x101, 1, 0x10db3, &value));

	assert(beryllium_adsp_apr_encode_adm_close(packet, sizeof(packet),
	    &packet_bytes, 0, 0));
	make_linux_apr_header(response, 40, 0, 22, 0x00160000u, 0x000110e8u);
	set_linux_apr_reply_route(response, BERYLLIUM_ADSP_APR_ADM_SERVICE);
	put32(response + 32, 0x00010327u);
	put32(response + 36, 0);
	assert(!beryllium_adsp_apr_parse_linux_basic_result(response, 40,
	    BERYLLIUM_ADSP_APR_ADM_SERVICE,
	    beryllium_adsp_apr_get16(packet + 10), 0x00160000u,
	    0x00010327u, &value));
	assert(beryllium_adsp_apr_parse_linux_basic_result(response, 40,
	    BERYLLIUM_ADSP_APR_ADM_SERVICE,
	    beryllium_adsp_apr_get16(packet + 6), 0x00160000u,
	    0x00010327u, &value) && value == 0);

	const uint32_t write_token = 0x12c00000;
	assert(beryllium_adsp_apr_parse_asm_write_done(
	    captured_asm_write_done_event, sizeof(captured_asm_write_done_event),
	    write_token, 0x0000000110000000ULL, 0, 4800));
	for (uint32_t sequence = 1; sequence < 4; sequence++) {
		uint64_t period_address = 0x0000000110000000ULL +
		    (uint64_t)sequence * 4800u;
		memcpy(response, captured_asm_write_done_event,
		    sizeof(captured_asm_write_done_event));
		put32(response + 12, write_token | sequence);
		put32(response + 32, (uint32_t)period_address);
		put32(response + 36, (uint32_t)(period_address >> 32));
		assert(beryllium_adsp_apr_parse_asm_write_done(response, 48,
		    write_token | sequence, period_address, sequence, 4800));
	}
	memcpy(response, captured_asm_write_done_event,
	    sizeof(captured_asm_write_done_event));
	put16(response, 0x0181); /* A response header is not the captured event. */
	assert(!beryllium_adsp_apr_parse_asm_write_done(response, 48,
	    write_token, 0x0000000110000000ULL, 0, 4800));
	memcpy(response, captured_asm_write_done_event,
	    sizeof(captured_asm_write_done_event));
	put16(response + 6, 0x102);
	assert(!beryllium_adsp_apr_parse_asm_write_done(response, 48,
	    write_token, 0x0000000110000000ULL, 0, 4800));
	memcpy(response, captured_asm_write_done_event,
	    sizeof(captured_asm_write_done_event));
	put16(response + 10, 0x102);
	assert(!beryllium_adsp_apr_parse_asm_write_done(response, 48,
	    write_token, 0x0000000110000000ULL, 0, 4800));
	memcpy(response, captured_asm_write_done_event,
	    sizeof(captured_asm_write_done_event));
	response[9] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP;
	assert(!beryllium_adsp_apr_parse_asm_write_done(response, 48,
	    write_token, 0x0000000110000000ULL, 0, 4800));
	memcpy(response, captured_asm_write_done_event,
	    sizeof(captured_asm_write_done_event));
	assert(!beryllium_adsp_apr_parse_asm_write_done(response, 48,
	    write_token + 1, 0x0000000110000000ULL, 0, 4800));
	assert(!beryllium_adsp_apr_parse_asm_write_done(
	    captured_asm_write_done_event, sizeof(captured_asm_write_done_event),
	    write_token, 0x0000000110000001ULL, 0, 4800));
	assert(!beryllium_adsp_apr_parse_asm_write_done(
	    captured_asm_write_done_event, sizeof(captured_asm_write_done_event),
	    write_token, 0x0000000110000000ULL, 1, 4800));
	memcpy(response, captured_asm_write_done_event,
	    sizeof(captured_asm_write_done_event));
	put16(response + 2, 44);
	assert(!beryllium_adsp_apr_parse_asm_write_done(response, 44,
	    write_token, 0x0000000110000000ULL, 0, 4800));

	service_table.service_count = 3;
	service_table.services[0].service_id = 7;
	service_table.services[0].api_version = 1;
	service_table.services[0].api_branch_version = 0;
	service_table.services[1].service_id = 8;
	service_table.services[1].api_version = 2;
	service_table.services[1].api_branch_version = 0;
	service_table.services[2].service_id = 4;
	service_table.services[2].api_version = 2;
	service_table.services[2].api_branch_version = 0;
	assert(beryllium_adsp_apr_audio_services_available(&service_table));
	service_table.services[0].api_version = 2;
	assert(!beryllium_adsp_apr_audio_services_available(&service_table));
	service_table.services[0].api_version = 1;
	service_table.services[2] = service_table.services[0];
	assert(!beryllium_adsp_apr_audio_services_available(&service_table));
}

int
main(void)
{
	uint8_t request[BERYLLIUM_ADSP_APR_HEADER_BYTES];
	uint8_t open_frame[24] = { 0 };
	uint8_t fastrpc_open_frame[32] = { 0 };
	uint8_t ipcrtr_open_frame[16] = { 0 };
	uint8_t glink_ssr_open_frame[24] = { 0 };
	uint8_t loopback_ctl_lpass_open_frame[32] = { 0 };
	uint8_t apr_apps2_open_frame[24] = { 0 };
	uint8_t intent_frame[24] = { 0 };
	const uint8_t rx_done_w_reuse_frame[8] = {
	    0x0e, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	const uint8_t captured_zero_intent_frame[16] = {
	    0x05, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00,
	    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	const uint8_t captured_apr_v1_response_prefix[64] = {
	    0x81, 0x01, 0x94, 0x00, 0x03, 0x04, 0x00, 0x00,
	    0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    0x2d, 0x29, 0x01, 0x00, 0x02, 0x01, 0x00, 0x04,
	    0x45, 0xcf, 0xc4, 0xe7, 0x00, 0x00, 0x00, 0x00,
	    0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	uint8_t data_frame[40] = { 0 };
	uint8_t response[BERYLLIUM_ADSP_APR_MAX_DATA_BYTES];
	uint8_t afe_request[BERYLLIUM_ADSP_AFE_GET_PARAM_BYTES];
	uint8_t afe_get_response[68];
	uint8_t afe_status_response[36];
	uint8_t afe_basic_response[40];
	uint8_t malformed_afe_response[68];
	uint8_t captured_apr_v1_response[148] = { 0 };
	uint8_t malformed_apr_response[148];
	struct beryllium_adsp_apr_response parsed = { 0 };
	struct beryllium_adsp_afe_response parsed_afe = { 0 };
	struct beryllium_adsp_apr_response afe_service_table = { 0 };
	struct beryllium_adsp_glink_intent intents[2];
	size_t frame_bytes = 0;
	size_t intent_count = 0;
	size_t afe_request_bytes = 0;

	assert(beryllium_adsp_apr_encode_get_fwk_version(request));
	assert(beryllium_adsp_glink_mode_valid(1));
	assert(beryllium_adsp_glink_mode_valid(2));
	assert(beryllium_adsp_glink_mode_valid(3));
	assert(beryllium_adsp_glink_mode_valid(4));
	assert(beryllium_adsp_glink_mode_valid(5));
	assert(!beryllium_adsp_glink_mode_valid(0));
	assert(!beryllium_adsp_glink_mode_valid(6));
	assert(beryllium_adsp_glink_mode_requests_pas(2));
	assert(beryllium_adsp_glink_mode_requests_pas(3));
	assert(beryllium_adsp_glink_mode_requests_pas(4));
	assert(beryllium_adsp_glink_mode_requests_pas(5));
	assert(beryllium_adsp_glink_mode_requests_glink(3));
	assert(beryllium_adsp_glink_mode_requests_glink(4));
	assert(beryllium_adsp_glink_mode_requests_glink(5));
	assert(!beryllium_adsp_glink_mode_requests_glink(2));
	assert(beryllium_adsp_glink_rx_frame_available(40, 0, 0x4000, 24));
	assert(!beryllium_adsp_glink_rx_frame_available(16, 0, 0x4000, 24));
	assert(beryllium_adsp_glink_rx_frame_available(8, 0x3ff8, 0x4000, 16));
	assert(sizeof(request) == 20);
	assert(request[0] == 0x50 && request[1] == 0x02);
	assert(request[2] == 20 && request[3] == 0);
	assert(request[4] == 3 && request[5] == 5);
	assert(request[8] == 3 && request[9] == 4);
	assert(request[16] == 0x2c && request[17] == 0x29 &&
	    request[18] == 0x01 && request[19] == 0x00);

	put16(open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	put16(open_frame + 2, 7);
	put32(open_frame + 4, BERYLLIUM_ADSP_APR_CHANNEL_NAME_BYTES);
	memcpy(open_frame + 8, BERYLLIUM_ADSP_APR_CHANNEL_NAME,
	    BERYLLIUM_ADSP_APR_CHANNEL_NAME_BYTES);
	assert(beryllium_adsp_apr_glink_frame_bytes(open_frame, sizeof(open_frame),
	    &frame_bytes));
	assert(frame_bytes == sizeof(open_frame));
	assert(beryllium_adsp_apr_open_name_valid(open_frame, frame_bytes, 7));

	put16(fastrpc_open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	put16(fastrpc_open_frame + 2, 1);
	assert(sizeof(BERYLLIUM_ADSP_FASTRPC_CHANNEL_NAME) ==
	    BERYLLIUM_ADSP_FASTRPC_CHANNEL_NAME_BYTES);
	put32(fastrpc_open_frame + 4,
	    BERYLLIUM_ADSP_FASTRPC_CHANNEL_NAME_BYTES);
	memcpy(fastrpc_open_frame + 8, BERYLLIUM_ADSP_FASTRPC_CHANNEL_NAME,
	    BERYLLIUM_ADSP_FASTRPC_CHANNEL_NAME_BYTES);
	assert(fastrpc_open_frame[29] == 0);
	assert(beryllium_adsp_apr_fastrpc_open_valid(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame), 1));
	assert(beryllium_adsp_apr_glink_frame_bytes(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame) - 8, &frame_bytes) ==
	    BERYLLIUM_ADSP_GLINK_FRAME_INCOMPLETE);
	assert(!beryllium_adsp_apr_fastrpc_open_valid(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame) - 8, 1));
	assert(!beryllium_adsp_apr_fastrpc_open_valid(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame), 0));
	assert(!beryllium_adsp_apr_fastrpc_open_valid(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame), 2));
	put32(fastrpc_open_frame + 4,
	    BERYLLIUM_ADSP_FASTRPC_CHANNEL_NAME_BYTES - 1);
	assert(!beryllium_adsp_apr_fastrpc_open_valid(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame), 1));
	put32(fastrpc_open_frame + 4,
	    BERYLLIUM_ADSP_FASTRPC_CHANNEL_NAME_BYTES);
	fastrpc_open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_fastrpc_open_valid(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame), 1));
	fastrpc_open_frame[8] ^= 1;
	fastrpc_open_frame[31] = 1;
	assert(beryllium_adsp_apr_fastrpc_open_valid(fastrpc_open_frame,
	    sizeof(fastrpc_open_frame), 1));
	put16(ipcrtr_open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	put16(ipcrtr_open_frame + 2, 2);
	put32(ipcrtr_open_frame + 4, BERYLLIUM_ADSP_IPCRTR_CHANNEL_NAME_BYTES);
	memcpy(ipcrtr_open_frame + 8, BERYLLIUM_ADSP_IPCRTR_CHANNEL_NAME,
	    BERYLLIUM_ADSP_IPCRTR_CHANNEL_NAME_BYTES);
	assert(beryllium_adsp_apr_ipcrtr_open_valid(ipcrtr_open_frame,
	    sizeof(ipcrtr_open_frame), 2));
	assert(!beryllium_adsp_apr_ipcrtr_open_valid(ipcrtr_open_frame,
	    sizeof(ipcrtr_open_frame) - 8, 2));
	assert(!beryllium_adsp_apr_ipcrtr_open_valid(ipcrtr_open_frame,
	    sizeof(ipcrtr_open_frame), 0));
	assert(!beryllium_adsp_apr_ipcrtr_open_valid(ipcrtr_open_frame,
	    sizeof(ipcrtr_open_frame), 1));
	put32(ipcrtr_open_frame + 4,
	    BERYLLIUM_ADSP_IPCRTR_CHANNEL_NAME_BYTES - 1);
	assert(!beryllium_adsp_apr_ipcrtr_open_valid(ipcrtr_open_frame,
	    sizeof(ipcrtr_open_frame), 2));
	put32(ipcrtr_open_frame + 4,
	    BERYLLIUM_ADSP_IPCRTR_CHANNEL_NAME_BYTES);
	ipcrtr_open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_ipcrtr_open_valid(ipcrtr_open_frame,
	    sizeof(ipcrtr_open_frame), 2));
	ipcrtr_open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_ipcrtr_open_valid(ipcrtr_open_frame,
	    sizeof(ipcrtr_open_frame) + 8, 2));
	put16(glink_ssr_open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	put16(glink_ssr_open_frame + 2, 3);
	put32(glink_ssr_open_frame + 4,
	    BERYLLIUM_ADSP_GLINK_SSR_CHANNEL_NAME_BYTES);
	memcpy(glink_ssr_open_frame + 8, BERYLLIUM_ADSP_GLINK_SSR_CHANNEL_NAME,
	    BERYLLIUM_ADSP_GLINK_SSR_CHANNEL_NAME_BYTES);
	assert(beryllium_adsp_apr_glink_ssr_open_valid(glink_ssr_open_frame,
	    sizeof(glink_ssr_open_frame), 3));
	assert(!beryllium_adsp_apr_glink_ssr_open_valid(glink_ssr_open_frame,
	    sizeof(glink_ssr_open_frame) - 8, 3));
	assert(!beryllium_adsp_apr_glink_ssr_open_valid(glink_ssr_open_frame,
	    sizeof(glink_ssr_open_frame), 0));
	assert(!beryllium_adsp_apr_glink_ssr_open_valid(glink_ssr_open_frame,
	    sizeof(glink_ssr_open_frame), 2));
	put32(glink_ssr_open_frame + 4,
	    BERYLLIUM_ADSP_GLINK_SSR_CHANNEL_NAME_BYTES - 1);
	assert(!beryllium_adsp_apr_glink_ssr_open_valid(glink_ssr_open_frame,
	    sizeof(glink_ssr_open_frame), 3));
	put32(glink_ssr_open_frame + 4,
	    BERYLLIUM_ADSP_GLINK_SSR_CHANNEL_NAME_BYTES);
	glink_ssr_open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_glink_ssr_open_valid(glink_ssr_open_frame,
	    sizeof(glink_ssr_open_frame), 3));
	put16(loopback_ctl_lpass_open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	put16(loopback_ctl_lpass_open_frame + 2, 4);
	put32(loopback_ctl_lpass_open_frame + 4,
	    BERYLLIUM_ADSP_LOOPBACK_CTL_LPASS_CHANNEL_NAME_BYTES);
	memcpy(loopback_ctl_lpass_open_frame + 8,
	    BERYLLIUM_ADSP_LOOPBACK_CTL_LPASS_CHANNEL_NAME,
	    BERYLLIUM_ADSP_LOOPBACK_CTL_LPASS_CHANNEL_NAME_BYTES);
	assert(beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame), 4));
	put16(loopback_ctl_lpass_open_frame, BERYLLIUM_ADSP_GLINK_CMD_INTENT);
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame), 4));
	put16(loopback_ctl_lpass_open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame) - 8, 4));
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame), 0));
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame), 3));
	put32(loopback_ctl_lpass_open_frame + 4,
	    BERYLLIUM_ADSP_LOOPBACK_CTL_LPASS_CHANNEL_NAME_BYTES - 1);
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame), 4));
	put32(loopback_ctl_lpass_open_frame + 4,
	    0x10000u | BERYLLIUM_ADSP_LOOPBACK_CTL_LPASS_CHANNEL_NAME_BYTES);
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame), 4));
	put32(loopback_ctl_lpass_open_frame + 4,
	    BERYLLIUM_ADSP_LOOPBACK_CTL_LPASS_CHANNEL_NAME_BYTES);
	loopback_ctl_lpass_open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame), 4));
	loopback_ctl_lpass_open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_loopback_ctl_lpass_open_valid(
	    loopback_ctl_lpass_open_frame, sizeof(loopback_ctl_lpass_open_frame) + 8, 4));
	put16(apr_apps2_open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	put16(apr_apps2_open_frame + 2, 6);
	put32(apr_apps2_open_frame + 4, BERYLLIUM_ADSP_APR_APPS2_CHANNEL_NAME_BYTES);
	memcpy(apr_apps2_open_frame + 8, BERYLLIUM_ADSP_APR_APPS2_CHANNEL_NAME,
	    BERYLLIUM_ADSP_APR_APPS2_CHANNEL_NAME_BYTES);
	assert(beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 6));
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame) - 8, 6));
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 0));
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 5));
	put16(apr_apps2_open_frame, BERYLLIUM_ADSP_GLINK_CMD_INTENT);
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 6));
	put16(apr_apps2_open_frame, BERYLLIUM_ADSP_GLINK_CMD_OPEN);
	put32(apr_apps2_open_frame + 4,
	    BERYLLIUM_ADSP_APR_APPS2_CHANNEL_NAME_BYTES - 1);
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 6));
	put32(apr_apps2_open_frame + 4, BERYLLIUM_ADSP_APR_APPS2_CHANNEL_NAME_BYTES);
	put32(apr_apps2_open_frame + 4,
	    0x10000u | BERYLLIUM_ADSP_APR_APPS2_CHANNEL_NAME_BYTES);
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 6));
	put32(apr_apps2_open_frame + 4, BERYLLIUM_ADSP_APR_APPS2_CHANNEL_NAME_BYTES);
	apr_apps2_open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 6));
	apr_apps2_open_frame[8] ^= 1;
	apr_apps2_open_frame[17] = 1;
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 6));
	apr_apps2_open_frame[17] = 0;
	apr_apps2_open_frame[23] = 1;
	assert(!beryllium_adsp_apr_apps2_open_valid(apr_apps2_open_frame,
	    sizeof(apr_apps2_open_frame), 6));
	put32(open_frame + 4, BERYLLIUM_ADSP_APR_CHANNEL_NAME_BYTES - 1);
	assert(!beryllium_adsp_apr_open_name_valid(open_frame, frame_bytes, 7));
	put32(open_frame + 4, 0x10000u | BERYLLIUM_ADSP_APR_CHANNEL_NAME_BYTES);
	assert(beryllium_adsp_apr_glink_frame_bytes(open_frame, 8, &frame_bytes) ==
	    BERYLLIUM_ADSP_GLINK_FRAME_INCOMPLETE);
	open_frame[8] ^= 1;
	assert(!beryllium_adsp_apr_open_name_valid(open_frame, frame_bytes, 7));
	open_frame[8] ^= 1;
	put32(open_frame + 4, 0x10000u | BERYLLIUM_ADSP_APR_CHANNEL_NAME_BYTES);
	assert(beryllium_adsp_apr_glink_frame_bytes(open_frame, sizeof(open_frame),
	    &frame_bytes) == BERYLLIUM_ADSP_GLINK_FRAME_COMPLETE);
	assert(beryllium_adsp_apr_open_name_valid(open_frame, frame_bytes, 7));

	put16(intent_frame, BERYLLIUM_ADSP_GLINK_CMD_INTENT);
	put16(intent_frame + 2, 7);
	put32(intent_frame + 4, 2);
	put32(intent_frame + 8, 512);
	put32(intent_frame + 12, 0x101);
	put32(intent_frame + 16, 20);
	put32(intent_frame + 20, 0x102);
	assert(beryllium_adsp_apr_glink_frame_bytes(intent_frame,
	    sizeof(intent_frame), &frame_bytes));
	assert(frame_bytes == sizeof(intent_frame));
	assert(beryllium_adsp_apr_parse_remote_intents(intent_frame, frame_bytes,
	    7, intents, 2, &intent_count));
	assert(intent_count == 2 && intents[0].id == 0x101 && intents[0].size == 512);
	assert(intents[1].id == 0x102 && intents[1].size == 20);
	assert(!beryllium_adsp_apr_parse_remote_intents(intent_frame, frame_bytes,
	    8, intents, 2, &intent_count));
	put32(intent_frame + 20, 0x101);
	assert(!beryllium_adsp_apr_parse_remote_intents(intent_frame, frame_bytes,
	    7, intents, 2, &intent_count));
	put32(intent_frame + 20, 0x102);
	put32(intent_frame + 4, BERYLLIUM_ADSP_APR_MAX_REMOTE_INTENTS + 1);
	assert(beryllium_adsp_apr_glink_frame_bytes(intent_frame,
	    sizeof(intent_frame), &frame_bytes) == BERYLLIUM_ADSP_GLINK_FRAME_MALFORMED);
	put32(intent_frame + 4, 2);
	assert(!beryllium_adsp_apr_parse_remote_intents(intent_frame, sizeof(intent_frame),
	    7, intents, 1, &intent_count));
	assert(beryllium_adsp_apr_glink_frame_bytes(captured_zero_intent_frame,
	    sizeof(captured_zero_intent_frame), &frame_bytes));
	assert(frame_bytes == sizeof(captured_zero_intent_frame));
	assert(beryllium_adsp_apr_parse_remote_intents(captured_zero_intent_frame,
	    frame_bytes, 5, intents, 2, &intent_count));
	assert(intent_count == 1 && intents[0].id == 0 && intents[0].size == 128);
	put16(intent_frame, BERYLLIUM_ADSP_GLINK_CMD_INTENT);
	put16(intent_frame + 2, 5);
	put32(intent_frame + 4, 2);
	put32(intent_frame + 8, 128);
	put32(intent_frame + 12, 0);
	put32(intent_frame + 16, 128);
	put32(intent_frame + 20, 0);
	assert(!beryllium_adsp_apr_parse_remote_intents(intent_frame, sizeof(intent_frame),
	    5, intents, 2, &intent_count));
	assert(beryllium_adsp_apr_glink_frame_bytes(rx_done_w_reuse_frame,
	    sizeof(rx_done_w_reuse_frame) - 1, &frame_bytes) ==
	    BERYLLIUM_ADSP_GLINK_FRAME_INCOMPLETE);
	assert(beryllium_adsp_apr_glink_frame_bytes(rx_done_w_reuse_frame,
	    sizeof(rx_done_w_reuse_frame), &frame_bytes) ==
	    BERYLLIUM_ADSP_GLINK_FRAME_COMPLETE);
	assert(frame_bytes == sizeof(rx_done_w_reuse_frame));
	assert(beryllium_adsp_apr_response_dump_bytes(0) == 0);
	assert(beryllium_adsp_apr_response_dump_bytes(1) == 1);
	assert(beryllium_adsp_apr_response_dump_bytes(64) == 64);
	assert(beryllium_adsp_apr_response_dump_bytes(65) == 64);
	assert(beryllium_adsp_apr_response_dump_bytes(512) == 64);

	put16(data_frame, BERYLLIUM_ADSP_GLINK_CMD_TX_DATA);
	put16(data_frame + 2, 7);
	put32(data_frame + 4, 0x101);
	put32(data_frame + 8, 20);
	put32(data_frame + 12, 0);
	assert(beryllium_adsp_apr_glink_frame_bytes(data_frame,
	    sizeof(data_frame), &frame_bytes));
	assert(frame_bytes == sizeof(data_frame));
	const uint8_t *view = NULL;
	size_t view_bytes = 0;
	assert(beryllium_adsp_apr_tx_data_view(data_frame, frame_bytes, 7,
	    0x101, &view, &view_bytes));
	assert(view == data_frame + 16 && view_bytes == 20);
	assert(!beryllium_adsp_apr_tx_data_view(data_frame, frame_bytes, 8,
	    0x101, &view, &view_bytes));
	put32(data_frame + 12, 1);
	assert(!beryllium_adsp_apr_glink_frame_bytes(data_frame,
	    sizeof(data_frame), &frame_bytes));
	put32(data_frame + 12, 0);
	put32(data_frame + 8, BERYLLIUM_ADSP_APR_MAX_DATA_BYTES + 1);
	assert(beryllium_adsp_apr_glink_frame_bytes(data_frame,
	    sizeof(data_frame), &frame_bytes) == BERYLLIUM_ADSP_GLINK_FRAME_MALFORMED);

	make_apr_header(response, 64, BERYLLIUM_ADSP_APR_RSP_GET_FWK_VERSION);
	put32(response + 20, 1);
	put32(response + 24, 2);
	put32(response + 28, 3);
	put32(response + 32, 4);
	put32(response + 36, 2);
	put32(response + 40, 3);
	put32(response + 44, 0x10001);
	put32(response + 48, 0x22);
	put32(response + 52, 7);
	put32(response + 56, 0x20002);
	put32(response + 60, 0x33);
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_FWK_VERSION);
	assert(parsed.major == 1 && parsed.minor == 2 && parsed.branch == 3);
	assert(parsed.subbranch == 4 && parsed.service_count == 2);
	assert(parsed.services[0].service_id == 3);
	assert(parsed.services[0].api_version == 0x10001);
	assert(parsed.services[0].api_branch_version == 0x22);
	assert(parsed.services[1].service_id == 7);
	assert(parsed.services[1].api_version == 0x20002);
	assert(parsed.services[1].api_branch_version == 0x33);
	memcpy(captured_apr_v1_response, captured_apr_v1_response_prefix,
	    sizeof(captured_apr_v1_response_prefix));
	assert(beryllium_adsp_apr_parse_response(captured_apr_v1_response,
	    sizeof(captured_apr_v1_response), &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_FWK_VERSION);
	assert(parsed.major == 2 && parsed.minor == 8 && parsed.branch == 0);
	assert(parsed.subbranch == 0 && parsed.service_count == 8);
	assert(parsed.services[0].service_id == 3);
	assert(parsed.services[0].api_version == 1);
	assert(parsed.services[0].api_branch_version == 0);

	make_apr_header(response, 40, BERYLLIUM_ADSP_APR_RSP_GET_FWK_VERSION);
	put32(response + 20, 1);
	put32(response + 24, 2);
	put32(response + 28, 3);
	put32(response + 32, 4);
	put32(response + 36, 0);
	assert(beryllium_adsp_apr_parse_response(response, 40, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_FWK_VERSION);
	assert(parsed.service_count == 0);

	memcpy(malformed_apr_response, captured_apr_v1_response,
	    sizeof(captured_apr_v1_response));
	put16(malformed_apr_response, 0x0182); /* unsupported APR version */
	assert(beryllium_adsp_apr_parse_response(malformed_apr_response,
	    sizeof(captured_apr_v1_response), &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	memcpy(malformed_apr_response, captured_apr_v1_response,
	    sizeof(captured_apr_v1_response));
	put16(malformed_apr_response, 0x0281); /* command, not response */
	assert(beryllium_adsp_apr_parse_response(malformed_apr_response,
	    sizeof(captured_apr_v1_response), &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	memcpy(malformed_apr_response, captured_apr_v1_response,
	    sizeof(captured_apr_v1_response));
	put16(malformed_apr_response, 0x0141); /* header is only 16 bytes */
	assert(beryllium_adsp_apr_parse_response(malformed_apr_response,
	    sizeof(captured_apr_v1_response), &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	memcpy(malformed_apr_response, captured_apr_v1_response,
	    sizeof(captured_apr_v1_response));
	put16(malformed_apr_response + 2, 20);
	assert(beryllium_adsp_apr_parse_response(malformed_apr_response, 20,
	    &parsed) == BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	memcpy(malformed_apr_response, captured_apr_v1_response,
	    sizeof(captured_apr_v1_response));
	put32(malformed_apr_response + 48, 9); /* count exceeds exact body */
	assert(beryllium_adsp_apr_parse_response(malformed_apr_response,
	    sizeof(captured_apr_v1_response), &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	memcpy(malformed_apr_response, captured_apr_v1_response,
	    sizeof(captured_apr_v1_response));
	put16(malformed_apr_response, 0x0581); /* reserved header-field bits */
	assert(beryllium_adsp_apr_parse_response(malformed_apr_response,
	    sizeof(captured_apr_v1_response), &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[4] = BERYLLIUM_ADSP_APR_Q6CORE_SERVICE + 1;
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[4] = BERYLLIUM_ADSP_APR_Q6CORE_SERVICE;
	response[8] = BERYLLIUM_ADSP_APR_Q6CORE_SERVICE + 1;
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[8] = BERYLLIUM_ADSP_APR_Q6CORE_SERVICE;
	response[5] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP + 1;
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[5] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP;
	response[9] = BERYLLIUM_ADSP_APR_DOMAIN_APPS + 1;
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[9] = BERYLLIUM_ADSP_APR_DOMAIN_APPS;
	put16(response, 0x0152);
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	put16(response, BERYLLIUM_ADSP_APR_RSP_HDR_FIELD);

	put16(response + 2, 63);
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	put16(response + 2, 64);
	put32(response + 36, 3);
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	make_apr_header(response, 52, BERYLLIUM_ADSP_APR_RSP_GET_FWK_VERSION);
	put32(response + 36, 2);
	assert(beryllium_adsp_apr_parse_response(response, 52, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	make_apr_header(response, sizeof(response),
	    BERYLLIUM_ADSP_APR_RSP_GET_FWK_VERSION);
	put32(response + 36,
	    BERYLLIUM_ADSP_APR_MAX_DATA_BYTES / 12u + 1u);
	assert(beryllium_adsp_apr_parse_response(response, sizeof(response), &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);

	make_apr_header(response, 28, BERYLLIUM_ADSP_APR_BASIC_RSP_RESULT);
	put32(response + 20, BERYLLIUM_ADSP_APR_CMD_GET_FWK_VERSION);
	put32(response + 24, 0x80000001);
	assert(beryllium_adsp_apr_parse_response(response, 28, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_BASIC_RESULT);
	assert(parsed.status == 0x80000001);
	make_apr_header(response, 40, BERYLLIUM_ADSP_APR_BASIC_RSP_RESULT);
	put16(response, 0x0181); /* v1 header with 12 optional bytes */
	put32(response + 32, BERYLLIUM_ADSP_APR_CMD_GET_FWK_VERSION);
	put32(response + 36, 0x80000002);
	assert(beryllium_adsp_apr_parse_response(response, 40, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_BASIC_RESULT);
	assert(parsed.status == 0x80000002);
	make_apr_header(response, 28, BERYLLIUM_ADSP_APR_BASIC_RSP_RESULT);
	put32(response + 20, BERYLLIUM_ADSP_APR_CMD_GET_FWK_VERSION);
	put32(response + 24, 0x80000001);
	response[5] = 5;
	assert(beryllium_adsp_apr_parse_response(response, 28, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[5] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP;
	put32(response + 20, 0x00129205);
	assert(beryllium_adsp_apr_parse_response(response, 28, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);

	assert(!beryllium_adsp_apr_encode_afe_get_i2s_config(afe_request,
	    sizeof(afe_request) - 1, &afe_request_bytes));
	assert(beryllium_adsp_apr_encode_afe_get_i2s_config(afe_request,
	    sizeof(afe_request), &afe_request_bytes));
	assert(afe_request_bytes == sizeof(afe_request));
	assert(afe_request[0] == 0x50 && afe_request[1] == 0x02);
	assert(afe_request[2] == sizeof(afe_request) && afe_request[3] == 0);
	assert(afe_request[4] == BERYLLIUM_ADSP_APR_AFE_SERVICE &&
	    afe_request[5] == BERYLLIUM_ADSP_APR_DOMAIN_APPS);
	assert(afe_request[8] == BERYLLIUM_ADSP_APR_AFE_SERVICE &&
	    afe_request[9] == BERYLLIUM_ADSP_APR_DOMAIN_ADSP);
	assert(afe_request[12] == BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN &&
	    afe_request[13] == 0 && afe_request[14] == 0 && afe_request[15] == 0);
	assert(afe_request[16] == 0xf0 && afe_request[17] == 0x00 &&
	    afe_request[18] == 0x01 && afe_request[19] == 0x00);
	assert(beryllium_adsp_apr_get16(afe_request + 20) ==
	    BERYLLIUM_ADSP_AFE_PORT_QUAT_MI2S_RX);
	assert(beryllium_adsp_apr_get16(afe_request + 22) ==
	    BERYLLIUM_ADSP_AFE_GET_PARAM_PAYLOAD_BYTES);
	assert(beryllium_adsp_apr_get32(afe_request + 24) == 0 &&
	    beryllium_adsp_apr_get32(afe_request + 28) == 0 &&
	    beryllium_adsp_apr_get32(afe_request + 32) == 0);
	assert(beryllium_adsp_apr_get32(afe_request + 36) ==
	    BERYLLIUM_ADSP_AFE_MODULE_AUDIO_DEV_INTERFACE);
	assert(beryllium_adsp_apr_get32(afe_request + 40) ==
	    BERYLLIUM_ADSP_AFE_PARAM_I2S_CONFIG);
	assert(beryllium_adsp_apr_get32(afe_request + 44) ==
	    BERYLLIUM_ADSP_AFE_MODULE_AUDIO_DEV_INTERFACE);
	assert(beryllium_adsp_apr_get32(afe_request + 48) ==
	    BERYLLIUM_ADSP_AFE_PARAM_I2S_CONFIG);
	assert(beryllium_adsp_apr_get16(afe_request + 52) ==
	    BERYLLIUM_ADSP_AFE_I2S_CONFIG_BYTES);
	assert(beryllium_adsp_apr_get16(afe_request + 54) == 0);

	afe_service_table.service_count = 1;
	afe_service_table.services[0].service_id = BERYLLIUM_ADSP_APR_AFE_SERVICE;
	afe_service_table.services[0].api_version = 2;
	afe_service_table.services[0].api_branch_version = 0;
	assert(beryllium_adsp_apr_afe_api_v2_available(&afe_service_table));
	afe_service_table.service_count = 2;
	afe_service_table.services[1] = afe_service_table.services[0];
	assert(!beryllium_adsp_apr_afe_api_v2_available(&afe_service_table));
	afe_service_table.service_count = 1;
	afe_service_table.services[0].api_version = 1;
	assert(!beryllium_adsp_apr_afe_api_v2_available(&afe_service_table));
	afe_service_table.services[0].api_version = 2;
	afe_service_table.services[0].api_branch_version = 1;
	assert(!beryllium_adsp_apr_afe_api_v2_available(&afe_service_table));
	afe_service_table.services[0].api_branch_version = 0;
	afe_service_table.service_count = BERYLLIUM_ADSP_APR_MAX_DATA_BYTES / 12u + 1u;
	assert(!beryllium_adsp_apr_afe_api_v2_available(&afe_service_table));

	make_afe_apr_header(afe_get_response, sizeof(afe_get_response),
	    BERYLLIUM_ADSP_AFE_RSP_GET_PARAM_V2);
	put32(afe_get_response + 32, 0);
	put32(afe_get_response + 36, BERYLLIUM_ADSP_AFE_MODULE_AUDIO_DEV_INTERFACE);
	put32(afe_get_response + 40, BERYLLIUM_ADSP_AFE_PARAM_I2S_CONFIG);
	put16(afe_get_response + 44, BERYLLIUM_ADSP_AFE_I2S_CONFIG_BYTES);
	put32(afe_get_response + 48, 1); /* I2S config minor version */
	put16(afe_get_response + 52, 16);
	put16(afe_get_response + 54, 1); /* SD0 */
	put16(afe_get_response + 56, 1); /* stereo */
	put16(afe_get_response + 58, 0); /* external WS */
	put32(afe_get_response + 60, 48000);
	put16(afe_get_response + 64, 0); /* linear PCM */
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(afe_get_response,
	    sizeof(afe_get_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_GET_PARAM);
	assert(parsed_afe.status == 0 && parsed_afe.i2s.minor_version == 1);
	assert(parsed_afe.i2s.bit_width == 16 && parsed_afe.i2s.channel_mode == 1);
	assert(parsed_afe.i2s.mono_stereo == 1 && parsed_afe.i2s.ws_source == 0);
	assert(parsed_afe.i2s.sample_rate == 48000 && parsed_afe.i2s.data_format == 0);

	memcpy(malformed_afe_response, afe_get_response, sizeof(afe_get_response));
	malformed_afe_response[4]++;
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(malformed_afe_response,
	    sizeof(malformed_afe_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	memcpy(malformed_afe_response, afe_get_response, sizeof(afe_get_response));
	put32(malformed_afe_response + 12, BERYLLIUM_ADSP_AFE_QUAT_MI2S_TOKEN + 1);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(malformed_afe_response,
	    sizeof(malformed_afe_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	memcpy(malformed_afe_response, afe_get_response, sizeof(afe_get_response));
	put32(malformed_afe_response + 40, BERYLLIUM_ADSP_AFE_PARAM_I2S_CONFIG + 1);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(malformed_afe_response,
	    sizeof(malformed_afe_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	memcpy(malformed_afe_response, afe_get_response, sizeof(afe_get_response));
	put16(malformed_afe_response + 44, BERYLLIUM_ADSP_AFE_I2S_CONFIG_BYTES - 4);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(malformed_afe_response,
	    sizeof(malformed_afe_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(afe_get_response,
	    sizeof(afe_get_response) - 1, &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);
	memcpy(malformed_afe_response, afe_get_response, sizeof(afe_get_response));
	put16(malformed_afe_response + 66, 1); /* reserved I2S field */
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(malformed_afe_response,
	    sizeof(malformed_afe_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);

	make_afe_apr_header(afe_status_response, sizeof(afe_status_response),
	    BERYLLIUM_ADSP_AFE_RSP_GET_PARAM_V2);
	put32(afe_status_response + 32, 0x80000001);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(afe_status_response,
	    sizeof(afe_status_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_STATUS);
	assert(parsed_afe.status == 0x80000001);

	make_afe_apr_header(afe_basic_response, sizeof(afe_basic_response),
	    BERYLLIUM_ADSP_APR_BASIC_RSP_RESULT);
	put32(afe_basic_response + 32, BERYLLIUM_ADSP_AFE_CMD_GET_PARAM_V2);
	put32(afe_basic_response + 36, 0x80000002);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(afe_basic_response,
	    sizeof(afe_basic_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_BASIC_RESULT);
	assert(parsed_afe.status == 0x80000002);
	put32(afe_basic_response + 32, BERYLLIUM_ADSP_AFE_CMD_GET_PARAM_V2 + 1);
	assert(beryllium_adsp_apr_parse_afe_get_i2s_config(afe_basic_response,
	    sizeof(afe_basic_response), &parsed_afe) ==
	    BERYLLIUM_ADSP_AFE_RESPONSE_MALFORMED);

	test_linux_asm_adm_packets();
	puts("APR mode-4 helper checks passed");
	return 0;
}
