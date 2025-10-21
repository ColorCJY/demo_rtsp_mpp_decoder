#include <signal.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>

#include <iostream>

#include "mk_mediakit.h"
#include "rtsp_server.h"
#include "inference.h"
#include "INIReader.h"

static sem_t exit_sem;

std::unique_ptr<RtspServer> server_raw;
std::unique_ptr<RtspServer> server_detect;

static void sigint_handler(int sig) {
    sem_post(&exit_sem);
}

std::vector<std::string> GetAllLocalIPs() {
    std::vector<std::string> ips;
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) == -1) {
        return ips;
    }
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            void* addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, addr, ip, INET_ADDRSTRLEN);
            
            std::string ip_str(ip);
            // 排除回环地址
            if (ip_str != "127.0.0.1") {
                ips.push_back(ip_str);
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return ips;
}

Config loadConfig(const std::string& filename) {
    Config config;
    INIReader reader(filename);
    
    if (reader.ParseError() < 0) {
        std::cerr << "无法加载配置文件: " << filename << std::endl;
        exit(1);
    }
    
    config.pullStream = reader.Get("pull_stream", "url", "");
    
    // 推流服务器配置
    config.pushServer.type = reader.Get("push_server", "type", "rtsp");
    config.pushServer.port = reader.GetInteger("push_server", "port", 8554);
    
    // 原始流配置
    config.originStream.vhost = reader.Get("origin_stream", "vhost", "__defaultVhost__");
    config.originStream.app = reader.Get("origin_stream", "app", "app");
    config.originStream.stream = reader.Get("origin_stream", "stream", "origin");
    
    // 检测流配置
    config.detectStream.vhost = reader.Get("detect_stream", "vhost", "__defaultVhost__");
    config.detectStream.app = reader.Get("detect_stream", "app", "app");
    config.detectStream.stream = reader.Get("detect_stream", "stream", "detect");

    config.model_path = reader.Get("model_path", "path", "./model/yolov8n.rknn");
    
    config.inference_threads = reader.GetInteger("inference", "threads", 2);


    std::cout << "Pull Stream URL: " << config.pullStream << std::endl;
    std::cout << "Push Server Port: " << config.pushServer.port << std::endl;
    auto local_ips = GetAllLocalIPs();

    std::cout << "Origin Stream (localhost): rtsp://localhost:" << config.pushServer.port
            << "/" << config.originStream.app << "/" << config.originStream.stream << std::endl;

    if (!local_ips.empty()) {
        for (size_t i = 0; i < local_ips.size(); ++i) {
            std::cout << "Origin Stream (network " << (i+1) << "): rtsp://" << local_ips[i] 
                    << ":" << config.pushServer.port
                    << "/" << config.originStream.app << "/" << config.originStream.stream << std::endl;
        }
    }

    std::cout << "Detect Stream (localhost): rtsp://localhost:" << config.pushServer.port
            << "/" << config.detectStream.app << "/" << config.detectStream.stream << std::endl;

    if (!local_ips.empty()) {
        for (size_t i = 0; i < local_ips.size(); ++i) {
            std::cout << "Detect Stream (network " << (i+1) << "): rtsp://" << local_ips[i] 
                    << ":" << config.pushServer.port
                    << "/" << config.detectStream.app << "/" << config.detectStream.stream << std::endl;
        }
    }

    std::cout << "Inference Threads: " << config.inference_threads << std::endl;
    
    return config;
}


static MppCodingType ConvertCodecType(int mk_codec) {
    if (mk_codec == MKCodecH264) return MPP_VIDEO_CodingAVC;
    if (mk_codec == MKCodecH265) return MPP_VIDEO_CodingHEVC;
    if (mk_codec == MKCodecVP8) return MPP_VIDEO_CodingVP8;
    if (mk_codec == MKCodecVP9) return MPP_VIDEO_CodingVP9;
    if (mk_codec == MKCodecAV1) return MPP_VIDEO_CodingAV1;
    if (mk_codec == MKCodecJPEG) return MPP_VIDEO_CodingMJPEG;
    return MPP_VIDEO_CodingUnused;
}

