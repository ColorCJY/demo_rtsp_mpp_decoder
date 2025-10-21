#ifndef RKNN_TYPE_H
#define RKNN_TYPE_H

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <map>
#include <atomic>
#include <vector>
#include <memory>
#include <condition_variable>

#include "rga.h"
#include "im2d.h"
#include "rknn_api.h"
#include "RgaUtils.h"
#include "dma_alloc.h"
#include "im2d_type.h"

#include "mpp_decoder.h"
#include "encode_video.h"

#define CMA_HEAP_PATH "/dev/dma_heap/cma"

class Inference;
class YUVLabelRenderer;

typedef struct
{
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs = nullptr;
    rknn_tensor_attr *output_attrs = nullptr;
    int model_channel;
    int model_width;
    int model_height;
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

struct dma_data_t {
    int width;
    int height;
    int width_stride;
    int height_stride;
    int format;
    int size;
    int fd = 0;
    u_char *buf = nullptr;
    uint64_t frame_seq = 0;

    dma_data_t() = default;
    dma_data_t(const dma_data_t&) = delete;
    dma_data_t& operator=(const dma_data_t&) = delete;

    ~dma_data_t() {
        if(buf != nullptr) {
            dma_buf_free(get_size(), &fd, buf);
        }
    }

    int make_dma(int width, int height, int format, int size) {
        this->width = width;
        this->height = height;
        this->format = format;
        this->size = size;
        int ret = dma_buf_alloc(CMA_HEAP_PATH, size, &fd, (void **)&buf);
        if (ret < 0 || fd <= 0 || buf == NULL) {
            printf("dma_buf_alloc failed: ret=%d, fd=%d, buf=%p\n", ret, fd, buf);
            return -1;
        }
        return 0;
    }

    void release() {
        if(buf != nullptr) {
            dma_buf_free(get_size(), &fd, buf);
            buf = nullptr;
            fd = 0;
            size = 0;
        }
    }

    int get_size() {
        if(buf != nullptr) {
            return size;
        }
        else {
            return 0;
        }
    }
};

struct code_frame_t {
    u_char* frame = nullptr;  // 手动管理的指针
    int size = 0;
    int width;
    int height;
    uint64_t frame_seq = 0;   // 序列号
    
    // 析构函数 - 自动释放 malloc 的内存
    ~code_frame_t() {
        if(frame) {
            free(frame);
            frame = nullptr;
        }
    }
    
    // 禁止拷贝（避免浅拷贝导致double free）
    code_frame_t(const code_frame_t&) = delete;
    code_frame_t& operator=(const code_frame_t&) = delete;
    
    // 允许移动
    code_frame_t(code_frame_t&& other) noexcept 
        : frame(other.frame), size(other.size), frame_seq(other.frame_seq) {
        other.frame = nullptr;
        other.size = 0;
    }
    
    code_frame_t& operator=(code_frame_t&& other) noexcept {
        if(this != &other) {
            // 释放当前资源
            if(frame) {
                free(frame);
            }
            // 转移所有权
            frame = other.frame;
            size = other.size;
            frame_seq = other.frame_seq;
            
            other.frame = nullptr;
            other.size = 0;
        }
        return *this;
    }
    
    // 默认构造函数
    code_frame_t() = default;
};


struct StreamConfig {
    std::string vhost;
    std::string app;
    std::string stream;
};

struct PushServer {
    std::string type = "rtsp";
    int port = 8554;
    StreamConfig stream_conifg;  // 原始流
};

struct Config {
    PushServer pushServer;
    StreamConfig originStream;  // 原始流
    StreamConfig detectStream;  // 检测流
    std::string pullStream;
    std::string model_path;
    int inference_threads = 2; // 推理线程数量
};

struct FrameContext {
    int fps = 30; // 视频流的fps

    std::vector<std::unique_ptr<Inference>> inferences; // 多个推理实例
    RKEncodeVideo *encoder = nullptr;
    MppDecoder *decoder = nullptr;
    
    std::atomic<uint64_t> frame_seq_counter{0}; // 帧序列号计数器
    std::atomic<uint64_t> encode_seq{0}; // 当前应该编码的序列号
    
    std::map<uint64_t, std::shared_ptr<code_frame_t>> pending_frames; // 等待编码的帧
    std::mutex pending_mutex;
    std::condition_variable pending_cv;
    
    std::thread encode_thread;
    std::atomic<bool> encoding_running{false};
    
    std::atomic<int> next_inference_idx{0}; // 轮询索引
};

#endif