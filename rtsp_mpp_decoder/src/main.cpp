#include "mk_mediakit.h"
#include "rtsp_server.h"
#include "yolov8.h"
#include "INIReader.h"

#include <iostream>

struct dma_data {
    int width;
    int height;
    int format = RK_FORMAT_RGBA_8888;
    int _fd = 0;
    rga_buffer_handle_t handle;
    char *buf = nullptr;
};

struct Config {
    PushServer pushServer; // 推流服务器配置
    std::string pullStream; // 源服务器地址
};

dma_data rgba_data;
std::unique_ptr<RtspServer> server;

Config loadConfig(const std::string& filename) {
    Config config;
    INIReader reader(filename);
    
    if (reader.ParseError() < 0) {
        std::cerr << "无法加载配置文件: " << filename << std::endl;
        // 可以设置默认值或抛出异常
        exit(1);
    }
    
    // 读取pull_stream部分
    config.pullStream = reader.Get("pull_stream", "url", "");
    
    // 读取push_server部分
    config.pushServer.type = reader.Get("push_server", "type", "rtsp");
    config.pushServer.port = reader.GetInteger("push_server", "port", 8554);
    config.pushServer.vhost = reader.Get("push_server", "vhost", "__defaultVhost__");
    config.pushServer.app = reader.Get("push_server", "app", "live");
    config.pushServer.stream = reader.Get("push_server", "stream", "test");

    std::cout << "Pull Stream URL: " << config.pullStream << std::endl;
    std::cout << "Push Server Type: " << config.pushServer.type << std::endl;
    std::cout << "Push Server Port: " << config.pushServer.port << std::endl;
    std::cout << "Push Server Vhost: " << config.pushServer.vhost << std::endl;
    std::cout << "Push Server App: " << config.pushServer.app << std::endl;
    std::cout << "Push Server Stream: " << config.pushServer.stream << std::endl;
    
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
    rknn_app_context_t *ctx = (rknn_app_context_t *)userdata;
    if(server != nullptr) {
        mk_media pMedia = server->getZlmMediaHandle();
        if(pMedia != nullptr) {
            mk_media_input_h264(pMedia, data, size, pts, pts);
        }
    }
}

void mpp_decoder_frame_callback(void *userdata, int width_stride, int height_stride, int width, int height, int format, int fd, void *data)
{

    rknn_app_context_t *ctx = (rknn_app_context_t *)userdata;

    int ret = 0;
    int buf_size = width * height * get_bpp_from_format(rgba_data.format);;
    rgba_data.width = width;
    rgba_data.height = height;
    static int src_frame_fd = -1;
    static void *src_frame_buf = nullptr;
    if(rgba_data.buf == nullptr) {
        ret = dma_buf_alloc(CMA_HEAP_PATH, buf_size, &rgba_data._fd, (void **)&rgba_data.buf);
        if (ret < 0) {
            printf("alloc dma32_heap buffer failed!\n");
            return;
        }
    }

    memset(rgba_data.buf, 0x00, buf_size);

    rga_buffer_t origin;
    rga_buffer_t src;
    rga_buffer_t rgba;

    memset(&rgba, 0, sizeof(rgba));
    rgba = wrapbuffer_fd(rgba_data._fd, width, height, rgba_data.format);

    if (ctx->encoder == NULL)
    {
        RKEncodeVideo *rk_encoder = new RKEncodeVideo();
        InputInfo info;
        info.width = width;
        info.height = height;
        info.fps = 25;
        info.format = eFormatType::YUV420SP;
        int ret = rk_encoder->Initencoder(info, 0, deal_coded_frame, ctx);
        if(ret != 0) {
            delete rk_encoder;
            return;
        }
        if(server != nullptr) {
            mk_media pMedia = server->getZlmMediaHandle();
            if(pMedia != nullptr) {
                mk_media_input_h264(pMedia, info.data, info.size, 0, 0);
            }
        }
        ctx->encoder = rk_encoder;
    }

    image_frame_t img;
    img.width = width;
    img.height = height;
    img.width_stride = width_stride;
    img.height_stride = height_stride;
    img.fd = fd;
    img.virt_addr = (char *)data;
    img.format = RK_FORMAT_YCbCr_420_SP;
    object_detect_result_list detect_result;
    memset(&detect_result, 0, sizeof(object_detect_result_list));

    ret = inference_model(ctx, &img, &detect_result);
    if (ret != 0)
    {
        printf("inference model fail\n");
        return;
    }

    if (src_frame_fd < 0) {
        int yuv_size = width_stride * height_stride * 3 / 2;
        ret = dma_buf_alloc(CMA_HEAP_PATH, yuv_size, &src_frame_fd, &src_frame_buf);
        if (ret < 0) {
            printf("alloc mpp frame buffer failed!\n");
            return;
        }
    }

    // Copy To another buffer avoid to modify mpp decoder buffer
    origin = wrapbuffer_fd(fd, width, height, RK_FORMAT_YCbCr_420_SP, width_stride, height_stride);
    src = wrapbuffer_fd(src_frame_fd, width, height, RK_FORMAT_YCbCr_420_SP, width_stride, height_stride);
    imcopy(origin, src);

    // Draw objects
    for (int i = 0; i < detect_result.count; i++)
    {
        object_detect_result *det_result = &(detect_result.results[i]);
        // printf("%s @ (%d %d %d %d) %f\n", coco_cls_to_name(det_result->cls_id), det_result->box.left, det_result->box.top,
        //     det_result->box.right, det_result->box.bottom, det_result->prop);
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;

        im_rect rect = {
            std::max(0, x1),
            std::max(0, y1),
            std::min(x2, width - 1) - std::max(0, x1) + 1,
            std::min(y2, height - 1) - std::max(0, y1) + 1
        };
        
        ret = imrectangle(rgba, rect, 0x00FF0000, 4);
        if (ret != IM_STATUS_SUCCESS) {
            printf("imrectangle failed: %s\n", imStrError((IM_STATUS)ret));
        }
    }

    imcomposite(rgba, src, src);

    int yuv_size = width_stride * height_stride * 3 / 2;
    ctx->encoder->WriteData((u_char*)src_frame_buf, yuv_size);
}

