/*
 * Copyright (c) 2008-2011, Michael Kohn
 * Copyright (c) 2013, Robin Hahling
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the author nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is the file containing gwavi library functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gwavi.h"
#include "fileio.h"

/**
 * TODO: document this function
 * @param filename
 * @param width
 * @param height
 * @param fourcc
 * @param fps
 * @param audio
 *
 * @return Structure containing required information in order to create the AVI
 * file. If errors occured, NULL is returned.
 */
struct gwavi_t *
gwavi_open(char *filename, int width, int height, char *fourcc,
	   int fps, struct gwavi_audio_t *audio)
{
	struct gwavi_t *gwavi;
	FILE *out;

	if ((out = fopen(filename, "wb+")) == NULL) {
		perror("gwavi_open: failed to open file for writing");
		return NULL;
	}

	if ((gwavi = malloc(sizeof(struct gwavi_t))) == NULL) {
		(void)fprintf(stderr, "gwavi_open: could not allocate memoryi "
			      "for gwavi structure\n");
		return NULL;
	}
	memset(gwavi, 0, sizeof(struct gwavi_t));

	gwavi->out = out;

	/* set avi header */
	gwavi->avi_header.time_delay= 1000000 / fps;
	gwavi->avi_header.data_rate = width * height * 3;
	gwavi->avi_header.flags = 0x10;

	if (audio)
		gwavi->avi_header.data_streams = 2;
	else
		gwavi->avi_header.data_streams = 1;

	/* this field gets updated when calling gwavi_close() */
	gwavi->avi_header.number_of_frames = 0;
	gwavi->avi_header.width = width;
	gwavi->avi_header.height = height;
	gwavi->avi_header.buffer_size = (width * height * 3);

	/* set stream header */
	(void)strcpy(gwavi->stream_header_v.data_type, "vids");
	(void)memcpy(gwavi->stream_header_v.codec, fourcc, 4);
	gwavi->stream_header_v.time_scale = 1;
	gwavi->stream_header_v.data_rate = fps;
	gwavi->stream_header_v.buffer_size = (width * height * 3);
	gwavi->stream_header_v.data_length = 0;

	/* set stream format */
	gwavi->stream_format_v.header_size = 40;
	gwavi->stream_format_v.width = width;
	gwavi->stream_format_v.height = height;
	gwavi->stream_format_v.num_planes = 1;
	gwavi->stream_format_v.bits_per_pixel = 24;
	gwavi->stream_format_v.compression_type =
		((int)fourcc[3] << 24) +
		((int)fourcc[2] << 16) +
		((int)fourcc[1] << 8) +
		((int)fourcc[0]);
	gwavi->stream_format_v.image_size = width * height * 3;
	gwavi->stream_format_v.colors_used = 0;
	gwavi->stream_format_v.colors_important = 0;

	gwavi->stream_format_v.palette = 0;
	gwavi->stream_format_v.palette_count = 0;

	if (audio) {
		/* set stream header */
		memcpy(gwavi->stream_header_a.data_type, "auds", 4);
		gwavi->stream_header_a.codec[0] = 1;
		gwavi->stream_header_a.codec[1] = 0;
		gwavi->stream_header_a.codec[2] = 0;
		gwavi->stream_header_a.codec[3] = 0;
		gwavi->stream_header_a.time_scale = 1;
		gwavi->stream_header_a.data_rate = audio->samples_per_second;
		gwavi->stream_header_a.buffer_size =
			audio->channels * (audio->bits / 8) * audio->samples_per_second;
		gwavi->stream_header_a.quality = -1;
		gwavi->stream_header_a.sample_size =
			(audio->bits / 8) * audio->channels;

		/* set stream format */
		gwavi->stream_format_a.format_type = 1;
		gwavi->stream_format_a.channels = audio->channels;
		gwavi->stream_format_a.sample_rate = audio->samples_per_second;
		gwavi->stream_format_a.bytes_per_second =
			audio->channels * (audio->bits / 8) * audio->samples_per_second;
		gwavi->stream_format_a.block_align =
			audio->channels * (audio->bits / 8);
		gwavi->stream_format_a.bits_per_sample = audio->bits;
		gwavi->stream_format_a.size = 0;
	}

	if (write_chars_bin(out, "RIFF", 4) == -1)
		goto write_chars_bin_failed;
	if (write_int(out, 0) == -1) {
		(void)fprintf(stderr, "gwavi_info: write_int() failed\n");
		return NULL;
	}
	if (write_chars_bin(out, "AVI ", 4) == -1)
		goto write_chars_bin_failed;

	if (write_avi_header_chunk(gwavi) == -1) {
		(void)fprintf(stderr, "gwavi_info: write_avi_header_chunk "
			      "failed\n");
		return NULL;
	}

	if (write_chars_bin(out, "LIST", 4) == -1)
		goto write_chars_bin_failed;
	if ((gwavi->marker = ftell(out)) == -1) {
		perror("gwavi_info (ftell)");
		return NULL;
	}
	if (write_int(out, 0) == -1) {
		(void)fprintf(stderr, "gwavi_info: write_int() failed\n");
		return NULL;
	}
	if (write_chars_bin(out, "movi", 4) == -1)
		goto write_chars_bin_failed;

	gwavi->offsets_len = 1024;
	if ((gwavi->offsets = malloc((size_t)gwavi->offsets_len * sizeof(int)))
			== NULL) {
		(void)fprintf(stderr, "gwavi_info: could not allocate memory "
			      "for gwavi offsets table\n");
		return NULL;
	}

	gwavi->offsets_ptr = 0;

	return gwavi;

write_chars_bin_failed:
	(void)fprintf(stderr, "gwavi_open: write_chars_bin() failed\n");
	return NULL;
}

