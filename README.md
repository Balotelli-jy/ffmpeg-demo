# ffmpeg-demo
this is a project based on ffmpeg.

#说明
1.FFmpeg简易demo在host/test下面，目前demo仅支持音频播放(卡顿)，dump数据；
2.ffplay测试程序在host/bin目录下，整个host目录是FFmpeg源码编译出来的，生成的文件目录为host，具体可参考FFmpeg在
Ubuntu下面的编译流程；
3.所有测试demo均需要支持sdl的库，否则无声；

#2019.7.3增订说明：
针对音频播放过程中的卡顿问题进行了分析，在pcm数据送往sdl的过程中，加入了环形buffer机制，但是卡顿现象并没有明显改善，但确认
dump数据和重采样之后的数据是一样的，也就是说没有数据丢失；
卡顿原因为SDL每次回调之后，必须要将本次回调的所有数据播完，而这个过程中就无法进行解码了，所以，考虑环形buffer实际上也是为了
增加一个缓冲，但效果不明显，如果想要流畅的播放pcm数据，就需要对SDL回调的机制有所了解，鉴于目前并不清楚这块，所以暂时无法解
决播放卡顿问题，作为遗留；




