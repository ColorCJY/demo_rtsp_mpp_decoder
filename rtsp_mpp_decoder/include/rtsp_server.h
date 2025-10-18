#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <mutex>
#include <string>
#include "rknn_type.h"
#include "mk_mediakit.h"

class RtspServer {
public:
    RtspServer(const PushServer& config);
    ~RtspServer();
    int initZlmMedia();  // 自动初始化服务器
    int stopServer();    // 减少实例计数
    mk_media getZlmMediaHandle() const;
private:
    int initServer();  // 改为私有
    
    static void API_CALL pushResultCallback(void *user_data, int err_code, const char *err_msg);
    static void API_CALL mediasourceRegistCallback(void *user_data, mk_media_source sender, int regist);
    
    void zlmPusherInit();
    void zlmPusherStart();
    void cleanup();
private:
    mk_media m_mediaHandle;
    mk_pusher m_pusherHandle;
    PushServer m_config;
    std::string m_url;
    std::atomic<bool> m_isActive;
    std::atomic<bool> m_isDestroying;  // 新增：标记正在销
    
    static std::mutex s_mutex;
    static int s_instanceCount;
    static bool s_serverInitialized;
};

#endif
