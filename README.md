## SeatRandom随机座位

本项目旨在为初/高中进行考场座位排布，随机算法通过获取时间种子+算法实现，形成较为随机的排布

技术栈：C++

推荐使用环境：Windows10及以上

## 文件说明

| 文件 | 说明 |
|---|---|
| `output/seatrandom` | 主程序：随机算法+读写Excel名单、座位表文件 |
| `seat/xxxx.xlsx`   | 输出：座位表Excel表格文件 |

## 使用方法

双击 `seatrandom.exe` / 将Excel文件拖入 即可使用

**注：使用时需将名单放置在同exe目录下，最好单独创建文件夹放置座位程序**

文件输出在``/seat``中

## 编译(编译器路径按照真实情况修改)

**环境要求：MinGW、Cmake**

**构建命令请在项目根目录执行**  

1.首次编译：

```powershell
D:\CMake\bin\cmake.exe -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=D:/WinLibs/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=D:/WinLibs/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/WinLibs/mingw64/bin/mingw32-make.exe `
  -DCMAKE_PREFIX_PATH=D:/WinLibs/mingw64
D:\WinLibs\mingw64\bin\mingw32-make.exe -C build -j4
```

2.日常编译：
```powershell
D:\WinLibs\mingw64\bin\mingw32-make.exe -C build -j4
```

## Debug（使用时未遇到问题提请忽略）
### Error Code
* **```Error:Can't find xlsx file```：目录下不存在对应的xlsx文件**
* **```Error:Can't read the file```：文件格式错误，请检查xlsx文件是否有问题**