void deal_coded_frame(uint8_t* data, uint32_t size, uint64_t pts, void* userdata) {
    if(server_detect != nullptr) {
        mk_media pMedia = server_detect->getZlmMediaHandle();
        if(pMedia != nullptr) {
            mk_media_input_h264(pMedia, data, size, pts, pts);
        }
    }
}

// 编码回调函数（从推理线程调用）
void inference_encode_callback(FrameContext* ctx, std::shared_ptr<code_frame_t> frame) {
    std::unique_lock<std::mutex> lock(ctx->pending_mutex);
    // 将处理完的帧加入待编码队列
    ctx->pending_frames[frame->frame_seq] = frame;
    lock.unlock();
    ctx->pending_cv.notify_one();
}

// 编码线程函数 - 按序编码
void encode_thread_func(FrameContext* ctx) {
    while(ctx->encoding_running) {
        std::shared_ptr<code_frame_t> frame_to_encode;
        
        {
            std::unique_lock<std::mutex> lock(ctx->pending_mutex);
            ctx->pending_cv.wait(lock, [ctx]() {
                return !ctx->pending_frames.empty() || !ctx->encoding_running;
            });
            
            if(!ctx->encoding_running && ctx->pending_frames.empty()) {
                break;
            }
            
            uint64_t expected_seq = ctx->encode_seq.load();
            auto it = ctx->pending_frames.find(expected_seq);
            
            if(it != ctx->pending_frames.end()) {
                frame_to_encode = it->second;
                ctx->pending_frames.erase(it);
                ctx->encode_seq++;
            } else {
                continue;
            }
        }
        
        // 渲染FPS
        if(frame_to_encode && frame_to_encode->frame) {
            YUVLabelRenderer::getInstance().drawFPS(frame_to_encode->frame, 
                                  frame_to_encode->width, 
                                  frame_to_encode->height, 
                                  10, 10);
        }
        
        // 编码帧
        if(frame_to_encode && frame_to_encode->frame && ctx->encoder != nullptr) {
            ctx->encoder->WriteData(frame_to_encode->frame, frame_to_encode->size);
        }
    }
}

