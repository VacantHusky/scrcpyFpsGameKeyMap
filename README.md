本仓库复刻自(<https://github.com/Genymobile/scrcpy>)。

在其原有基础上添加了按键映射。

对于基础功能，参见源仓库。

# 1. 使用

从[发行版](https://github.com/VacantHusky/scrcpyFpsGameKeyMap/releases/tag/What)下载对应系统的构建文件。

在`fps_game_config.txt`中编辑按键坐标，其坐标值应在[0, 1]。

在该目录，按住Shift，右键，在终端打开，输入命令运行。

若使用usb连接手机，则输入命令：
```shell
./scrcpy -d --no-key-repeat -m 1280
```

若使用无线连接，则输入：
```shell
./scrcpy -d --tcpip=你手机的ip:端口 --no-key-repeat -m 1280
# 例如
./scrcpy --tcpip=192.168.5.102:5555 --no-key-repeat -m 1280
```

若你的手机还未开启无线连接，可以先使用usb连接，然后输入以下命令开启无线连接：
```shell
adb tcpip 5555 
```


`fps_game_config.txt`中的按键解释如下：

```
pointX = 0.55; // 初始视角点
pointY = 0.4;
speedRatioX = 0.00025; // 鼠标速度
speedRatioY = 0.0006;
wheelCenterposX = 0.20;  // 方向轮盘中心点
wheelCenterposY = 0.75;
wheelLeftOffset = 0.1;  // 上下左右滑动的距离
wheelRightOffset = 0.1;
wheelUpOffset = 0.24;
wheeldownOffset = 0.2;
leftProbeX = 0.145; // 左右探头
leftProbeY = 0.364;
rightProbeX = 0.21;
rightProbeY = 0.364;
autoRunX = 0.84; // 自动跑
autoRunY = 0.26;
jumpX = 0.94; // 跳
jumpY = 0.7;
mapX = 0.95; // 地图
mapY = 0.03;
knapsackX = 0.09; // 背包
knapsackY = 0.9;
dropX = 0.91; // 趴
dropY = 0.9;
squatX = 0.84; // 蹲
squatY = 0.93;
reloadX = 0.76; // 装弹
reloadY = 0.93;
pickup1X = 0.7; // 拾取1
pickup1Y = 0.34;
pickup2X = 0.7; // 拾取2
pickup2Y = 0.44;
pickup3X = 0.7; // 拾取3
pickup3Y = 0.54;
switchGun1X = 0.45; // 换枪1
switchGun1Y = 0.9;
switchGun2X = 0.55; // 换枪2
switchGun2Y = 0.9;
fragX = 0.65; // 手雷
fragY = 0.92;
medicineX = 0.35; // 打药
medicineY = 0.95;
getOffCarX = 0.92; //  下车
getOffCarY = 0.4;
getOnCarX = 0.7; //  上车
getOnCarY = 0.54;
helpX = 0.49; //  救人
helpY = 0.63;
openDoorX = 0.7; //  开门
openDoorY = 0.7;
lickBagX = 0.7; //  舔包
lickBagY = 0.25;
fireX = 0.86; //  开火
fireY = 0.72;
openMirrorX = 0.94; //  开镜
openMirrorY = 0.52;
punctuationX = 0.888; //  万能标点
punctuationY = 0.338;

```