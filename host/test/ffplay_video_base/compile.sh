#-I 指定头文件的搜索路径， -L指定动态库的搜索路径 -l指定要链接的动态库
#-lswresample解决av_register_all()函数引入的编译问题 -lpthread：编译线程
g++ -I ../../include/	\
ffplay_video_base.cpp -o ffplay_video_base \
-L ../../lib/ -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lSDL2main -lSDL2 -lswscale -lswresample -lpthread

cp ./ffplay_video_base /home/jiyi/code/ffmpeg/host/test/ffplay_video_base/
