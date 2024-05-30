# 昇腾硬件能力开放

> Since 2024年5月30日
>
> @Ambert



## 解码能力
官方称之为VDEC，解码接口分为v1版本和v2版本，目前实验测试使用的是v1版本接口。

官方样例：https://gitee.com/ascend/samples/tree/master/cplusplus/level2_simple_inference/0_data_process/vdec

官方文档：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_000028.html

接口介绍：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_03_0709.html



### 示例

1. samples/vdec

   - 来源于官方示例

   - 实现读取h264/h265文件并解码为yuv像素数据

   - 只会解码一次
2. samples/decandenc
   - 实现读取图片并解码，读取mp4文件->解码->图片叠加（缩放）->编码->封装发送
   - 已封装成acl引擎
   - 解码部分见AclEngine.hpp的Decoder类



## 编码能力

官方称之为VENC，昇腾编码接口分为v1版本和v2版本，目前实验测试使用的是v1版本接口。

官方样例：https://gitee.com/ascend/samples/tree/master/cplusplus/level2_simple_inference/0_data_process/venc

官方文档：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_000029.html

接口介绍：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_03_0715.html



### 示例

1. samples/venc
   - 来源于官方示例
   - 实现读取yuv像素数据并编码为h264/h265文件

   - 只会编码一次
2. samples/decandenc
   - 实现读取图片并解码，读取mp4文件->解码->图片叠加（缩放）->编码->封装发送
   - 已封装成acl引擎
   - 编码部分见AclEngine.hpp的Encoder类



## 图片叠加能力

官方称之为VPC，昇腾相关接口分为v1版本和v2版本，目前实验测试使用的是v1版本接口。

官方样例：https://gitee.com/ascend/samples/tree/master/cplusplus/level2_simple_inference/0_data_process/cropandpaste

官方文档：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_000024.html

接口介绍：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_03_0672.html



### 示例

1. samples/cropandpaste
   - 实现读取jpg图片，裁剪图片部分区域，重新贴回图片指定区域并保存为yuv输出
2. samples/decandenc
   - 实现读取图片并解码，读取mp4文件->解码->图片叠加（缩放）->编码->封装发送
   - 已封装成acl引擎
   - 叠加部分见AclEngine.hpp的ImageHandler类



## 图片读取解码能力

官方称之为JPEGD和PNGD，昇腾相关接口分为v1版本和v2版本，目前实验测试使用的是v1版本接口。测试使用PNGD暂时存在问题，但是用JPEGD的接口可以解码jpg格式和png格式，不过JPEGD接口只支持解码为YUV相关像素格式，不支持如：RGBA，BGRA等格式，因此会丢失透明通道。

官方样例：https://gitee.com/ascend/samples/tree/master/cplusplus/level2_simple_inference/0_data_process/jpegd

官方文档：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_000025.html#sub-ZH-CN_TOPIC_0000001675372916__section118801825154012

接口介绍：https://www.hiascend.com/document/detail/zh/canncommercial/70RC1/inferapplicationdev/aclcppdevg/aclcppdevg_03_0691.html

### 示例

1. samples/jpg/jpegd
   - 实现读取jpg图片并解码为yuv数据，保存yuv数据输出
2. samples/decandenc
   - 实现读取图片并解码，读取mp4文件->解码->图片叠加（缩放）->编码->封装发送
   - 已封装成acl引擎
   - 图片读取叠加部分见AclEngine.hpp的ImageReader类
