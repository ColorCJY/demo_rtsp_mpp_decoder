#ifndef LABEL_RENDERER_H
#define LABEL_RENDERER_H
#include "postprocess.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ==================== YUV标签渲染器类 ====================

class YUVLabelRenderer {
public:
    // 配置结构体
    struct Config {
        int font_size;                    // 字体大小
        std::string font_path;            // 字体路径
        uint8_t font_color_r;            // 字体颜色 R
        uint8_t font_color_g;            // 字体颜色 G
        uint8_t font_color_b;            // 字体颜色 B
        uint8_t bg_color_r;              // 背景颜色 R
        uint8_t bg_color_g;              // 背景颜色 G
        uint8_t bg_color_b;              // 背景颜色 B
        uint8_t bg_alpha;                // 背景透明度 (0-255)
        uint8_t box_color_r;             // 边框颜色 R
        uint8_t box_color_g;             // 边框颜色 G
        uint8_t box_color_b;             // 边框颜色 B
        int box_thickness;                // 边框粗细
        
        // 默认配置
        Config() 
            : font_size(16)
            , font_path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
            , font_color_r(255), font_color_g(255), font_color_b(255)  // 白色
            , bg_color_r(0), bg_color_g(0), bg_color_b(0)              // 黑色
            , bg_alpha(180)
            , box_color_r(255), box_color_g(100), box_color_b(100)     // 橙红色
            , box_thickness(2)
        {}
    };
    
    // 构造和析构
    YUVLabelRenderer();
    ~YUVLabelRenderer();
    
    // 禁止拷贝
    YUVLabelRenderer(const YUVLabelRenderer&) = delete;
    YUVLabelRenderer& operator=(const YUVLabelRenderer&) = delete;
    
    // 初始化
    bool initialize(char* labels[OBJ_CLASS_NUM], const Config& config = Config());
    
    // 更新配置（会重新生成标签图片）
    bool updateConfig(const Config& new_config);
    
    // 设置字体大小（会重新生成标签图片）
    bool setFontSize(int size);
    
    // 设置字体路径（会重新生成标签图片）
    bool setFontPath(const std::string& path);
    
    // 设置字体颜色（会重新生成标签图片）
    bool setFontColor(uint8_t r, uint8_t g, uint8_t b);
    
    // 设置背景颜色（无需重新生成）
    void setBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t alpha = 180);
    
    // 设置边框颜色（无需重新生成）
    void setBoxColor(uint8_t r, uint8_t g, uint8_t b);
    
    // 设置边框粗细（无需重新生成）
    void setBoxThickness(int thickness);
    
    // 绘制检测结果 - YUV420SP格式 (单个指针)
    void drawDetection(uint8_t* yuv420sp,
                      int frame_width, int frame_height,
                      int class_id, float confidence,
                      int box_x, int box_y,
                      bool is_nv21 = false);
    
    // 绘制检测结果 - 分离的Y和UV平面 (兼容旧接口)
    void drawDetection(uint8_t* y_plane, uint8_t* uv_plane,
                      int frame_width, int frame_height,
                      int class_id, float confidence,
                      int box_x, int box_y,
                      bool is_nv21 = false);
    
    // 清理资源
    void cleanup();
    
    // 获取当前配置
    const Config& getConfig() const { return config_; }
    
    // 检查是否已初始化
    bool isInitialized() const { return initialized_; }

private:
    // 内部数据结构
    struct TextImageRGBA {
        std::vector<uint8_t> data;
        int width;
        int height;
        
        TextImageRGBA() : width(0), height(0) {}
    };
    
    struct YUVColor {
        uint8_t y, u, v;
    };
    
    std::mutex freetype_mutex;
    // 成员变量
    Config config_;
    char* labels_[OBJ_CLASS_NUM];
    TextImageRGBA label_images_[OBJ_CLASS_NUM];
    bool initialized_;
    FT_Library ft_library_;
    FT_Face ft_face_;
    
    // 内部方法
    YUVColor rgbToYuv(uint8_t r, uint8_t g, uint8_t b);
    
    bool initFreeType();
    void cleanupFreeType();
    
    TextImageRGBA generateTextImage(const char* text);
    bool regenerateAllLabels();
    
    void fillRectYUV420SP(uint8_t* yuv420sp,
                         int w, int h,
                         int rx, int ry, int rw, int rh,
                         uint8_t r, uint8_t g, uint8_t b,
                         uint8_t alpha, bool is_nv21);
    
    void blitRgbaToYUV420SP(uint8_t* yuv420sp,
                           int w, int h,
                           const TextImageRGBA& img,
                           int dst_x, int dst_y,
                           bool is_nv21);
    
    void fillRectC1(uint8_t* pixels, int w, int h, int stride,
                   int rx, int ry, int rw, int rh,
                   uint8_t color, uint8_t alpha);
    
    void fillRectC2(uint8_t* pixels, int w, int h, int stride,
                   int rx, int ry, int rw, int rh,
                   uint8_t u, uint8_t v, uint8_t alpha);
};

#endif // YUV_LABEL_RENDERER_H
