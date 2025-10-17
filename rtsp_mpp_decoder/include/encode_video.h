#pragma once

#include <rockchip/rk_mpi.h>
#include <functional>
#include <string>
#include <thread>

#pragma once

#include <string>
#include <cstdint>

//视频head信息
typedef struct {
        uint8_t data[256];      //head数据
        uint32_t size;      //数据大小
} SpsHeader;


// 输入图片的格式
enum  eFormatType
{
    BGR24 = 0x00,
    RGB24 = 0x01,
    ARGB32 = 0x02,
    ABGR32 = 0x03,
    BGRA32 = 0x04,
    RGBA32 = 0x05,
    YUV420SP = 0x06,
    YUV420P = 0x07,
};


//编码结果流格式
enum eStreamType
{
    H264 = 0x00,
    H265 = 0x01,
    WMV = 0x03,
};

struct FrameInfo {
    uint32_t height;    //frame高
    uint32_t width;     //frame宽
    short format;   //eFormatType
    uint32_t fps;    //帧率
    
};

struct StreamInfo {
    short StreamType;   //eStreamType
    uint32_t gop;       //
};

typedef struct InputInfo{
    int width = 0;          //视频宽
    int height = 0;         //视频高
    int fps = 30;           //视频帧率
    short format;           //eFormatType
    uint8_t data[256];      //head数据
    uint32_t size;          //数据大小
} InputInfo ;

//编码格式
struct MppEncInfo {
    int32_t width;
    int32_t height;
    int32_t hor_stride;
    int32_t ver_stride;
    int32_t frame_size;
    int32_t header_size;
    int32_t mdinfo_size;
    int32_t bps;
    MppCodingType code_type;
    MppFrameFormat frame_format;

    MppEncRcMode rc_mode;
    MppEncSeiMode sei_mode;
    MppEncHeaderMode header_mode;
};

using PacketCallback = std::function<void(uint8_t*, uint32_t, uint64_t, void*)>;


class RKEncodeVideo
{
public:
    RKEncodeVideo();
    ~RKEncodeVideo(void);

    /** * @brief  初始化编码
     * @param   encoderinfo   视频参数信息
     * @param   srcindex   视频流编号
     * @param   writefilecallback   写入文件回调
     * @return  0: sucess ** **/
    int Initencoder(InputInfo& encoderinfo, int srcindex, PacketCallback  callback, void* userdata);

    /** * @brief  推入图片数据
     * @param   data  图片数据
     * @param   size  图片大小
     * @return  0: sucess ** **/
    int WriteData(const uint8_t *data, int size);

      /** * @brief  结束编码
     * @param   
     * @return  ** **/  
    void EndEncode();

private:
     /** * @brief  接收编码结果线程
     * @param   
     * @return  0:  ** **/
    void EncRecvThread();

    /** * @brief  设置mpp 编码资源
     * @return ** **/
    bool SetMppEncCfg(void);

    /** * @brief  初始化Mpp资源
     * @return ** **/
    void InitMppEnc();

    /** * @brief  申请Drm内存
     * @return ** **/
    bool AllocterDrmbuf();

    /** * @brief  初始化MPP API
     * @return ** **/
    bool InitMppAPI();

    /** * @brief  获取视频流head信息
     * @param   sps_header   视频head信息
     * @return ** **/
    void GetHeaderInfo(SpsHeader *sps_header);

    /** * @brief  转换视频流信息为mpp视频格式
     * @param   stream_type   视频流类型
     * @return mpp视频流格式 ** **/
    MppCodingType AdaptStreamType(short stream_type);

    /** * @brief  转换frame格式为mpp格式
     * @param   stream_type   frame 类型
     * @return mppframe格式 ** **/
    MppFrameFormat AdaptFrameType(short stream_type);

    /** * @brief  获取frame大小
     * @param   frame_format   frame 类型
     * @param   hor_stride  
     * @param   ver_stride  
     * @return  ** **/
    int GetFrameSize(MppFrameFormat frame_format, int32_t hor_stride, int32_t ver_stride);

     /** * @brief  获取视频head信息大小
     * @param   frame_format   frame 类型
     * @param   width  
     * @param   height  
     * @return  ** **/
    int GetHeaderSize(MppFrameFormat frame_format, uint32_t width, uint32_t height);

    /** * @brief  释放资源
     * @return ** **/
    void Release();

      /** * @brief  封装为其他格式
     * @param   data   编码后的数据
     * @param   size   数据大小 
     * @return  ** **/
    void Packaging(uint8_t* data,uint32_t size);

private:
    MppCtx m_mppctx = nullptr;
    MppApi* m_mppapi = nullptr;
    MppEncCfg m_mppcfg = nullptr;
    MppBufferGroup m_grpbuffer = nullptr;
    MppBuffer m_frame_buf = nullptr;
    MppBuffer m_pkt_buf = nullptr;
    MppFrame m_frame = nullptr;

    FrameInfo m_frame_info;     //frame 信息
    StreamInfo m_stream_info;   //视频流信息
    MppEncInfo m_enc_info;      //编码格式数据

    bool m_is_running = false;  //是否编码
    bool m_is_init = false;
    std::thread m_recv_thread;  //接收编码结果线程

    PacketCallback  m_callback; // TODO

    void* m_userdata = nullptr;

    int m_put_num = 0;              //接收到的编码帧数量
    int m_encode_num = 0;           //完成编码帧数量
    int m_srcindex = 0;            //视频流编号
    int m_frame_index=0;        //帧序号
};