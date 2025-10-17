#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <string>
#include "mk_mediakit.h"

struct PushServer {
    std::string type = "rtsp";
    std::string vhost = "__defaultVhost__";
    std::string app = "live";
    std::string stream = "test";
    int port = 8554;
};

class RtspServer {
public:
    RtspServer(const PushServer& config);
    ~RtspServer();

    // 禁止拷贝
    RtspServer(const RtspServer&) = delete;
    RtspServer& operator=(const RtspServer&) = delete;

    int initServer();
    int stopServer();
    int initZlmMedia();
    mk_media getZlmMediaHandle() const;

private:
    static void API_CALL pushResultCallback(void *user_data, int err_code, const char *err_msg);
    static void API_CALL mediasourceRegistCallback(void *user_data, mk_media_source sender, int regist);
    
    void zlmPusherInit();
    void zlmPusherStart();

private:
    mk_media m_mediaHandle;
    mk_pusher m_pusherHandle;
    PushServer m_config;
    std::string m_url = "rtsp://127.0.0.1/live/camera1";
};

#endif
