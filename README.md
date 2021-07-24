## ffmpeg推流到RTMP

CMakeLists.txt为 mac下的文件, CMakeLists.txt.bak为适用于树莓派的工具
代码功能简单, 不做说明, 具体见代码

## 使用说明

需要变更git文件夹名字为 CXXDEMO

```bash

mv CMakeLists.txt.bak CMakeLists.txt
mkdir build && cd buid && cmake .. && make 

./CXXDEMO/build/CXXDEMO playlist.txt "rtmp://live-push.bilivideo.comxxxxxxxx" # 即可使用
```
