# RTSP_MPP_DECODER

## 功能

基于rk3588实现rtsp拉流-mpp解码-yolov8目标检测-叠加目标框-编码-推流，最终可由vlc等播放器播放来自rk3588的推流

## 依赖库

1.[zlmediakit](https://github.com/ZLMediaKit/ZLMediaKit)

2.[rknpu2](https://github.com/airockchip/rknn-toolkit2)

3.[mpp](https://github.com/rockchip-linux/mpp)

4.[librga](https://github.com/airockchip/librga)

5.[模型下载](https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yolov8/README.md)

## Useage
```
git clone https://github.com/ColorCJY/demo_rtsp_mpp_decoder

cd demo_rtsp_mpp_decoder/rtsp_mpp_decoder

mkdir build && cd build

cmake ..

make install

cd install

mv config.ini.example config.ini

修改config.ini当中的内容

tar -zcvf rtsp_mpp_decoder.tar.gz rtsp_mpp_decoder

上传rtsp_mpp_decoder.tar.gz到rk3588板子解压并进入解压后的文件夹

./rtsp_mpp_decoder.sh
```

