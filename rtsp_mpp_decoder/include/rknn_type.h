#ifndef RKNN_TYPE_H
#define RKNN_TYPE_H

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <vector>

#include "im2d.h"
#include "im2d_type.h"
#include "rga.h"
#include "RgaUtils.h"

#include "rknn_api.h"

#include "mpp_decoder.h"
#include "encode_video.h"

#define CMA_HEAP_PATH "/dev/dma_heap/cma"

typedef struct
{
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs = nullptr;
    rknn_tensor_attr *output_attrs = nullptr;
    int model_channel;
    int model_width;
    int model_height;
    MppDecoder *decoder = nullptr;
    RKEncodeVideo *encoder= nullptr;

    int resize_dma_fd;
    char *resize_buf = nullptr;
    int dst_dma_fd;
    char *dst_buf = nullptr;
    rga_buffer_handle_t resize_handle;
    rga_buffer_handle_t dst_handle;
    bool is_quant;
} rknn_app_context_t;

typedef struct
{
    int width;
    int height;
    int width_stride;
    int height_stride;
    int format;
    char *virt_addr;
    int fd;
} image_frame_t;

/**
 * @brief Image pixel format
 * 
 */
typedef enum {
    IMAGE_FORMAT_GRAY8,
    IMAGE_FORMAT_RGB888,
    IMAGE_FORMAT_RGBA8888,
    IMAGE_FORMAT_YUV420SP_NV21,
    IMAGE_FORMAT_YUV420SP_NV12,
} image_format_t;

/**
 * @brief Image buffer
 * 
 */
typedef struct {
    int width;
    int height;
    int width_stride;
    int height_stride;
    image_format_t format;
    unsigned char* virt_addr;
    int size;
    int fd;
} image_buffer_t;

/**
 * @brief Image rectangle
 * 
 */
typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} image_rect_t;

typedef struct {
    int x_pad;
    int y_pad;
    float scale;
} letterbox_t;

/**
 * @brief Image obb rectangle
 * 
 */
typedef struct {
    int x;
    int y;
    int w;
    int h;
    float angle;
} image_obb_box_t;

typedef struct _BOX_RECT {
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

#endif