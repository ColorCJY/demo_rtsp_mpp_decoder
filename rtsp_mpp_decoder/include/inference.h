#ifndef INFERENCE_H
#define INFERENCE_H
#include <mutex>
#include <thread>
#include <functional>

#include "rknn_api.h"
#include "rknn_type.h"
#include "dma_alloc.h"
#include "postprocess.h"

// 编码回调函数类型
using EncodeCallback = std::function<void(std::shared_ptr<dma_data_t>)>;

class Inference {
public:
    ~Inference() {
        release();
    }
    int initialize(const char *model_path, bool info);
    
    // 获取源帧缓冲区的引用（用于外部直接复制）
    dma_data_t& get_src_frame() { return src_frame; }
    
    // 触发推理（数据已经复制到 src_frame）
    void trigger_inference(uint64_t frame_seq);
    
    void release();
    bool is_busy() { return m_is_busy.load(); }
    
    void set_encode_callback(EncodeCallback callback) {
        m_encode_callback = callback;
    }

    Inference() = default;
    Inference(const Inference&) = delete;
    Inference& operator=(const Inference&) = delete;

private:
    void inference_model();

private:
    bool m_is_init = false;
    bool m_is_running = false;
    std::atomic<bool> m_is_busy{false};  // 这个原子变量保证了访问顺序

    std::thread m_inferenceThread;

    dma_data_t resize_img;
    dma_data_t input_img;
    dma_data_t rgba_data;
    dma_data_t src_frame; // 源帧缓冲区（对外暴露）

    rknn_app_context_t app_ctx;

    uint64_t m_current_frame_seq = 0;
    bool m_has_frame = false;
    
    std::mutex m_signal_mutex; // 只保护信号变量
    std::condition_variable m_frame_cv;
    
    // 编码回调
    EncodeCallback m_encode_callback;
};

#endif