void mpp_decoder_frame_callback(void *userdata, int width_stride, int height_stride, 
                                int width, int height, int format, int fd, void *data)
{
    FrameContext *ctx = (FrameContext *)userdata;

    // 初始化编码器和编码线程
    if (ctx->encoder == nullptr) {
        RKEncodeVideo *rk_encoder = new RKEncodeVideo();
        InputInfo info;
        info.width = width;
        info.height = height;
        info.fps = ctx->fps;
        info.format = eFormatType::YUV420SP;
        int ret = rk_encoder->Initencoder(info, 0, deal_coded_frame, ctx);
        if(ret != 0) {
            delete rk_encoder;
            return;
        }
        if(server_detect != nullptr) {
            mk_media pMedia = server_detect->getZlmMediaHandle();
            if(pMedia != nullptr) {
                mk_media_input_h264(pMedia, info.data, info.size, 0, 0);
            }
        }
        ctx->encoder = rk_encoder;
        
        // 启动编码线程
        ctx->encoding_running = true;
        ctx->encode_thread = std::thread(encode_thread_func, ctx);
    }
    
    // 分配序列号
    uint64_t frame_seq = ctx->frame_seq_counter++;
    
    // 负载均衡：尝试找到空闲的推理线程
    int thread_count = ctx->inferences.size();
    bool pushed = false;
    
    // 轮询所有推理线程
    for(int i = 0; i < thread_count; i++) {
        int idx = (ctx->next_inference_idx.fetch_add(1) % thread_count);
        
        // 检查是否空闲（原子操作，无需锁）
        if(!ctx->inferences[idx]->is_busy()) {
            Inference* inf = ctx->inferences[idx].get();
            
            // 直接访问 src_frame（因为此时 is_busy == false，推理线程不会访问）
            dma_data_t& src_frame = inf->get_src_frame();
            
            int yuv_size = width_stride * height_stride * 3 / 2;
            if(src_frame.get_size() == 0 || 
               src_frame.width_stride != width_stride || 
               src_frame.height_stride != height_stride) {
                
                // printf("Thread %d: Reallocating source frame buffer: %dx%d\n", idx, width_stride, height_stride);
                
                src_frame.release();
                int ret = src_frame.make_dma(width_stride, height_stride, RK_FORMAT_YCbCr_420_SP, yuv_size);
                if(ret < 0) {
                    printf("src_frame make_dma error\n");
                    continue;
                }
            }
            
            // 更新帧信息
            src_frame.width = width;
            src_frame.height = height;
            src_frame.width_stride = width_stride;
            src_frame.height_stride = height_stride;
            src_frame.format = RK_FORMAT_YCbCr_420_SP;
            src_frame.frame_seq = frame_seq;
            
            memcpy(src_frame.buf, data, yuv_size);
            
            // 触发推理（设置 is_busy = true 后，推理线程才会访问 src_frame）
            inf->trigger_inference(frame_seq);
            pushed = true;
            break;
        }
    }
    
    // 如果所有线程都忙，直接编码（保持顺序）
    if(!pushed) {
        printf("All inference threads busy, encoding frame %lu directly\n", frame_seq);
        int yuv_size = width_stride * height_stride * 3 / 2;
    
        auto direct_frame = std::make_shared<code_frame_t>();
        direct_frame->frame_seq = frame_seq;
        direct_frame->size = yuv_size;
        direct_frame->frame = (u_char*)malloc(yuv_size);
        direct_frame->width = width_stride;
        direct_frame->height = height_stride;
            
        memcpy(direct_frame->frame, data, yuv_size);
        std::unique_lock<std::mutex> lock(ctx->pending_mutex);
        ctx->pending_frames[frame_seq] = direct_frame;
        lock.unlock();
        ctx->pending_cv.notify_one();
    }
}



void API_CALL on_track_frame_out(void *user_data, mk_frame frame) {
    FrameContext *ctx = (FrameContext *)user_data;
    if(ctx == nullptr) {
        return;
    }
    int code = mk_frame_codec_id(frame);    
    
    const char *data = mk_frame_get_data(frame);
    size_t size = mk_frame_get_data_size(frame);

    uint64_t pts = mk_frame_get_pts(frame);
    uint64_t dts = mk_frame_get_dts(frame);

    // 推送原始编码流到 server_raw
    if(server_raw != nullptr) {
        mk_media pMedia = server_raw->getZlmMediaHandle();
            if(pMedia != nullptr) {
                if(code == MKCodecH264) {
                    mk_media_input_h264(pMedia, (void*)data, size, dts, pts);
            } else if(code == MKCodecH265) {
                    mk_media_input_h265(pMedia, (void*)data, size, dts, pts);
            }
        }
    }
    
    if (ctx->decoder == NULL) {
        MppDecoder *decoder = new MppDecoder();
        MppCodingType video_type = ConvertCodecType(code);
        if (video_type == MPP_VIDEO_CodingUnused) {
            fprintf(stderr, "Unsupported codec type: %d\n", code);
            delete decoder;
            return;
        }
        
        if (decoder->Init(video_type, ctx->fps, ctx) != 1) {
            fprintf(stderr, "Failed to initialize decoder\n");
            delete decoder;
            return;
        }
        decoder->SetCallback(mpp_decoder_frame_callback);
        ctx->decoder = decoder;
    }
    ctx->decoder->Decode((uint8_t *)data, size, 0);
}

