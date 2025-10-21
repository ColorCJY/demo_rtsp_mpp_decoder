#ifndef LABEL_RENDERER_H
#define LABEL_RENDERER_H

#include "postprocess.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

class YUVLabelRenderer {
public:
    struct Config {
        int font_size;
        std::string font_path;
        uint8_t font_color_r, font_color_g, font_color_b;
        uint8_t bg_color_r, bg_color_g, bg_color_b;
        uint8_t bg_alpha;
        uint8_t box_color_r, box_color_g, box_color_b;
        int box_thickness;
        
        Config() 
            : font_size(16)
            , font_path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
            , font_color_r(255), font_color_g(255), font_color_b(255)
            , bg_color_r(0), bg_color_g(0), bg_color_b(0)
            , bg_alpha(180)
            , box_color_r(255), box_color_g(100), box_color_b(100)
            , box_thickness(2)
        {}
    };

    static YUVLabelRenderer& getInstance() {
        static YUVLabelRenderer instance;
        return instance;
    }

    YUVLabelRenderer(const YUVLabelRenderer&) = delete;
    YUVLabelRenderer& operator=(const YUVLabelRenderer&) = delete;

    bool initialize(char* labels[OBJ_CLASS_NUM], const Config& config = Config());
    bool updateConfig(const Config& new_config);
    bool setFontSize(int size);
    bool setFontPath(const std::string& path);
    bool setFontColor(uint8_t r, uint8_t g, uint8_t b);
    void setBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t alpha = 180);
    void setBoxColor(uint8_t r, uint8_t g, uint8_t b);
    void setBoxThickness(int thickness);
    
    void drawDetection(uint8_t* yuv420sp, int frame_width, int frame_height,
                      int class_id, float confidence, int box_x, int box_y,
                      bool is_nv21 = false);
    
    void drawDetection(uint8_t* y_plane, uint8_t* uv_plane,
                      int frame_width, int frame_height,
                      int class_id, float confidence, int box_x, int box_y,
                      bool is_nv21 = false);
    
    void drawFPS(uint8_t* yuv420sp, int frame_width, int frame_height, 
                int x, int y, bool is_nv21 = false);
    
    void cleanup();
    const Config& getConfig() const { return config_; }
    bool isInitialized() const { return initialized_.load(); }

private:
    YUVLabelRenderer();
    ~YUVLabelRenderer();

    struct TextImageRGBA {
        std::vector<uint8_t> data;
        int width, height;
        TextImageRGBA() : width(0), height(0) {}
    };

    struct YUVColor {
        uint8_t y, u, v;
    };

    std::atomic<bool> initialized_;
    Config config_;
    char* labels_[OBJ_CLASS_NUM];
    TextImageRGBA label_images_[OBJ_CLASS_NUM];
    FT_Library ft_library_;
    FT_Face ft_face_;
    
    mutable std::mutex render_mutex_;
    mutable std::mutex freetype_mutex_;
    mutable std::mutex config_mutex_;
    
    std::atomic<int> frame_count_;
    std::atomic<int64_t> last_fps_time_ms_;
    std::atomic<float> current_fps_;

    YUVColor rgbToYuv(uint8_t r, uint8_t g, uint8_t b);
    bool initFreeType();
    void cleanupFreeType();
    TextImageRGBA generateTextImage(const char* text);
    bool regenerateAllLabels();
    float calculateFPS();
    
    void fillRectYUV420SP(uint8_t* yuv420sp, int w, int h,
                         int rx, int ry, int rw, int rh,
                         uint8_t r, uint8_t g, uint8_t b,
                         uint8_t alpha, bool is_nv21);
    
    void blitRgbaToYUV420SP(uint8_t* yuv420sp, int w, int h,
                           const TextImageRGBA& img,
                           int dst_x, int dst_y, bool is_nv21);
    
    void fillRectC1(uint8_t* pixels, int w, int h, int stride,
                   int rx, int ry, int rw, int rh,
                   uint8_t color, uint8_t alpha);
    
    void fillRectC2(uint8_t* pixels, int w, int h, int stride,
                   int rx, int ry, int rw, int rh,
                   uint8_t u, uint8_t v, uint8_t alpha);
};

#endif
