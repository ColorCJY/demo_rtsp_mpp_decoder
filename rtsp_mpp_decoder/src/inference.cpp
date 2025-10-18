#include "inference.h"

inline static double __get_us(struct timeval t) { 
    return (t.tv_sec * 1000000 + t.tv_usec); 
}

static void dump_tensor_attr(rknn_tensor_attr *attr) {
    printf("index=%d, name=%s, n_dims=%d, dims=[", attr->index, attr->name, attr->n_dims);
    for(int i=0; i < attr->n_dims; i++) {
        if(i + 1 == attr->n_dims) {
            printf("%d], ", attr->dims[i]);
        }
        else {
            printf("%d, ", attr->dims[i]);
        }
    }
    printf("n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n", attr->n_elems, attr->size, get_format_string(attr->fmt), 
           get_type_string(attr->type), get_qnt_type_string(attr->qnt_type), 
           attr->zp, attr->scale);
}

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz) {
    unsigned char *data;
    int ret;

    data = NULL;

    if (NULL == fp) {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0) {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char *)malloc(sz);
    if (data == NULL) {
        printf("buffer malloc failure.\n");
        return NULL;
    }
    ret = fread(data, 1, sz, fp);
    return data;
}

static unsigned char *load_model(const char *filename, int *model_size) {
    FILE *fp;
    unsigned char *data;

    fp = fopen(filename, "rb");
    if (NULL == fp) {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);

    data = load_data(fp, 0, size);

    fclose(fp);

    *model_size = size;
    return data;
}

int Inference::initialize(const char *model_path, bool info) {
    int ret;
    
    memset(&app_ctx, 0, sizeof(rknn_app_context_t));
    
    int model_data_size = 0;
    unsigned char *model_data = load_model(model_path, &model_data_size);
    if (model_data == NULL) {
        printf("Failed to load model file\n");
        return -1;
    }

    ret = rknn_init(&app_ctx.rknn_ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0) {
        printf("rknn_init error ret=%d\n", ret);
        free(model_data);
        return -2;
    }

    if (model_data) {
        free(model_data);
    }
    if(info) {
        rknn_sdk_version version;
        ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
        if (ret < 0) {
            printf("rknn_query version error ret=%d\n", ret);
            return -3;
        }
        printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);
    }

    ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &app_ctx.io_num, sizeof(rknn_input_output_num));
    if (ret < 0) {
        printf("rknn_query RKNN_QUERY_IN_OUT_NUM error ret=%d\n", ret);
        return -3;
    }
    if(info)
        printf("model input num: %d, output num: %d\n", app_ctx.io_num.n_input, app_ctx.io_num.n_output);

    app_ctx.input_attrs = (rknn_tensor_attr *)malloc(app_ctx.io_num.n_input * sizeof(rknn_tensor_attr));
    memset(app_ctx.input_attrs, 0, app_ctx.io_num.n_input * sizeof(rknn_tensor_attr));
    for (int i = 0; i < app_ctx.io_num.n_input; i++) {
        app_ctx.input_attrs[i].index = i;
        ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_INPUT_ATTR, &(app_ctx.input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query RKNN_QUERY_INPUT_ATTR error ret=%d\n", ret);
            return -4;
        }
        if(info)
            dump_tensor_attr(&(app_ctx.input_attrs[i]));
    }

    app_ctx.output_attrs = (rknn_tensor_attr *)malloc(app_ctx.io_num.n_output * sizeof(rknn_tensor_attr));
    memset(app_ctx.output_attrs, 0, app_ctx.io_num.n_output * sizeof(rknn_tensor_attr));
    for (int i = 0; i < app_ctx.io_num.n_output; i++) {
        app_ctx.output_attrs[i].index = i;
        ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &(app_ctx.output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query RKNN_QUERY_OUTPUT_ATTR error ret=%d\n", ret);
            return -5;
        }
        if(info)
            dump_tensor_attr(&(app_ctx.output_attrs[i]));
    }

    if (app_ctx.output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC && 
        app_ctx.output_attrs[0].type == RKNN_TENSOR_INT8) {
        app_ctx.is_quant = true;
    } else {
        app_ctx.is_quant = false;
    }

    if (app_ctx.input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        if(info)
            printf("model is NCHW input fmt\n");
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_height = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[3];
    } else {
        if(info)
            printf("model is NHWC input fmt\n");
        app_ctx.model_height = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[3];
    }
    
    int size = app_ctx.model_height * app_ctx.model_width * app_ctx.model_channel;
    ret = resize_img.make_dma(app_ctx.model_width, app_ctx.model_height, RK_FORMAT_RGB_888, size, "resize_img");
    if(ret < 0) {
        printf("resize_img make_dma error\n");
        return -6;
    }

    ret = input_img.make_dma(app_ctx.model_width, app_ctx.model_height, RK_FORMAT_RGB_888, size, "input");
    if(ret < 0) {
        printf("input_img make_dma error\n");
        return -7;
    }
    if(info)
        printf("model input height=%d, width=%d, channel=%d\n", app_ctx.model_height, app_ctx.model_width, app_ctx.model_channel);
    
    m_is_init = true;
    m_is_running = true;
    m_inferenceThread = std::thread(&Inference::inference_model, this);
    
    return 0;
}

