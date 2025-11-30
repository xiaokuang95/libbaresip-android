#ifndef CLIPREC_H264_H
#define CLIPREC_H264_H
// #include "cliprec.h"
#include "common.h"

// void* h264_alloc(void* param, int bytes);
// void h264_free(void* param, void *packet);
// int h264_packet(void* param, const void *packet, int bytes, uint32_t timestamp, int flags);
// int h264_writer_frame(uint8_t *buf, size_t len,void *arg);
void h264_register(void);
void h264_unregister(void);

#endif
