// rtsp_server.cpp
#include "rtsp_server.h"
#include <chrono>
#include <thread>

std::mutex RtspServer::s_mutex;
int RtspServer::s_instanceCount = 0;
bool RtspServer::s_serverInitialized = false;

RtspServer::RtspServer(const PushServer& config) 
    : m_mediaHandle(nullptr)
    , m_pusherHandle(nullptr)
    , m_config(config)
    , m_isActive(true)
    , m_isDestroying(false)
{
    m_url = m_config.type + "://127.0.0.1/" + m_config.stream_conifg.app+ "/" + m_config.stream_conifg.stream;
    
    std::lock_guard<std::mutex> lock(s_mutex);
    s_instanceCount++;
    printf("RtspServer instance created, count: %d\n", s_instanceCount);
}

RtspServer::~RtspServer() {
    m_isDestroying = true;  // 标记开始销毁
    cleanup();
}

void RtspServer::cleanup() {
    // 先停止推流，避免回调继续执行
    if (m_pusherHandle) {
        mk_pusher_release(m_pusherHandle);
        m_pusherHandle = nullptr;
    }
    
    // 释放media资源
    if (m_mediaHandle) {
        mk_media_release(m_mediaHandle);
        m_mediaHandle = nullptr;
    }
    
    // 等待一小段时间确保回调完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::lock_guard<std::mutex> lock(s_mutex);
    if (m_isActive.exchange(false)) {  // 使用 exchange 确保只执行一次
        s_instanceCount--;
        printf("RtspServer instance destroyed, remaining count: %d\n", s_instanceCount);
        
        if (s_instanceCount == 0 && s_serverInitialized) {
            printf("Last instance destroyed, stopping server...\n");
            
            // 给其他线程一些时间完成操作
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // 关闭服务器
            mk_stop_all_server();
            s_serverInitialized = false;
            
            // 再等待一段时间确保服务器完全停止
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int RtspServer::initServer() {
    std::lock_guard<std::mutex> lock(s_mutex);
    
    if (s_serverInitialized) {
        return 0;
    }
    
    mk_config config = {
        .thread_num = 0,
        .log_level = 0,
        .log_mask = LOG_CONSOLE,
        .log_file_path = NULL,
        .log_file_days = 0,
        .ini_is_path = 0,
        .ini = NULL,
        .ssl_is_path = 0,
        .ssl = NULL,
        .ssl_pwd = NULL
    };

    mk_env_init(&config);
    
    if (m_config.type == "rtsp") {
        mk_rtsp_server_start(m_config.port, 0);
    } else if (m_config.type == "rtmp") {
        mk_rtmp_server_start(m_config.port, 0);
    }

    s_serverInitialized = true;
    return 0;
}

int RtspServer::stopServer() {
    cleanup();
    return 0;
}

int RtspServer::initZlmMedia() {
    initServer();
    
    const char *vhost  = m_config.stream_conifg.vhost.c_str();
    const char *app    = m_config.stream_conifg.app.c_str();
    const char *stream = m_config.stream_conifg.stream.c_str();
    
    m_mediaHandle = mk_media_create(vhost, app, stream, 0, 0, 0);
    mk_media_init_video(m_mediaHandle, 0, 0, 0, 0, 0);
    
    codec_args v_args = {0};
    mk_track v_track = mk_track_create(MKCodecH264, &v_args);
    mk_media_init_track(m_mediaHandle, v_track);
    mk_media_set_on_regist(m_mediaHandle, mediasourceRegistCallback, this);
    mk_media_init_complete(m_mediaHandle);
    mk_track_unref(v_track);

    return 0;
}

void API_CALL RtspServer::pushResultCallback(void *user_data, int err_code, const char *err_msg) {
    RtspServer* server = static_cast<RtspServer*>(user_data);
    
    // 检查对象是否正在销毁
    if (!server || server->m_isDestroying) {
        return;
    }
    
    printf("err_code:%d, err_msg:%s\n", err_code, err_msg);
}

void RtspServer::zlmPusherInit() {
    if (m_isDestroying) return;
    
    m_pusherHandle = mk_pusher_create(m_config.type.c_str(), m_config.stream_conifg.vhost.c_str(), 
                                      m_config.stream_conifg.app.c_str(), m_config.stream_conifg.stream.c_str());
    mk_pusher_set_on_result(m_pusherHandle, pushResultCallback, this);
}

void RtspServer::zlmPusherStart() {
    if (m_isDestroying || !m_pusherHandle) return;
    
    mk_pusher_publish(m_pusherHandle, m_url.c_str());
}

void API_CALL RtspServer::mediasourceRegistCallback(void *user_data, mk_media_source sender, int regist) {
    RtspServer* server = static_cast<RtspServer*>(user_data);
    
    // 检查对象是否有效且未在销毁中
    if (!server || server->m_isDestroying) {
        return;
    }
    
    if (regist == 1) {
        server->zlmPusherInit();
        server->zlmPusherStart();
    }
}

mk_media RtspServer::getZlmMediaHandle() const {
    return m_mediaHandle;
}
