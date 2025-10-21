#include "label_render.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <chrono>
#include <algorithm>

YUVLabelRenderer::YUVLabelRenderer() 
    : initialized_(false)
    , ft_library_(nullptr)
    , ft_face_(nullptr)
    , frame_count_(0)
    , last_fps_time_ms_(0)
    , current_fps_(0.0f)
{
    for (int i = 0; i < OBJ_CLASS_NUM; i++) {
        labels_[i] = nullptr;
    }
}

YUVLabelRenderer::~YUVLabelRenderer() {
    cleanup();
}

bool YUVLabelRenderer::initialize(char* labels[OBJ_CLASS_NUM], const Config& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (initialized_.load()) {
        cleanup();
    }
    
    config_ = config;
    
    for (int i = 0; i < OBJ_CLASS_NUM; i++) {
        labels_[i] = labels[i];
    }
    
    if (!initFreeType()) {
        return false;
    }
    
    if (!regenerateAllLabels()) {
        cleanupFreeType();
        return false;
    }
    
    initialized_.store(true);
    printf("YUVLabelRenderer initialized successfully\n");
    return true;
}

bool YUVLabelRenderer::updateConfig(const Config& new_config) {
    if (!initialized_.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    bool need_regenerate = false;
    
    if (new_config.font_size != config_.font_size ||
        new_config.font_path != config_.font_path ||
        new_config.font_color_r != config_.font_color_r ||
        new_config.font_color_g != config_.font_color_g ||
        new_config.font_color_b != config_.font_color_b) {
        need_regenerate = true;
    }
    
    if (new_config.font_path != config_.font_path) {
        cleanupFreeType();
        config_.font_path = new_config.font_path;
        if (!initFreeType()) {
            return false;
        }
    }
    
    config_ = new_config;
    
    if (need_regenerate) {
        return regenerateAllLabels();
    }
    
    return true;
}

bool YUVLabelRenderer::setFontSize(int size) {
    if (!initialized_.load() || size <= 0) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.font_size = size;
    return regenerateAllLabels();
}

bool YUVLabelRenderer::setFontPath(const std::string& path) {
    if (!initialized_.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    cleanupFreeType();
    config_.font_path = path;
    
    if (!initFreeType()) {
        return false;
    }
    
    return regenerateAllLabels();
}

bool YUVLabelRenderer::setFontColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized_.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.font_color_r = r;
    config_.font_color_g = g;
    config_.font_color_b = b;
    
    return regenerateAllLabels();
}

void YUVLabelRenderer::setBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.bg_color_r = r;
    config_.bg_color_g = g;
    config_.bg_color_b = b;
    config_.bg_alpha = alpha;
}

void YUVLabelRenderer::setBoxColor(uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.box_color_r = r;
    config_.box_color_g = g;
    config_.box_color_b = b;
}

void YUVLabelRenderer::setBoxThickness(int thickness) {
    if (thickness > 0) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_.box_thickness = thickness;
    }
}

void YUVLabelRenderer::drawDetection(uint8_t* yuv420sp,
                                    int frame_width, int frame_height,
                                    int class_id, float confidence,
                                    int box_x, int box_y,
                                    bool is_nv21) {
    if (!initialized_.load() || class_id < 0 || class_id >= OBJ_CLASS_NUM) {
        return;
    }

    std::lock_guard<std::mutex> lock(render_mutex_);

    char conf_text[32];
    snprintf(conf_text, sizeof(conf_text), ": %.1f%%", confidence * 100.0f);
    TextImageRGBA conf_img = generateTextImage(conf_text);

    const TextImageRGBA& label_img = label_images_[class_id];
    if (label_img.data.empty()) {
        return;
    }

    int total_width = label_img.width + conf_img.width;
    int total_height = std::max(label_img.height, conf_img.height);

    int text_x = box_x;
    int text_y = box_y - total_height - 6;
    if (text_y < 0) {
        text_y = box_y + 2;
    }

    fillRectYUV420SP(yuv420sp, frame_width, frame_height,
                     text_x - 2, text_y - 2,
                     total_width + 4, total_height + 4,
                     config_.bg_color_r, config_.bg_color_g, config_.bg_color_b,
                     config_.bg_alpha, is_nv21);

    blitRgbaToYUV420SP(yuv420sp, frame_width, frame_height,
                       label_img, text_x, text_y, is_nv21);

    blitRgbaToYUV420SP(yuv420sp, frame_width, frame_height,
                       conf_img, text_x + label_img.width, text_y, is_nv21);
}

void YUVLabelRenderer::drawDetection(uint8_t* y_plane, uint8_t* uv_plane,
                                    int frame_width, int frame_height,
                                    int class_id, float confidence,
                                    int box_x, int box_y,
                                    bool is_nv21) {
    if (uv_plane == y_plane + frame_width * frame_height) {
        drawDetection(y_plane, frame_width, frame_height,
                     class_id, confidence, box_x, box_y, is_nv21);
    } else {
        printf("Warning: Non-contiguous Y and UV planes not fully supported\n");
    }
}