/**
 * TODO: document this function
 * @param gwavi
 * @param buffer
 * @param len
 *
 * @return 0 on success, -1 on error.
 */
int
gwavi_add_frame(struct gwavi_t *gwavi, unsigned char *buffer, size_t len)
{
	size_t maxi_pad;  /* if your frame is raggin, give it some paddin' */
	size_t t;

	gwavi->offset_count++;
	gwavi->stream_header_v.data_length++;

	maxi_pad = len % 4;
	if (maxi_pad > 0)
		maxi_pad = 4 - maxi_pad;

	if (gwavi->offset_count >= gwavi->offsets_len) {
		gwavi->offsets_len += 1024;
		gwavi->offsets = realloc(gwavi->offsets,
					(size_t)gwavi->offsets_len * sizeof(int));
	}

	gwavi->offsets[gwavi->offsets_ptr++] = (int)(len + maxi_pad);

	if (write_chars_bin(gwavi->out, "00dc", 4) == -1) {
		(void)fprintf(stderr, "gwavi_add_frame: write_chars_bin() "
			      "failed\n");
		return -1;
	}
	if (write_int(gwavi->out, (int)(len + maxi_pad)) == -1) {
		(void)fprintf(stderr, "gwavi_add_frame: write_int() failed\n");
		return -1;
	}

	if ((t = fwrite(buffer, 1, len, gwavi->out)) != len) {
		(void)fprintf(stderr, "gwavi_add_frame: fwrite() failed\n");
		return -1;
	}

	for (t = 0; t < maxi_pad; t++)
		if (fputc(0, gwavi->out) == EOF) {
			(void)fprintf(stderr, "gwavi_add_frame: fputc() failed\n");
			return -1;
		}

	return 0;
}

/**
 * TODO: document this function
 * @param gwavi
 * @param buffer
 * @param len
 *
 * @return 0 on success, -1 on error.
 */
int
gwavi_add_audio(struct gwavi_t *gwavi, unsigned char *buffer, size_t len)
{
	size_t maxi_pad;  /* in case audio bleeds over the 4 byte boundary  */
	size_t t;

	gwavi->offset_count++;

	maxi_pad = len % 4;
	if (maxi_pad > 0)
		maxi_pad = 4 - maxi_pad;

	if (gwavi->offset_count >= gwavi->offsets_len) {
		gwavi->offsets_len += 1024;
		gwavi->offsets = realloc(gwavi->offsets,
					(size_t)gwavi->offsets_len*sizeof(int));
	}

	gwavi->offsets[gwavi->offsets_ptr++] = (int)((len + maxi_pad) | 0x80000000);

	if (write_chars_bin(gwavi->out,"01wb",4) == -1) {
		(void)fprintf(stderr, "gwavi_add_audio: write_chars_bin() "
			      "failed\n");
		return -1;
	}
	if (write_int(gwavi->out,(int)(len + maxi_pad)) == -1) {
		(void)fprintf(stderr, "gwavi_add_audio: write_int() failed\n");
		return -1;
	}

	if ((t = fwrite(buffer, 1, len, gwavi->out)) != len ) {
		(void)fprintf(stderr, "gwavi_add_audio: fwrite() failed\n");
		return -1;
	}

	for (t = 0; t < maxi_pad; t++)
		if (fputc(0,gwavi->out) == EOF) {
			(void)fprintf(stderr, "gwavi_add_audio: fputc() failed\n");
			return -1;
		}

	gwavi->stream_header_a.data_length += (int)(len + maxi_pad);

	return 0;
}

