cmake_minimum_required(VERSION 3.10)
project(rtsp_mpp_decoder)

set(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -O3")

# RK3588 MPP 库路径
set(MPP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/mpp/include")
set(MPP_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/mpp/lib")

set(RGA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/librga/include")
set(RGA_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/librga/libs/Linux/gcc-aarch64")

set(RKNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/rknn/include")
set(RKNN_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/rknn/lib")

set(ZLMEDIAKIT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/zlmediakit/include")
set(ZLMEDIAKI_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/zlmediakit/aarch64")

set(FREETYPE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/freetype/include/freetype2")
set(FREETYPE_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/freetype/lib")

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${ZLMEDIAKIT_INCLUDE_DIR}
    ${MPP_INCLUDE_DIR}
    ${RGA_INCLUDE_DIR}
    ${RKNN_INCLUDE_DIR}
    ${FREETYPE_INCLUDE_DIR}
)

link_directories(
    ${ZLMEDIAKI_LIB_DIR}
    ${MPP_LIB_DIR}
    ${RKNN_LIB_DIR}
    ${RGA_LIB_DIR}
    ${FREETYPE_LIB_DIR}
)

# 源文件
set(SOURCES
    src/main.cpp
    src/mpp_decoder.cpp
    src/encode_video.cpp
    src/postprocess.cpp
    src/dma_alloc.cpp
    src/rtsp_server.cpp
    src/INIReader.cpp
    src/ini.c
    src/inference.cpp
    src/label_render.cpp
)

add_executable(rtsp_mpp_decoder ${SOURCES})

target_link_libraries(rtsp_mpp_decoder
    rockchip_mpp
    rknnrt
    rga
    mk_api
    pthread
    libfreetype.a
)

# 安装
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install/rtsp_mpp_decoder" CACHE PATH "Installation Directory" FORCE)

# 安装可执行文件到根目录（不带bin目录）
install(TARGETS rtsp_mpp_decoder DESTINATION .)
install(DIRECTORY ${MPP_LIB_DIR}/ 
    DESTINATION lib
    FILES_MATCHING PATTERN "librockchip_mpp.so*"
    PATTERN "*.a" EXCLUDE
    PATTERN "pkgconfig" EXCLUDE  # 排除 pkgconfig 目录
)
install(FILES ${RKNN_LIB_DIR}/librknnrt.so DESTINATION lib)
install(FILES ${RGA_LIB_DIR}/librga.so DESTINATION lib)
install(FILES ${ZLMEDIAKI_LIB_DIR}/libmk_api.so DESTINATION lib)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rtsp_mpp_decoder.sh 
    DESTINATION .
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config.ini.example
    DESTINATION .
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../model 
    DESTINATION .
)