void YUVLabelRenderer::cleanup() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    for (int i = 0; i < OBJ_CLASS_NUM; i++) {
        label_images_[i].data.clear();
        label_images_[i].width = 0;
        label_images_[i].height = 0;
        labels_[i] = nullptr;
    }
    
    cleanupFreeType();
    initialized_.store(false);
}

YUVLabelRenderer::YUVColor YUVLabelRenderer::rgbToYuv(uint8_t r, uint8_t g, uint8_t b) {
    YUVColor yuv;
    yuv.y = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
    yuv.u = (uint8_t)((-0.169 * r - 0.331 * g + 0.500 * b) + 128);
    yuv.v = (uint8_t)((0.500 * r - 0.419 * g - 0.081 * b) + 128);
    return yuv;
}

bool YUVLabelRenderer::initFreeType() {
    if (FT_Init_FreeType(&ft_library_)) {
        printf("Failed to init FreeType\n");
        return false;
    }
    
    if (FT_New_Face(ft_library_, config_.font_path.c_str(), 0, &ft_face_)) {
        printf("Failed to load font: %s\n", config_.font_path.c_str());
        FT_Done_FreeType(ft_library_);
        ft_library_ = nullptr;
        return false;
    }
    
    return true;
}

void YUVLabelRenderer::cleanupFreeType() {
    if (ft_face_) {
        FT_Done_Face(ft_face_);
        ft_face_ = nullptr;
    }
    
    if (ft_library_) {
        FT_Done_FreeType(ft_library_);
        ft_library_ = nullptr;
    }
}

YUVLabelRenderer::TextImageRGBA YUVLabelRenderer::generateTextImage(const char* text) {
    TextImageRGBA result;
    
    if (!ft_library_ || !ft_face_ || !text || strlen(text) == 0) {
        return result;
    }
    
    std::lock_guard<std::mutex> lock(freetype_mutex_);
    
    FT_Set_Pixel_Sizes(ft_face_, 0, config_.font_size);
    
    int total_width = 0;
    int max_ascent = 0;
    int max_descent = 0;
    
    for (const char* p = text; *p; p++) {
        if (FT_Load_Char(ft_face_, (unsigned char)*p, FT_LOAD_RENDER)) {
            continue;
        }
        FT_GlyphSlot slot = ft_face_->glyph;
        total_width += slot->advance.x >> 6;
        
        int ascent = slot->bitmap_top;
        int descent = slot->bitmap.rows - slot->bitmap_top;
        if (ascent > max_ascent) max_ascent = ascent;
        if (descent > max_descent) max_descent = descent;
    }
    
    int max_height = max_ascent + max_descent;
    
    result.width = total_width + 4;
    result.height = max_height + 4;
    result.data.resize(result.width * result.height * 4, 0);
    
    int pen_x = 2;
    int baseline_y = max_ascent + 2;
    
    for (const char* p = text; *p; p++) {
        if (FT_Load_Char(ft_face_, (unsigned char)*p, FT_LOAD_RENDER)) {
            continue;
        }
        
        FT_GlyphSlot slot = ft_face_->glyph;
        FT_Bitmap* bitmap = &slot->bitmap;
        
        int x_offset = pen_x + slot->bitmap_left;
        int y_offset = baseline_y - slot->bitmap_top;
        
        for (unsigned int row = 0; row < bitmap->rows; row++) {
            for (unsigned int col = 0; col < bitmap->width; col++) {
                int px = x_offset + col;
                int py = y_offset + row;
                
                if (px >= 0 && px < result.width && py >= 0 && py < result.height) {
                    uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                    int offset = (py * result.width + px) * 4;
                    
                    result.data[offset + 0] = config_.font_color_r;
                    result.data[offset + 1] = config_.font_color_g;
                    result.data[offset + 2] = config_.font_color_b;
                    result.data[offset + 3] = alpha;
                }
            }
        }
        
        pen_x += slot->advance.x >> 6;
    }
    
    return result;
}

bool YUVLabelRenderer::regenerateAllLabels() {
    printf("Regenerating label atlas...\n");
    
    for (int i = 0; i < OBJ_CLASS_NUM; i++) {
        if (labels_[i] == nullptr || strlen(labels_[i]) == 0) {
            continue;
        }
        
        label_images_[i] = generateTextImage(labels_[i]);
        
        if (label_images_[i].data.empty()) {
            printf("Failed to generate image for label: %s\n", labels_[i]);
            continue;
        }
    }
    
    printf("Label atlas regenerated\n");
    return true;
}

float YUVLabelRenderer::calculateFPS() {
    frame_count_.fetch_add(1, std::memory_order_relaxed);
    
    auto now = std::chrono::steady_clock::now();
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    int64_t last_time = last_fps_time_ms_.load(std::memory_order_relaxed);
    int64_t elapsed = now_ms - last_time;
    
    if (elapsed >= 1000) {
        int frames = frame_count_.exchange(0, std::memory_order_relaxed);
        float fps = frames * 1000.0f / elapsed;
        current_fps_.store(fps, std::memory_order_relaxed);
        last_fps_time_ms_.store(now_ms, std::memory_order_relaxed);
    }
    
    return current_fps_.load(std::memory_order_relaxed);
}

