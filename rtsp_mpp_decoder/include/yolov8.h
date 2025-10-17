#ifndef YOLOV8_H
#define YOLOV8_H

#include "rknn_type.h"
#include "dma_alloc.h"
#include "im2d_buffer.h"
#include "postprocess.h"

inline static double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

// 输出信息
static void dump_tensor_attr(rknn_tensor_attr *attr) {
    printf("index=%d, name=%s, n_dims=%d, dims=[", attr->index, attr->name, attr->n_dims);
    for(int i=0;i< attr->n_dims;i++) {
        if(i + 1 == attr->n_dims) {
            printf("%d], ", attr->dims[i]);
        }
        else {
            printf("%d, ", attr->dims[i]);
        }
    }
    printf("n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n", attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

// 加载数据
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

// 加载模型
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

// 模型初始化
static int initialize(const char *model_path, rknn_app_context_t* app_ctx) {
    int ret;
    
    printf("Loading mode...\n");
    int model_data_size = 0;
    unsigned char *model_data = load_model(model_path, &model_data_size);
    if (model_data == NULL) {
        return -1;
    }

    // init ctx
    ret = rknn_init(&app_ctx->rknn_ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -2;
    }

    // free model_data
    if (model_data) {
        free(model_data);
    }

    // get version
    rknn_sdk_version version;
    ret = rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0) {
        printf("rknn_init error ret=%d\n", ret);
        return -3;
    }
    printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

    // query io_num
    ret = rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &app_ctx->io_num, sizeof(rknn_input_output_num));
    if (ret < 0) {
        printf("rknn_query RKNN_QUERY_IN_OUT_NUM error ret=%d\n", ret);
        return -3;
    }
    printf("model input num: %d, output num: %d\n", app_ctx->io_num.n_input, app_ctx->io_num.n_output);

    // input_attrs
    rknn_tensor_attr *input_attrs = (rknn_tensor_attr *)malloc(app_ctx->io_num.n_input * sizeof(rknn_tensor_attr));
    memset(input_attrs, 0, app_ctx->io_num.n_input * sizeof(rknn_tensor_attr));
    for (int i = 0; i < app_ctx->io_num.n_input; i++) {
        input_attrs[i].index = i;
        ret = rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query RKNN_QUERY_INPUT_ATTR error ret=%d\n", ret);
            return -4;
        }
        dump_tensor_attr(&(input_attrs[i]));
    }

    // output_attrs
    rknn_tensor_attr *output_attrs = (rknn_tensor_attr *)malloc(app_ctx->io_num.n_output * sizeof(rknn_tensor_attr));
    memset(output_attrs, 0, app_ctx->io_num.n_output * sizeof(rknn_tensor_attr));
    for (int i = 0; i < app_ctx->io_num.n_output; i++) {
        output_attrs[i].index = i;
        ret = rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0)
        {
            printf("rknn_query RKNN_QUERY_OUTPUT_ATTR error ret=%d\n", ret);
            return -5;
        }
        dump_tensor_attr(&(output_attrs[i]));
    }

    app_ctx->input_attrs = input_attrs;
    app_ctx->output_attrs = output_attrs;

    if (output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC && output_attrs[0].type == RKNN_TENSOR_INT8)
    {
        app_ctx->is_quant = true;
    }
    else
    {
        app_ctx->is_quant = false;
    }

    // record [c h w]
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        printf("model is NCHW input fmt\n");
        app_ctx->model_channel = input_attrs[0].dims[1];
        app_ctx->model_height = input_attrs[0].dims[2];
        app_ctx->model_width = input_attrs[0].dims[3];
    }
    else {
        printf("model is NHWC input fmt\n");
        app_ctx->model_height = input_attrs[0].dims[1];
        app_ctx->model_width = input_attrs[0].dims[2];
        app_ctx->model_channel = input_attrs[0].dims[3];
    }

    int max_temp_size = app_ctx->model_height * app_ctx->model_height * app_ctx->model_channel;
    int dst_size = app_ctx->model_width * app_ctx->model_height * app_ctx->model_channel;

    ret = dma_buf_alloc(CMA_HEAP_PATH, max_temp_size, &app_ctx->resize_dma_fd, (void **)&app_ctx->resize_buf);
    if (ret < 0 || app_ctx->resize_dma_fd <= 0 || app_ctx->resize_buf == NULL) {
        printf("resize dma_buf_alloc failed: ret=%d, fd=%d, buf=%p\n", ret, app_ctx->resize_dma_fd, app_ctx->resize_buf);
        return -6;
    }
    printf("resize dma allocated: fd=%d, buf=%p, size=%d\n", app_ctx->resize_dma_fd, app_ctx->resize_buf, max_temp_size);

    ret = dma_buf_alloc(CMA_HEAP_PATH, dst_size, &app_ctx->dst_dma_fd, (void **)&app_ctx->dst_buf);
    if (ret < 0 || app_ctx->dst_dma_fd <= 0 || app_ctx->dst_buf == NULL) {
        printf("dst dma_buf_alloc failed: ret=%d, fd=%d, buf=%p\n", ret, app_ctx->dst_dma_fd, app_ctx->dst_buf);
        return -7;
    }
    printf("dst dma allocated: fd=%d, buf=%p, size=%d\n", app_ctx->dst_dma_fd, app_ctx->dst_buf, dst_size);

    app_ctx->resize_handle = importbuffer_fd(app_ctx->resize_dma_fd, max_temp_size);
    app_ctx->dst_handle = importbuffer_fd(app_ctx->dst_dma_fd, dst_size);

    printf("resize_handle=%d, dst_handle=%d\n", app_ctx->resize_handle, app_ctx->dst_handle);

    printf("model input height=%d, width=%d, channel=%d\n", app_ctx->model_height, app_ctx->model_width, app_ctx->model_channel);

    return 0;
}

