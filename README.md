# ESP32-S3-AmourK08-Speaker
ESP32-S3-AmourK08-Speaker dev board

## Project Description
这是一个廉价蓝牙音响AmourK08双按键无TF插槽版本硬件改装项目代码，
包含测试和调试使用的示例代码，以及相关说明。
代码基于Arduino开发，使用PlatformIO工具进行编译。
[硬件开源](https://oshwhub.com/hzy3774/armour-lan-ya-yin-xiang-k08)
![图片展示](https://image.lceda.cn/oshwhub/8428d19e9a5b4950aab0415b1ffd8aa4.png)

## 核心组件
* ESP32-S3模组
* MAX98357模组(音频DAC以及功放)
* CD42电池充放电管理模块(带开关机按键接入)
* 1.3寸IPS屏幕模块(SPI驱动，分辨率240x240，7针引脚)

## 相关链接
* S3模组相关说明[链接](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_cn.pdf)
* MAX98357音频DAC相关说明[链接](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)