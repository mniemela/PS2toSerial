/*
 * ringbuf.h
 *
 * Created: 27.9.2014 2:01:46
 *  Author: mikael
 */ 

#include <inttypes.h>

#ifndef RINGBUF_H
#define RINGBUF_H

struct ringBuffer {
	volatile uint8_t ring_head;
	volatile uint8_t ring_size;
	volatile uint8_t ring_tail;
	volatile uint8_t ring_elements;
	char *ring_data;
};

static inline void init_buf(struct ringBuffer *buf, uint8_t size, char * array_) {
	buf->ring_data = array_;
	buf->ring_size = size;
	buf->ring_head = 0;
	buf->ring_tail = 0;
	buf->ring_elements = 0;
}

static int add(struct ringBuffer *buf, char c) {
	uint8_t next_head = (buf->ring_head + 1) & (buf->ring_size - 1);
	if (next_head != buf->ring_tail) {
		buf->ring_data[buf->ring_head] = c;
		buf->ring_head = next_head;
		buf->ring_elements++;
		return 0;
	}
	else {
		return -1;
	}
}

static char remove(struct ringBuffer *buf) {
	if (buf->ring_head != buf->ring_tail) {
		uint8_t c = buf->ring_data[buf->ring_tail];
		buf->ring_tail = (buf->ring_tail + 1) & (buf->ring_size - 1);
		buf->ring_elements--;
		return c;
	}
	else {
		return 0x00;
	}
}

static inline uint8_t elements(struct ringBuffer *buf) {
	return buf->ring_elements;
}

static inline void empty(struct ringBuffer *buf) {
	buf->ring_elements = 0;
	buf->ring_head = 0;
	buf->ring_tail = 0;
}

static inline int addarr(struct ringBuffer *buf, char *arr, uint8_t count) {
	for (int i = 0; i < count;i++) {
		if (add(buf, arr[i]) == -1) return -1;
	}
	return 0;
}

#endif /* RINGBUF_H */