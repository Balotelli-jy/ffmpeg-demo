#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
  
#define __STDC_CONSTANT_MACROS  /* c++中使用C99定义的一些宏 */
  
#ifdef __cplusplus  
extern "C"  
{  
#endif  
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavdevice/avdevice.h>
#include <libswresample/swresample.h> /* 重采样相关 */
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"


#include <SDL2/SDL.h> /* 使用SDL组件库去播放视频和声音 */

#ifdef __cplusplus 
};
#endif

typedef struct Ffmpeg_Thread
{
    pthread_t threadId;
} Ff_Thread;

#define SAMPLE_GET_INPUTCMD(InputCmd) fgets((char *)(InputCmd), (sizeof(InputCmd) - 1), stdin) //TODO
#define Dump_YUV

static bool g_bQuit = false;

//TODO:PATH
static char* g_FilePath = "/home/jiyi/code/ffmpeg/host/test/ffplay_video_base/bigbuckbunny_480x272.hevc";

static int Ffmpeg_Init(AVFormatContext** pFormatCtx, int* videoIndex, int* audioIndex)
{
    /* 1.通过注册 */
    av_register_all();
    avformat_network_init();

    /* 2.申请AVformat */
    *pFormatCtx = avformat_alloc_context();
    if (NULL == *pFormatCtx)
    {
        printf("Error:alloc avformat context failed!\n");
        goto error;
    }

    /* 3.打开输入流文件 */
    if(avformat_open_input(&(*pFormatCtx), g_FilePath, NULL, NULL) != 0)
    {
        printf("Error:Couldn't open input stream!\n");
        goto error;
    }
    /* Dump valid information onto standard error */
    av_dump_format(*pFormatCtx, 0, g_FilePath, false);

    /* 4.查询码流信息 */
    if(avformat_find_stream_info(*pFormatCtx, NULL) < 0)
    {
        printf("Error:Couldn't find stream information!\n");
        goto error;
    }

    /* 5.确认会是否有音视频流 */
    for(int i=0; i < (*pFormatCtx)->nb_streams; i++)
    {
        if((*pFormatCtx)->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
        {
            *videoIndex = i;
        }
        if((*pFormatCtx)->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO)
        {
            *audioIndex = i;
        }
    }
    printf("videoIndex = 0X%x, audioIndex = 0X%x\n", *videoIndex, *audioIndex);

    return 0;
error:
    return -1;
}
static void* Ffmpeg_Keyboard_Quit(void *arg)
{
    char InputCmd[32];

    SAMPLE_GET_INPUTCMD(InputCmd);
    while (1)
    {
        if ('q' == InputCmd[0])
        {
            printf("prepare to quit!\n");
            g_bQuit = true;
            break;
        }
    }
}
int main(void)
{
    AVFormatContext*    pFormatCtx = NULL;
    AVCodecContext*     pCodecCtx = NULL;
    AVCodec*            pCodec= NULL;
    AVPacket*           packet = NULL;
    AVFrame*            pFrame = NULL, *pFrameYUV = NULL;
    struct SwsContext*  img_convert_ctx;   
    uint8_t*            out_buffer;
    FILE*               fp_yuv;
    int index = 0;
    int err = -1;
    int videoIndex = -1, audioIndex = -1;
    int y_size;


    /* SDL varibles */
    int screen_w=0,screen_h=0;
    SDL_Window *screen; 
    SDL_Renderer* sdlRenderer;
    SDL_Texture* sdlTexture;
    SDL_Rect sdlRect;

    err = Ffmpeg_Init(&pFormatCtx, &videoIndex, &audioIndex);
    if(err)
    {
        printf("Error:Ffmpeg_Init failed!\n");
        return -1;
    }

    /* 6.find and open decoder */
    pCodecCtx=pFormatCtx->streams[videoIndex]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL)
    {
        printf("Codec not found. codecid = 0X%x\n", pCodecCtx->codec_id);
        return -1;
    }
    else
    {
        printf("videoIndex codec name  = %s\n", pCodec->name);
    }

    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0)
    {
        printf("Could not open codec.\n");
        return -1;
    }

    /* 7.alloc source */
    pFrame     = av_frame_alloc();
    pFrameYUV  = av_frame_alloc();
    out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  pCodecCtx->width, pCodecCtx->height, 1));    
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize,out_buffer,
                         AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
    packet=(AVPacket *)av_malloc(sizeof(AVPacket));
    printf("--------------- File Information ----------------\n");
    av_dump_format(pFormatCtx, 0, g_FilePath, 0);
    printf("-------------------------------------------------\n");

    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, 
            pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
     