void YUVLabelRenderer::drawFPS(uint8_t* yuv420sp, int frame_width, int frame_height,
                               int x, int y, bool is_nv21) {
    if (!initialized_.load()) return;
    
    std::lock_guard<std::mutex> lock(render_mutex_);
    
    float fps = calculateFPS();
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", fps);
    
    TextImageRGBA fps_img = generateTextImage(fps_text);
    if (fps_img.data.empty()) return;
    
    fillRectYUV420SP(yuv420sp, frame_width, frame_height,
                     x - 2, y - 2, fps_img.width + 4, fps_img.height + 4,
                     config_.bg_color_r, config_.bg_color_g, config_.bg_color_b,
                     config_.bg_alpha, is_nv21);
    
    blitRgbaToYUV420SP(yuv420sp, frame_width, frame_height, fps_img, x, y, is_nv21);
}

void YUVLabelRenderer::fillRectC1(uint8_t* pixels, int w, int h, int stride,
                                 int rx, int ry, int rw, int rh,
                                 uint8_t color, uint8_t alpha) {
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= h) continue;
        uint8_t* p = pixels + stride * y;
        for (int x = rx; x < rx + rw; x++) {
            if (x >= 0 && x < w) {
                p[x] = (p[x] * (255 - alpha) + color * alpha) / 255;
            }
        }
    }
}

void YUVLabelRenderer::fillRectC2(uint8_t* pixels, int w, int h, int stride,
                                 int rx, int ry, int rw, int rh,
                                 uint8_t u, uint8_t v, uint8_t alpha) {
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= h) continue;
        uint8_t* p = pixels + stride * y;
        for (int x = rx; x < rx + rw; x++) {
            if (x >= 0 && x < w) {
                p[x * 2 + 0] = (p[x * 2 + 0] * (255 - alpha) + u * alpha) / 255;
                p[x * 2 + 1] = (p[x * 2 + 1] * (255 - alpha) + v * alpha) / 255;
            }
        }
    }
}

void YUVLabelRenderer::fillRectYUV420SP(uint8_t* yuv420sp,
                                      int w, int h,
                                      int rx, int ry, int rw, int rh,
                                      uint8_t r, uint8_t g, uint8_t b,
                                      uint8_t alpha, bool is_nv21) {
   YUVColor color = rgbToYuv(r, g, b);
   
   uint8_t* Y = yuv420sp;
   fillRectC1(Y, w, h, w, rx, ry, rw, rh, color.y, alpha);
   
   uint8_t* UV = yuv420sp + w * h;
   
   if (is_nv21) {
       fillRectC2(UV, w / 2, h / 2, w, rx / 2, ry / 2, rw / 2, rh / 2,
                 color.v, color.u, alpha);
   } else {
       fillRectC2(UV, w / 2, h / 2, w, rx / 2, ry / 2, rw / 2, rh / 2,
                 color.u, color.v, alpha);
   }
}

void YUVLabelRenderer::blitRgbaToYUV420SP(uint8_t* yuv420sp,
                                        int w, int h,
                                        const TextImageRGBA& img,
                                        int dst_x, int dst_y,
                                        bool is_nv21) {
   if (img.data.empty()) return;
   
   uint8_t* Y = yuv420sp;
   uint8_t* UV = yuv420sp + w * h;
   
   for (int y = 0; y < img.height; y++) {
       int fy = dst_y + y;
       if (fy < 0 || fy >= h) continue;
       
       for (int x = 0; x < img.width; x++) {
           int fx = dst_x + x;
           if (fx < 0 || fx >= w) continue;
           
           int src_idx = (y * img.width + x) * 4;
           uint8_t alpha = img.data[src_idx + 3];
           
           if (alpha == 0) continue;
           
           uint8_t r = img.data[src_idx + 0];
           uint8_t g = img.data[src_idx + 1];
           uint8_t b = img.data[src_idx + 2];
           
           YUVColor yuv = rgbToYuv(r, g, b);
           
           int y_idx = fy * w + fx;
           Y[y_idx] = (Y[y_idx] * (255 - alpha) + yuv.y * alpha) / 255;
           
           if ((fy % 2 == 0) && (fx % 2 == 0)) {
               int uv_idx = (fy / 2) * w + (fx / 2) * 2;
               if (is_nv21) {
                   UV[uv_idx + 0] = (UV[uv_idx + 0] * (255 - alpha) + yuv.v * alpha) / 255;
                   UV[uv_idx + 1] = (UV[uv_idx + 1] * (255 - alpha) + yuv.u * alpha) / 255;
               } else {
                   UV[uv_idx + 0] = (UV[uv_idx + 0] * (255 - alpha) + yuv.u * alpha) / 255;
                   UV[uv_idx + 1] = (UV[uv_idx + 1] * (255 - alpha) + yuv.v * alpha) / 255;
               }
           }
       }
   }
}
