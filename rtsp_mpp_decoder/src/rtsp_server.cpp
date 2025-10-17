#include "rtsp_server.h"
#include <stdio.h>

RtspServer::RtspServer(const PushServer& config) 
    : m_mediaHandle(nullptr)
    , m_pusherHandle(nullptr)
    , m_config(config)
    {
        m_url = m_config.type + "://127.0.0.1/" + m_config.app + "/" + m_config.stream;
}

RtspServer::~RtspServer() {
    if (m_pusherHandle) {
        mk_pusher_release(m_pusherHandle);
        m_pusherHandle = nullptr;
    }
    if (m_mediaHandle) {
        mk_media_release(m_mediaHandle);
        m_mediaHandle = nullptr;
    }
}

void API_CALL RtspServer::pushResultCallback(void *user_data, int err_code, const char *err_msg) {
    printf("err_code:%d\n", err_code);
    printf("err_msg:%s\n", err_msg);
}

void RtspServer::zlmPusherInit() {
    const char *type   = m_config.type.c_str();
    const char *vhost  = m_config.vhost.c_str();
    const char *app    = m_config.app.c_str();
    const char *stream = m_config.stream.c_str();
    
    m_pusherHandle = mk_pusher_create(type, vhost, app, stream);
    mk_pusher_set_on_result(m_pusherHandle, pushResultCallback, this);
}

void RtspServer::zlmPusherStart() {
    mk_pusher_publish(m_pusherHandle, m_url.c_str());
}

void API_CALL RtspServer::mediasourceRegistCallback(void *user_data, mk_media_source sender, int regist) {
    printf("mk_media_source:%p\n", sender);
    printf("regist type:%x\n", regist);

    if (regist == 1) {
        RtspServer* server = static_cast<RtspServer*>(user_data);
        if (server) {
            server->zlmPusherInit();
            server->zlmPusherStart();
        }
    }
}

int RtspServer::initServer() {
    mk_config config = {
        .thread_num = 0,        // 0表示自动检测CPU核心数
        .log_level = 0,         // 日志级别
        .log_mask = LOG_CONSOLE,// 控制台日志
        .log_file_path = NULL,  // 不输出到文件
        .log_file_days = 0,     // 不保存日志文件
        .ini_is_path = 0,       // ini不是路径
        .ini = NULL,            // 使用默认配置
        .ssl_is_path = 0,       // ssl不是路径
        .ssl = NULL,            // 无SSL证书
        .ssl_pwd = NULL         // 无证书密码
    };

    // 初始化环境
    mk_env_init(&config);
    
    if (m_config.type == "rtsp") {
        mk_rtsp_server_start(m_config.port, 0); // 使用配置的端口, 0表示非SSL
    }

    if (m_config.type == "rtmp") {
        mk_rtmp_server_start(m_config.port, 0); // 使用配置的端口, 0表示非SSL
    }

    return 0;
}

int RtspServer::stopServer() {
    mk_stop_all_server();
    return 0;
}

int RtspServer::initZlmMedia() {
    const char *vhost  = m_config.vhost.c_str();
    const char *app    = m_config.app.c_str();
    const char *stream = m_config.stream.c_str();
    
    m_mediaHandle = mk_media_create(vhost, app, stream, 0, 0, 0);
    mk_media_init_video(m_mediaHandle, 0, 0, 0, 0, 0);
    
    codec_args v_args = {0};
    // 创建轨道
    mk_track v_track = mk_track_create(MKCodecH264, &v_args);
    // 初始化媒体源的视频轨道
    mk_media_init_track(m_mediaHandle, v_track);
    mk_media_set_on_regist(m_mediaHandle, mediasourceRegistCallback, this);
    mk_media_init_complete(m_mediaHandle);
    // 释放资源
    mk_track_unref(v_track);

    return 0;
}

mk_media RtspServer::getZlmMediaHandle() const {
    return m_mediaHandle;
}