/**
 * TODO: document this function
 * @param gwavi
 *
 * @return 0 on success, -1 on error.
 */
int
gwavi_close(struct gwavi_t *gwavi)
{
	long t;

	if ((t = ftell(gwavi->out)) == -1)
		goto ftell_failed;
	if (fseek(gwavi->out, gwavi->marker, SEEK_SET) == -1)
		goto fseek_failed;
	if (write_int(gwavi->out, (t-gwavi->marker - 4)) == -1) {
		(void)fprintf(stderr, "gwavi_close: write_int() failed\n");
		return -1;
	}
	if (fseek(gwavi->out,t,SEEK_SET) == -1)
		goto fseek_failed;

	if (write_index(gwavi->out, gwavi->offset_count, gwavi->offsets) == -1) {
		(void)fprintf(stderr, "gwavi_close: write_index() failed\n");
		return -1;
	}

	free(gwavi->offsets);

	/* reset some avi header fields */
	gwavi->avi_header.number_of_frames = gwavi->stream_header_v.data_length;

	if ((t = ftell(gwavi->out)) == -1)
		goto ftell_failed;
	if (fseek(gwavi->out, 12, SEEK_SET) == -1)
		goto fseek_failed;
	if (write_avi_header_chunk(gwavi) == -1) {
		(void)fprintf(stderr, "gwavi_close: write_avi_header_chunk() "
			      "failed\n");
		return -1;
	}
	if (fseek(gwavi->out, t, SEEK_SET) == -1)
		goto fseek_failed;

	if ((t = ftell(gwavi->out)) == -1)
		goto ftell_failed;
	if (fseek(gwavi->out, 4, SEEK_SET) == -1)
		goto fseek_failed;
	if (write_int(gwavi->out, (t - 8)) == -1) {
		(void)fprintf(stderr, "gwavi_close: write_int() failed\n");
		return -1;
	}
	if (fseek(gwavi->out, t, SEEK_SET) == -1)
		goto fseek_failed;

	if (gwavi->stream_format_v.palette != 0)
		free(gwavi->stream_format_v.palette);

	fclose(gwavi->out);
	free(gwavi);

	return 0;

ftell_failed:
	perror("gwavi_close: (ftell)");
	return -1;

fseek_failed:
	perror("gwavi_close (fseek)");
	return -1;
}

/**
 * TODO: document this function
 * @param gwavi
 * @param fps
 */
void
gwavi_set_framerate(struct gwavi_t *gwavi, int fps)
{
	gwavi->stream_header_v.data_rate = fps;
	gwavi->avi_header.time_delay = (int)(1000000.0 / fps);
}

/**
 * TODO: document this function
 * @param gwavi
 * @param fourcc
 */
void
gwavi_set_codec(struct gwavi_t *gwavi, const char *fourcc)
{
	memcpy(gwavi->stream_header_v.codec, fourcc, 4);
	gwavi->stream_format_v.compression_type =
		((int)fourcc[3] << 24) +
		((int)fourcc[2] << 16) +
		((int)fourcc[1] << 8) +
		((int)fourcc[0]);
}

/**
 * TODO: document this function
 * @param gwavi
 * @param width
 * @param height
 */
void
gwavi_set_size(struct gwavi_t *gwavi, int width, int height)
{
	int size = (width * height * 3);

	gwavi->avi_header.data_rate = size;
	gwavi->avi_header.width = width;
	gwavi->avi_header.height = height;
	gwavi->avi_header.buffer_size = size;
	gwavi->stream_header_v.buffer_size = size;
	gwavi->stream_format_v.width = width;
	gwavi->stream_format_v.height = height;
	gwavi->stream_format_v.image_size = size;
}