void Inference::trigger_inference(uint64_t frame_seq) {
    // 不需要检查 is_init 和 is_busy，因为外部已经检查过了
    {
        std::lock_guard<std::mutex> lock(m_signal_mutex);
        m_current_frame_seq = frame_seq;
        m_has_frame = true;
        m_is_busy.store(true);
    }
    m_frame_cv.notify_one();
}

void Inference::inference_model() {
    while(m_is_running) {
        uint64_t frame_seq;
        
        // 等待新帧
        {
            std::unique_lock<std::mutex> lock(m_signal_mutex);
            m_frame_cv.wait(lock, [this]() {
                return m_has_frame || !m_is_running;
            });
            
            if(!m_is_running) {
                break;
            }
            
            if(!m_has_frame) {
                continue;
            }
            
            frame_seq = m_current_frame_seq;
            m_has_frame = false;
        }

        int ret;
        object_detect_result_list detect_result;
        memset(&detect_result, 0, sizeof(object_detect_result_list));
        
        rknn_context ctx = app_ctx.rknn_ctx;
        int model_width = app_ctx.model_width;
        int model_height = app_ctx.model_height;
        int model_channel = app_ctx.model_channel;

        struct timeval start_time, stop_time;
        const float nms_threshold = NMS_THRESH;
        const float box_conf_threshold = BOX_THRESH;

        float scale_w = (float)model_width / src_frame.width;
        float scale_h = (float)model_height / src_frame.height;
        float scale = (scale_w < scale_h) ? scale_w : scale_h;
        int new_width = (int)(src_frame.width * scale);
        int new_height = (int)(src_frame.height * scale);

        rga_buffer_t src;
        rga_buffer_t dst;
        rga_buffer_t resize;
        memset(&dst, 0, sizeof(dst));
        memset(&resize, 0, sizeof(resize));

        resize = wrapbuffer_fd(resize_img.fd, new_width, new_height, RK_FORMAT_RGB_888);
        dst = wrapbuffer_fd(input_img.fd, model_width, model_height, RK_FORMAT_RGB_888);

        src = wrapbuffer_fd(src_frame.fd, src_frame.width, src_frame.height, src_frame.format, 
                           src_frame.width_stride, src_frame.height_stride);
        ret = imresize(src, resize);
        if(ret != IM_STATUS_SUCCESS) {
            printf("imresize failed: %s\n", imStrError((IM_STATUS)ret));
            if(m_encode_callback) {
                // 创建共享指针包装
                auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
                m_encode_callback(frame_ptr);
            }
            m_is_busy.store(false);
            continue;
        }

        int pad_top = (model_height - new_height) / 2;
        int pad_bottom = model_height - new_height - pad_top;
        int pad_left = (model_width - new_width) / 2;
        int pad_right = model_width - new_width - pad_left;

        ret = immakeBorder(resize, dst, pad_top, pad_bottom, pad_left, pad_right, 
                          IM_BORDER_CONSTANT, 0x727272);
        if(ret != IM_STATUS_SUCCESS) {
            printf("immakeBorder failed: %s\n", imStrError((IM_STATUS)ret));
            if(m_encode_callback) {
                // 创建共享指针包装
                auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
                m_encode_callback(frame_ptr);
            }
            m_is_busy.store(false);
            continue;
        }

        rknn_input inputs[1];
        memset(inputs, 0, sizeof(inputs));
        inputs[0].index = 0;
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].size = model_width * model_height * model_channel;
        inputs[0].fmt = RKNN_TENSOR_NHWC;
        inputs[0].pass_through = 0;
        inputs[0].buf = input_img.buf;

        gettimeofday(&start_time, NULL);
        ret = rknn_inputs_set(ctx, app_ctx.io_num.n_input, inputs);
        if(ret < 0) {
            printf("rknn_inputs_set failed: %d\n", ret);
            if(m_encode_callback) {
                // 创建共享指针包装
                auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
                m_encode_callback(frame_ptr);
            }
            m_is_busy.store(false);
            continue;
        }

        rknn_output outputs[app_ctx.io_num.n_output];
        memset(outputs, 0, sizeof(outputs));
        for (int i = 0; i < app_ctx.io_num.n_output; i++) {
            outputs[i].index = i;
            outputs[i].want_float = (!app_ctx.is_quant);
        }

        ret = rknn_run(ctx, NULL);
        if(ret < 0) {
            printf("rknn_run failed: %d\n", ret);
            if(m_encode_callback) {
                // 创建共享指针包装
                auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
                m_encode_callback(frame_ptr);
            }
            m_is_busy.store(false);
            continue;
        }
        
        ret = rknn_outputs_get(ctx, app_ctx.io_num.n_output, outputs, NULL);
        if(ret < 0) {
            printf("rknn_outputs_get failed: %d\n", ret);
            if(m_encode_callback) {
                // 创建共享指针包装
                auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
                m_encode_callback(frame_ptr);
            }
            m_is_busy.store(false);
            continue;
        }
        
        gettimeofday(&stop_time, NULL);

        std::vector<float> out_scales;
        std::vector<int32_t> out_zps;
        for (int i = 0; i < app_ctx.io_num.n_output; ++i) {
            out_scales.push_back(app_ctx.output_attrs[i].scale);
            out_zps.push_back(app_ctx.output_attrs[i].zp);
        }

        letterbox_t letter_box;
        letter_box.x_pad = pad_left;
        letter_box.y_pad = pad_top;
        letter_box.scale = scale;

        post_process(&app_ctx, outputs, &letter_box, box_conf_threshold, nms_threshold, &detect_result);

        ret = rknn_outputs_release(ctx, app_ctx.io_num.n_output, outputs);

        // 检查并重新分配 RGBA 缓冲区
        int rgba_size = src_frame.width * src_frame.height * get_bpp_from_format(RK_FORMAT_RGBA_8888);
        if(rgba_data.get_size() == 0 || 
           rgba_data.width != src_frame.width || 
           rgba_data.height != src_frame.height) {
            
            rgba_data.release();
            ret = rgba_data.make_dma(src_frame.width, src_frame.height, RK_FORMAT_RGBA_8888, rgba_size, "rgba");
            if (ret < 0) {
                printf("alloc rgba_data buffer failed!\n");
                if(m_encode_callback) {
                    // 创建共享指针包装
                    auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
                    m_encode_callback(frame_ptr);
                }
                m_is_busy.store(false);
                continue;
            }
        }

        memset(rgba_data.buf, 0, rgba_size);
        rga_buffer_t rgba = wrapbuffer_fd(rgba_data.fd, src_frame.width, src_frame.height, rgba_data.format);

        im_rect rect[detect_result.count];
        for (int i = 0; i < detect_result.count; i++) {
            object_detect_result *det_result = &(detect_result.results[i]);
            // printf("%s @ (%d %d %d %d) %f\n", coco_cls_to_name(det_result->cls_id), 
            //        det_result->box.left, det_result->box.top,
            //        det_result->box.right, det_result->box.bottom, det_result->prop);
            
            int x1 = det_result->box.left;
            int y1 = det_result->box.top;
            int x2 = det_result->box.right;
            int y2 = det_result->box.bottom;

            rect[i] = {
                std::max(0, x1),
                std::max(0, y1),
                std::min(x2, src_frame.width - 1) - std::max(0, x1) + 1,
                std::min(y2, src_frame.height - 1) - std::max(0, y1) + 1
            };
            
        }

        ret = imrectangleArray(rgba, rect, detect_result.count, 0x000000FF, 4);
        if (ret != IM_STATUS_SUCCESS) {
            printf("imrectangle failed: %s\n", imStrError((IM_STATUS)ret));
            if(m_encode_callback) {
                // 创建共享指针包装
                auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
                m_encode_callback(frame_ptr);
            }
            continue;
        }
        // 合成结果
        src = wrapbuffer_fd(src_frame.fd, src_frame.width, src_frame.height, src_frame.format, 
                           src_frame.width_stride, src_frame.height_stride);
        ret = imcomposite(rgba, src, src);
        if (ret != IM_STATUS_SUCCESS) {
            printf("imcomposite failed: %s\n", imStrError((IM_STATUS)ret));
        }

        // 调用编码回调
        if(m_encode_callback) {
            // 创建共享指针包装
            auto frame_ptr = std::shared_ptr<dma_data_t>(&src_frame, [](dma_data_t*){});
            m_encode_callback(frame_ptr);
        }
        
        // 处理完成，标记为非忙碌
        m_is_busy.store(false);
    }
}

void Inference::release() {
    m_is_running = false;
    m_frame_cv.notify_all();
    
    if(m_inferenceThread.joinable()) {
        m_inferenceThread.join();
    }
    
    src_frame.release();
    resize_img.release();
    input_img.release();
    rgba_data.release();
    
    if(app_ctx.rknn_ctx) {
        rknn_destroy(app_ctx.rknn_ctx);
        app_ctx.rknn_ctx = 0;
    }
    
    if(app_ctx.input_attrs) {
        free(app_ctx.input_attrs);
        app_ctx.input_attrs = nullptr;
    }
    
    if(app_ctx.output_attrs) {
        free(app_ctx.output_attrs);
        app_ctx.output_attrs = nullptr;
    }

    deinit_post_process();
    
    m_is_init = false;
}
