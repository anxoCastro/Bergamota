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
	uint8_t data_frame[40] = { 0 };
	uint8_t response[BERYLLIUM_ADSP_APR_MAX_DATA_BYTES];
	struct beryllium_adsp_apr_response parsed = { 0 };
	struct beryllium_adsp_glink_intent intents[2];
	size_t frame_bytes = 0;
	size_t intent_count = 0;

	assert(beryllium_adsp_apr_encode_get_fwk_version(request));
	assert(beryllium_adsp_glink_mode_valid(1));
	assert(beryllium_adsp_glink_mode_valid(2));
	assert(beryllium_adsp_glink_mode_valid(3));
	assert(beryllium_adsp_glink_mode_valid(4));
	assert(!beryllium_adsp_glink_mode_valid(0));
	assert(!beryllium_adsp_glink_mode_valid(5));
	assert(beryllium_adsp_glink_mode_requests_pas(2));
	assert(beryllium_adsp_glink_mode_requests_pas(3));
	assert(beryllium_adsp_glink_mode_requests_pas(4));
	assert(beryllium_adsp_glink_mode_requests_glink(3));
	assert(beryllium_adsp_glink_mode_requests_glink(4));
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
	response[0] ^= 1;
	assert(beryllium_adsp_apr_parse_response(response, 64, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[0] ^= 1;

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
	response[5] = 5;
	assert(beryllium_adsp_apr_parse_response(response, 28, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);
	response[5] = BERYLLIUM_ADSP_APR_DOMAIN_ADSP;
	put32(response + 20, 0x00129205);
	assert(beryllium_adsp_apr_parse_response(response, 28, &parsed) ==
	    BERYLLIUM_ADSP_APR_RESPONSE_MALFORMED);

	puts("APR mode-4 helper checks passed");
	return 0;
}