void API_CALL on_track_frame_out(void *user_data, mk_frame frame) {
    rknn_app_context_t *ctx = (rknn_app_context_t *)user_data;
    // printf("on_track_frame_out ctx=%p\n", ctx);
    int code = mk_frame_codec_id(frame);
    
    const char *data = mk_frame_get_data(frame);
    size_t size = mk_frame_get_data_size(frame);
    if (ctx->decoder == NULL) {
        MppDecoder *decoder = new MppDecoder();
    
        MppCodingType video_type = ConvertCodecType(code);
        if (video_type == MPP_VIDEO_CodingUnused) {
            fprintf(stderr, "Unsupported codec type: %d\n", code);
            delete decoder;
            return;
        }
        
        if (decoder->Init(video_type, 30, ctx) != 1) {
            fprintf(stderr, "Failed to initialize decoder\n");
            delete decoder;
            return;
        }
        decoder->SetCallback(mpp_decoder_frame_callback);
        ctx->decoder = decoder;
    }
    ctx->decoder->Decode((uint8_t *)data, size, 0);
}

void API_CALL on_mk_play_event_func(void *user_data, int err_code, const char *err_msg, mk_track tracks[],
                                    int track_count)
{
    rknn_app_context_t *ctx = (rknn_app_context_t *)user_data;
    if (err_code == 0) {
        // success
        printf("play success!");
        int i;
        for (i = 0; i < track_count; ++i) {
            if (mk_track_is_video(tracks[i])) {
                log_info("got video track: %s", mk_track_codec_name(tracks[i]));
                // 监听track数据回调
                mk_track_add_delegate(tracks[i], on_track_frame_out, user_data);
            }   
        }
    }
    else {
        printf("play failed: %d %s", err_code, err_msg);
    }
}

void API_CALL on_mk_shutdown_func(void *user_data, int err_code, const char *err_msg, mk_track tracks[], int track_count) {
    printf("play interrupted: %d %s", err_code, err_msg);
}

int process_video_rtsp(rknn_app_context_t *ctx, const char *url) {
    mk_config config;
    memset(&config, 0, sizeof(mk_config));
    config.log_mask = LOG_CONSOLE;
    mk_env_init(&config);
    mk_player player = mk_player_create();
    mk_player_set_on_result(player, on_mk_play_event_func, ctx);
    mk_player_set_on_shutdown(player, on_mk_shutdown_func, ctx);
    mk_player_play(player, url);

    printf("enter any key to exit\n");
    getchar();

    if (player) {
        mk_player_release(player);
    }
    return 0;
}

int main(int argc, char **argv) {
    
    rknn_app_context_t app_ctx;
    Config config = loadConfig("config.ini");
    const char *model_path = "./model/yolov8n.rknn";
    int ret = initialize(model_path, &app_ctx);
    if(ret != 0) {
        printf("initialize inference error ret=%d\n", ret);
        return 1;
    }

    init_post_process();

    server = std::make_unique<RtspServer>(config.pushServer);
    server->initServer();
    server->initZlmMedia();

    process_video_rtsp(&app_ctx, config.pullStream.c_str());

    if (app_ctx.decoder != nullptr) {
        delete (app_ctx.decoder);
        app_ctx.decoder = nullptr;
    }
    if (app_ctx.encoder != nullptr) {
        delete (app_ctx.encoder);
        app_ctx.encoder = nullptr;
    }

    deinit_post_process();

    server->stopServer();

    release(&app_ctx);

    return 0;
}