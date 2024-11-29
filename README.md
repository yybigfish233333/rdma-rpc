# rdma-rpc

基于rdma的rpc

因为只改了rocket部分所以只提交了rocket部分

整个rpc框架修改自https://github.com/Gooddbird/rocket 这位大佬的rpc框架

用rdma进行双端通信，能够完成双端的传递消息和rpc调度，但暂时是非常粗暴的代码

RoCE环境

TODO:

服务器端有一些错误输出，需要再修改一下

封装一下rdma部分的代码，现有的代码太粗暴了

采用日志输出
