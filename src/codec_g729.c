/*
 * codec_g729.c - G.729 transcoder module for Asterisk (built on bcg729)
 *
 * Copyright (C) 2026, Ioan Mario
 *
 * This program is free software, distributed under the terms of the GNU
 * General Public License, version 2 or (at your option) any later version.
 * See the LICENSE file at the top of the source tree. (or-later is chosen so
 * the combined work with the GPLv3+ bcg729 library is license-compatible.)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ---------------------------------------------------------------------------
 * What this module is (and is not)
 * ---------------------------------------------------------------------------
 * This is the *codec* (transcoder) module. It registers two translators with
 * the Asterisk translation core:
 *
 *      g729  -> slin   (decode)
 *      slin  -> g729   (encode)
 *
 * That is ALL that is needed to interoperate with every PCM-based codec.
 * Asterisk chains slin <-> ulaw/alaw/gsm automatically, so a path such as
 *
 *      (g729@8000)->(slin@8000)->(ulaw@8000)
 *
 * is built by the core without us writing a single g729<->ulaw converter.
 *
 * The actual G.729 math (CELP encode/decode, PCM <-> G.729) lives in the
 * Belledonne bcg729 library, which this module links against. bcg729 knows
 * nothing about Asterisk; this module is the glue between the two.
 *
 * Note: format_g729.so (the file/passthrough format) is unrelated - it only
 * describes how G.729 frames look on the wire/in a file. It does NOT
 * transcode. This module is what makes "core show translation paths g729"
 * report a real path instead of "No Translation Path".
 */

#define AST_MODULE "codec_g729"
#ifndef AST_MODULE_SELF_SYM
#define AST_MODULE_SELF_SYM __internal_codec_g729_self
#endif

#include "asterisk.h"

#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/slin.h"

#include <bcg729/encoder.h>
#include <bcg729/decoder.h>

/* One G.729 frame == 10 ms == 80 PCM samples == 10 bytes of G.729 payload. */
#define G729_SAMPLES    80
#define G729_FRAME_LEN  10
/* G.729 Annex B SID (comfort-noise) frames are 2 bytes. */
#define G729_SID_LEN    2

/* Generous staging buffer (1 second of 8 kHz audio). */
#define BUFFER_SAMPLES  8000

struct g729_coder_pvt {
	bcg729EncoderChannelContextStruct *encoder;
	bcg729DecoderChannelContextStruct *decoder;
	/* Staging buffer for the slin -> g729 direction. */
	int16_t buf[BUFFER_SAMPLES];
};

/*
 * One G.729 frame of payload, used only by the translation core to measure
 * the cost of the path at registration time. The content is irrelevant - any
 * valid 10-byte frame decodes without error - so silence-ish zeros are fine.
 */
static uint8_t g729_ex[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                               */
/* ----------------------------------------------------------------------- */

static int g729tolin_new(struct ast_trans_pvt *pvt)
{
	struct g729_coder_pvt *p = pvt->pvt;

	p->decoder = initBcg729DecoderChannel();
	return p->decoder ? 0 : -1;
}

static int lintog729_new(struct ast_trans_pvt *pvt)
{
	struct g729_coder_pvt *p = pvt->pvt;

	/* VAD/DTX disabled: always emit full 10-byte frames. */
	p->encoder = initBcg729EncoderChannel(0);
	return p->encoder ? 0 : -1;
}

static void g729_destroy(struct ast_trans_pvt *pvt)
{
	struct g729_coder_pvt *p = pvt->pvt;

	if (p->encoder) {
		closeBcg729EncoderChannel(p->encoder);
		p->encoder = NULL;
	}
	if (p->decoder) {
		closeBcg729DecoderChannel(p->decoder);
		p->decoder = NULL;
	}
}

/* ----------------------------------------------------------------------- */
/* g729 -> slin (decode)                                                   */
/* ----------------------------------------------------------------------- */

static struct ast_frame *g729tolin_sample(void)
{
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.datalen = sizeof(g729_ex),
		.samples = G729_SAMPLES,
		.mallocd = 0,
		.offset = 0,
		.src = __PRETTY_FUNCTION__,
		.data.ptr = g729_ex,
	};
	return &f;
}

static int g729tolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct g729_coder_pvt *p = pvt->pvt;
	uint8_t *src = f->data.ptr;
	int16_t *dst = pvt->outbuf.i16;
	int x = 0;

	/*
	 * A packet may carry several stacked frames, and with Annex B a 2-byte
	 * SID frame may appear. Walk the buffer, decoding 10-byte speech frames
	 * and 2-byte SID frames, producing 80 PCM samples per frame.
	 */
	while (x < f->datalen) {
		int len;
		uint8_t sid;

		if (f->datalen - x >= G729_FRAME_LEN) {
			len = G729_FRAME_LEN;
			sid = 0;
		} else {
			len = G729_SID_LEN;
			sid = 1;
		}

		bcg729Decoder(p->decoder, src + x, len, 0 /* erasure */, sid,
			0 /* rfc3389 */, dst + pvt->samples);

		pvt->samples += G729_SAMPLES;
		pvt->datalen += G729_SAMPLES * 2;
		x += len;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */
/* slin -> g729 (encode)                                                   */
/* ----------------------------------------------------------------------- */

static int lintog729_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct g729_coder_pvt *p = pvt->pvt;

	/* Accumulate raw PCM; the actual encode happens in frameout(). */
	memcpy(p->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

static struct ast_frame *lintog729_frameout(struct ast_trans_pvt *pvt)
{
	struct g729_coder_pvt *p = pvt->pvt;
	int datalen = 0;
	int samples = 0;

	/* Encode as many whole 80-sample frames as we have buffered. */
	while (pvt->samples >= G729_SAMPLES) {
		uint8_t outlen = 0;

		bcg729Encoder(p->encoder, p->buf + samples,
			(uint8_t *) pvt->outbuf.uc + datalen, &outlen);

		datalen += outlen;
		samples += G729_SAMPLES;
		pvt->samples -= G729_SAMPLES;
	}

	/* Keep any leftover (< 80) samples for the next call. */
	if (pvt->samples) {
		memmove(p->buf, p->buf + samples, pvt->samples * 2);
	}

	if (!datalen) {
		return NULL;
	}

	return ast_trans_frameout(pvt, datalen, samples);
}

/* ----------------------------------------------------------------------- */
/* Translator registration                                                 */
/* ----------------------------------------------------------------------- */

static struct ast_translator g729tolin = {
	.name = "g729tolin",
	.src_codec = {
		.name = "g729",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin",
	.newpvt = g729tolin_new,
	.framein = g729tolin_framein,
	.destroy = g729_destroy,
	.sample = g729tolin_sample,
	.desc_size = sizeof(struct g729_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lintog729 = {
	.name = "lintog729",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "g729",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "g729",
	.newpvt = lintog729_new,
	.framein = lintog729_framein,
	.frameout = lintog729_frameout,
	.destroy = g729_destroy,
	.sample = slin8_sample,
	.desc_size = sizeof(struct g729_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = (BUFFER_SAMPLES * G729_FRAME_LEN) / G729_SAMPLES,
};

/* ----------------------------------------------------------------------- */
/* Module entry points                                                     */
/* ----------------------------------------------------------------------- */

static int unload_module(void)
{
	int res = 0;

	res |= ast_unregister_translator(&lintog729);
	res |= ast_unregister_translator(&g729tolin);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_register_translator(&g729tolin);
	res |= ast_register_translator(&lintog729);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY,
	"G.729 Coder/Decoder (bcg729)");