void API_CALL on_mk_play_event_func(void *user_data, int err_code, const char *err_msg, 
                                    mk_track tracks[], int track_count)
{
    if (err_code == 0) {
        printf("play success!\n");
        for (int i = 0; i < track_count; ++i) {
            if (mk_track_is_video(tracks[i])) {
                log_info("got video track: %s", mk_track_codec_name(tracks[i]));
                mk_track_add_delegate(tracks[i], on_track_frame_out, user_data);
                auto ptr = (FrameContext*)user_data;
                if(ptr != nullptr) {
                    ptr->fps = mk_track_video_fps(tracks[i]);
                }
            }
        }
    } else {
        printf("play failed: %d %s\n", err_code, err_msg);
    }
}

void API_CALL on_mk_shutdown_func(void *user_data, int err_code, const char *err_msg, 
                                  mk_track tracks[], int track_count) {
    printf("play interrupted: %d %s\n", err_code, err_msg);
}

int process_video_rtsp(FrameContext *ctx, const char *url) {
    mk_config config;
    memset(&config, 0, sizeof(mk_config));
    config.log_mask = LOG_CONSOLE;
    mk_env_init(&config);
    
    mk_player player = mk_player_create();
    mk_player_set_option(player, "rtp_type", "tcp");
    mk_player_set_on_result(player, on_mk_play_event_func, ctx);
    mk_player_set_on_shutdown(player, on_mk_shutdown_func, ctx);
    mk_player_play(player, url);

    sem_init(&exit_sem, 0, 0);
    signal(SIGINT, sigint_handler);
    printf("Press Ctrl+C to exit\n");
    sem_wait(&exit_sem);
    sem_destroy(&exit_sem);

    if (player) {
        mk_player_release(player);
    }
    return 0;
}

int main(int argc, char **argv) {
    Config config = loadConfig("config.ini");

    int ret = init_post_process();
    if(ret < 0) {
        return ret;
    }
    
    YUVLabelRenderer::Config font_config;
    font_config.font_size = 18;
    font_config.font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    font_config.font_color_r = 255;
    font_config.font_color_g = 255;
    font_config.font_color_b = 0;
    font_config.bg_color_r = 0;
    font_config.bg_color_g = 0;
    font_config.bg_color_b = 0;
    font_config.bg_alpha = 255;
    extern char* labels[OBJ_CLASS_NUM];
    ret = YUVLabelRenderer::getInstance().initialize(labels, font_config);
    if(!ret) {
        return -2;
    }

    FrameContext frame_ctx;
    for(int i = 0; i < config.inference_threads; i++) {
        auto inference = std::make_unique<Inference>();
        ret = inference->initialize(config.model_path.c_str(), i == 0 ? true : false);
        if(ret != 0) {
            printf("initialize inference %d error ret=%d\n", i, ret);
            return 1;
        }
        
        inference->set_encode_callback([&frame_ctx](std::shared_ptr<code_frame_t> frame) {
            inference_encode_callback(&frame_ctx, frame);
        });
        
        frame_ctx.inferences.push_back(std::move(inference));
    }

    PushServer m_server_config = config.pushServer;

    m_server_config.stream_conifg = config.detectStream;
    server_detect = std::make_unique<RtspServer>(m_server_config);
    server_detect->initZlmMedia();

    m_server_config.stream_conifg = config.originStream;
    server_raw = std::make_unique<RtspServer>(m_server_config);
    server_raw->initZlmMedia();

    process_video_rtsp(&frame_ctx, config.pullStream.c_str());

    deinit_post_process();

    frame_ctx.encoding_running = false;
    frame_ctx.pending_cv.notify_all();
    
    if(frame_ctx.encode_thread.joinable()) {
        frame_ctx.encode_thread.join();
    }

    if (frame_ctx.decoder != nullptr) {
        delete frame_ctx.decoder;
        frame_ctx.decoder = nullptr;
    }
    if (frame_ctx.encoder != nullptr) {
        delete frame_ctx.encoder;
        frame_ctx.encoder = nullptr;
    }

    frame_ctx.inferences.clear();
    server_detect->stopServer();
    server_raw->stopServer();

    return 0;
}