static int inference_model(rknn_app_context_t *app_ctx, image_frame_t *img, object_detect_result_list *detect_result) {
    int ret;
    rknn_context ctx = app_ctx->rknn_ctx;
    int model_width = app_ctx->model_width;
    int model_height = app_ctx->model_height;
    int model_channel = app_ctx->model_channel;

    struct timeval start_time, stop_time;
    const float nms_threshold = NMS_THRESH;
    const float box_conf_threshold = BOX_THRESH;

    // 1. 计算保持比例的缩放
    float scale_w = (float)model_width / img->width;
    float scale_h = (float)model_height / img->height;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;
    int new_width = (int)(img->width * scale);
    int new_height = (int)(img->height * scale);

    
    im_rect src_rect;
    im_rect dst_rect;
    memset(&src_rect, 0, sizeof(src_rect));
    memset(&dst_rect, 0, sizeof(dst_rect));

    rga_buffer_t src;
    rga_buffer_t dst;
    rga_buffer_t resize_img;
    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    memset(&resize_img, 0, sizeof(resize_img));
    int size = model_width * model_height * model_channel;
    
    resize_img = wrapbuffer_fd(app_ctx->resize_dma_fd, new_width, new_height, RK_FORMAT_RGB_888);
    dst = wrapbuffer_fd(app_ctx->dst_dma_fd, model_width, model_height, RK_FORMAT_RGB_888);

    src = wrapbuffer_virtualaddr((void *)img->virt_addr, img->width, img->height, img->format, img->width_stride, img->height_stride);
    ret = imresize(src, resize_img);

    int pad_top = (model_height - new_height) / 2;
    int pad_bottom = model_height - new_height - pad_top;
    int pad_left = (model_width - new_width) / 2;
    int pad_right = model_width - new_width - pad_left;

    auto STATUS = immakeBorder(resize_img, dst, pad_top, pad_bottom, pad_left, pad_right, 
                      IM_BORDER_CONSTANT, 0x727272);

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = model_width * model_height * model_channel;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;

    inputs[0].buf = app_ctx->dst_buf;

    gettimeofday(&start_time, NULL);
    rknn_inputs_set(ctx, app_ctx->io_num.n_input, inputs);

    rknn_output outputs[app_ctx->io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < app_ctx->io_num.n_output; i++)
    {
        outputs[i].index = i;
        outputs[i].want_float = 0;
    }

    ret = rknn_run(ctx, NULL);
    ret = rknn_outputs_get(ctx, app_ctx->io_num.n_output, outputs, NULL);
    gettimeofday(&stop_time, NULL);
    // printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);

    // printf("post process config: box_conf_threshold = %.2f, nms_threshold = %.2f\n", box_conf_threshold, nms_threshold);

    std::vector<float> out_scales;
    std::vector<int32_t> out_zps;
    for (int i = 0; i < app_ctx->io_num.n_output; ++i) {
        out_scales.push_back(app_ctx->output_attrs[i].scale);
        out_zps.push_back(app_ctx->output_attrs[i].zp);
    }

    letterbox_t letter_box;
    letter_box.x_pad = pad_left;
    letter_box.y_pad = pad_top;
    letter_box.scale = scale;

    post_process(app_ctx, outputs, &letter_box, box_conf_threshold, nms_threshold, detect_result);

    ret = rknn_outputs_release(ctx, app_ctx->io_num.n_output, outputs);

    return 0;
}

// 释放
static void release(rknn_app_context_t* app_ctx) {

    rknn_destroy(app_ctx->rknn_ctx);

    free(app_ctx->input_attrs);
    free(app_ctx->output_attrs);

    releasebuffer_handle(app_ctx->resize_handle);
    releasebuffer_handle(app_ctx->dst_handle);

    int size = app_ctx->model_width * app_ctx->model_height * app_ctx->model_channel;
    dma_buf_free(size, &app_ctx->resize_dma_fd, app_ctx->resize_buf);
    dma_buf_free(size, &app_ctx->dst_dma_fd, app_ctx->dst_buf);
}

#endif