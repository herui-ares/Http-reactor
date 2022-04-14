# Http-reactor
本文通过epoll-reactor模型实现一个微型的服务器，包含了GET请求的解析与响应报文的发送功能


程序使用演示：
![](https://github.com/herui-ares/Http-reactor/blob/main/picture/http.png)

执行程序后在浏览器输入IP:端口号/index.html可以得到服务器的响应html网页
![](https://github.com/herui-ares/Http-reactor/blob/main/picture/http1.png)

可以从服务器的后台终端中看到浏览器发送的GET请求内容

![在这里插入图片描述](https://github.com/herui-ares/Http-reactor/blob/main/picture/HTTP2.png)

以及服务器发送的响应报文内容

![](https://github.com/herui-ares/Http-reactor/blob/main/picture/http3.png)


http解析的原理可参考https://blog.csdn.net/weixin_44477424/article/details/124161787?spm=1001.2014.3001.5502