#ifdef Dump_YUV
    fp_yuv= fopen("output.yuv","wb+");
    if (!fp_yuv)
    {
        printf("Error:open yuv file failed!\n");
    }
#endif

    /* SDL begine */
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {  
        printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
        return -1;
    }
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    /* SDL 2.0 Support for multiple windows */
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h, SDL_WINDOW_OPENGL);
    if(!screen) 
    {  
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());  
        return -1;
    }
    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);  
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,pCodecCtx->width,pCodecCtx->height);  

    sdlRect.x=0;
    sdlRect.y=0;
    sdlRect.w=screen_w;
    sdlRect.h=screen_h;
    /* SDL end */

    /* creat keyboard thread */
    int ret;
    int got_picture;
    Ff_Thread* keyboardThread = (Ff_Thread*)malloc(sizeof(Ff_Thread));
    Ff_Thread* audioPlaybackThread = (Ff_Thread*)malloc(sizeof(Ff_Thread));

    ret = pthread_create(&keyboardThread->threadId, NULL, &Ffmpeg_Keyboard_Quit, NULL);
    if (ret)
    {
        printf("Error:pthread_create keyboard failed!\n");
        return -1;
    }
    /* begin decode frame */
    while(av_read_frame(pFormatCtx, packet)>=0)
    {
        if(packet->stream_index == videoIndex)
        {
            ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
            if(ret < 0)
            {
                printf("Decode Error.\n");
                return -1;
            }
            if(got_picture)
            {
                sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, 
                pFrameYUV->data, pFrameYUV->linesize);

#ifdef Dump_YUV
                if (fp_yuv)
                {
                    y_size = pCodecCtx->width*pCodecCtx->height;  
                    fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
                    fwrite(pFrameYUV->data[1], 1, y_size/4, fp_yuv);  //U
                    fwrite(pFrameYUV->data[2], 1, y_size/4, fp_yuv);  //V
                }

#endif
                /* SDL relation */
#if 0
                SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );  
#else
                SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
                pFrameYUV->data[0], pFrameYUV->linesize[0],
                pFrameYUV->data[1], pFrameYUV->linesize[1],
                pFrameYUV->data[2], pFrameYUV->linesize[2]);
#endif

                SDL_RenderClear( sdlRenderer );  
                SDL_RenderCopy( sdlRenderer, sdlTexture,  NULL, &sdlRect);  
                SDL_RenderPresent( sdlRenderer );  
                /* Delay 40ms */
                SDL_Delay(40);
            }
        }
        av_free_packet(packet);         
    } 
    while (1)
    {
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
        if (ret < 0)
            break;
        if (!got_picture)
            break;
        sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, 
        pFrameYUV->data, pFrameYUV->linesize);
#ifdef Dump_YUV
        if (fp_yuv)
        {
            int y_size=pCodecCtx->width*pCodecCtx->height;  
            fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y 
            fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
            fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
        }

#endif
        /* SDL begin */
        SDL_UpdateTexture( sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0] );  
        SDL_RenderClear( sdlRenderer );  
        SDL_RenderCopy( sdlRenderer, sdlTexture,  NULL, &sdlRect);  
        SDL_RenderPresent( sdlRenderer );  
        /* SDL End */
        //Delay 40ms
        SDL_Delay(40);
    } 
    sws_freeContext(img_convert_ctx);
     
#ifdef Dump_YUV
    if (fp_yuv)
    {
     fclose(fp_yuv);       
    }
#endif   
    SDL_Quit();
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}

