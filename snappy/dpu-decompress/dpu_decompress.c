/**
 * DPU-compatible port of snappy decompression. Heavily borrowed from
 * https://github.com/andikleen/snappy-c
 */

#include <assert.h>
#include <string.h>  // memcpy
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <mram.h>
#include <defs.h>
#include "dpu_decompress.h"

/*******************
 * Memory helpers  *
 *******************/
static uint8_t READ_BYTE(struct in_buffer_context *_i)
{
	uint8_t ret = *_i->ptr;
	_i->ptr = seqread_get(_i->ptr, sizeof(uint8_t), &_i->sr);
	_i->curr++;
	return ret;
}

static uint16_t make_offset_1_byte(uint8_t tag, struct in_buffer_context *input)
{
	if (input->curr >= input->length)
		return 0;
	return (uint16_t)(READ_BYTE(input)) | (uint16_t)(GET_OFFSET_1_BYTE(tag) << 8);
}

static uint16_t make_offset_2_byte(uint8_t tag, struct in_buffer_context *input)
{
	UNUSED(tag);

	if ((input->curr + sizeof(uint16_t)) > input->length)
		return 0;
	   
	return (READ_BYTE(input) | (READ_BYTE(input) << 8));
}

static uint32_t make_offset_4_byte(uint8_t tag, struct in_buffer_context *input)
{
	if ((input->curr + sizeof(uint32_t)) > input->length)
		return 0;
	
	return (READ_BYTE(input) |
				(READ_BYTE(input) << 8) |
				(READ_BYTE(input) << 16) |
				(READ_BYTE(input) << 24));
}

/***************************
 * Reader & writer helpers *
 ***************************/
uint32_t read_long_literal_size(struct in_buffer_context *input, uint32_t len)
{
	if ((input->curr + len) >= input->length)
		return 0;
	
	uint32_t size = 0;
	for (uint32_t i = 0; i < len; i++) {
		size |= (READ_BYTE(input) << (i << 3));
	}
	
	return size;
}

/**
 * Copy and append data from the input buffer to the output buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param len: length of data to copy over
 */
static inline void writer_append_dpu(struct in_buffer_context *input, struct out_buffer_context *output, uint16_t len)
{
	uint32_t curr_index = output->curr - output->append_window;
	while (len)
	{
		// if we are past the window, write the current window back to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;
		}
		output->append_ptr[curr_index++] = READ_BYTE(input);
		output->curr++;
		len--;
	}
}

/**
 * Copy and append previous data to the output buffer. The data may
 * already be existing in the append buffer, or may need to be copied
 * from MRAM first.
 *
 * @param output: holds output buffer information
 * @param copy_length: length of data to copy over
 * @param offset: where to copy from, offset from the current output pointer
 */
void write_copy_dpu(struct out_buffer_context *output, uint32_t copy_length, uint32_t offset)
{
	// We only copy previous data, not future data
	if (offset > output->curr)
	{
		printf("Invalid offset detected: 0x%x\n", offset);
		return;
	}

	uint32_t read_index = output->curr - offset;
	dbg_printf("Copying %u bytes from offset=0x%x to 0x%x\n", copy_length, read_index, output->curr);

	uint8_t read_buf[OUT_BUFFER_LENGTH];
	uint8_t *read_ptr = read_buf;
	uint32_t curr_index = output->curr - output->append_window;
	while (copy_length)
	{
		uint32_t to_copy = MIN(OUT_BUFFER_LENGTH - curr_index, copy_length);

		// First check if we can use data already in the append window, note we do not need to check the
		// upper bound since the first check in this function handles that case
		if (read_index >= output->append_window) {
			read_ptr = &output->append_ptr[read_index % OUT_BUFFER_LENGTH];
		}
		else {
			read_ptr = read_buf;
			mram_read(&output->buffer[read_index], read_buf, ALIGN(to_copy, 8));
		}	

		memcpy(&output->append_ptr[curr_index], read_ptr, to_copy);
		curr_index += to_copy;
		copy_length -= to_copy;
		read_index += to_copy;

		// If we are past the append window, write the current window back to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;
		}
	}
}

/**************************
 * Snappy decompressor.   *
 **************************/

/*********************
 * Public functions  *
 *********************/

snappy_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output)
{
	dbg_printf("curr: %u length: %u\n", input->curr, input->length);
	dbg_printf("output length: %u\n", output->length);
	while (input->curr < input->length) 
	{
		uint32_t length;
		uint32_t offset;
		uint8_t tag;
		tag = READ_BYTE(input);
		dbg_printf("Got tag byte 0x%x at index 0x%x\n", tag, input->curr - 1);
		// There are two types of elements in a Snappy stream: Literals and
		// copies (backreferences). Each element starts with a tag byte,
		// and the lower two bits of this tag byte signal what type of element
		// will follow.
		switch (GET_ELEMENT_TYPE(tag))
		{
		case EL_TYPE_LITERAL:
			// For literals up to and including 60 bytes in length, the upper
			// six bits of the tag byte contain (len-1). The literal follows
			// immediately thereafter in the bytestream.
			length = GET_LENGTH_2_BYTE(tag) + 1;
			if (length > 60)
				length = read_long_literal_size(input, length - 60) + 1;

			writer_append_dpu(input, output, length);
			break;

		// Copies are references back into previous decompressed data, telling
		// the decompressor to reuse data it has previously decoded.
		// They encode two values: The _offset_, saying how many bytes back
		// from the current position to read, and the _length_, how many bytes
		// to copy.
		case EL_TYPE_COPY_1:
			length = GET_LENGTH_1_BYTE(tag) + 4;
			offset = make_offset_1_byte(tag, input);
			write_copy_dpu(output, length, offset);
			break;

		case EL_TYPE_COPY_2:
			length = GET_LENGTH_2_BYTE(tag) + 1;
			offset = make_offset_2_byte(tag, input);
			write_copy_dpu(output, length, offset);
			break;

		case EL_TYPE_COPY_4:
			length = GET_LENGTH_2_BYTE(tag) + 1;
			offset = make_offset_4_byte(tag, input);
			write_copy_dpu(output, length, offset);
			break;
		}
	}

	// Write out the final buffer
	if (output->append_window < output->length) {
		uint32_t len_final = output->length % OUT_BUFFER_LENGTH;
		if (len_final == 0)
			len_final = OUT_BUFFER_LENGTH;

		dbg_printf("Writing window at: 0x%x (%u bytes)\n", output->append_window, len_final);
		mram_write(output->append_ptr, &output->buffer[output->append_window], len_final);
	}
	return SNAPPY_OK;
}

