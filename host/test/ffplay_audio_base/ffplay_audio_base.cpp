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

#include <SDL2/SDL.h> /* 使用SDL组件库去播放视频和声音 */

#ifdef __cplusplus 
};
#endif

typedef struct Ffmpeg_Thread
{
    pthread_t threadId;
} Ff_Thread;

typedef struct Ffmpeg_CircleBuff
{
    uint8_t* buffer;
    int      readPos;
    int      writePos;
    int      length;
} CircleBuff;

typedef struct Ffmpeg_PcmFrame
{
    //Buffer:
    //|-----------|-------------|
    //chunk-------pos---len-----|
    Uint8*  audio_chunk;
    Uint32  audio_len;
    Uint8*  audio_pos;
} PcmFrame;

#define DUMP_PCM                    /* dump解码完后的pcm数据 */
#define USE_SDL  (1)                /* 使用使用SDL组件进行播放 */
#define MAX_AUDIO_FRAME_SIZE 192000 /* 48khz mono/stereo 32/16bit */
#define SAMPLE_GET_INPUTCMD(InputCmd) fgets((char *)(InputCmd), (sizeof(InputCmd) - 1), stdin) //TODO

#ifdef DUMP_PCM
static FILE*        g_pPcmFile = NULL;
#endif
static bool         g_bQuit = false;
static CircleBuff*  g_pLoopBuf = NULL;
static PcmFrame*    g_pPcmFrame;

//TODO:PATH
static char* g_FilePath = "/home/jiyi/code/ffmpeg/host/bin/qinglvzhuang.mp3";

int circleBuffQueryFree(CircleBuff* pCircleBuff)
{
    int freeLen, readPos, writePos;

    readPos  = pCircleBuff->readPos;
    writePos = pCircleBuff->writePos;

    if (readPos > writePos)
    {
        freeLen = readPos - writePos;
    }
    else
    {
        freeLen = pCircleBuff->length - (writePos - readPos);
    }

    return freeLen;
}

int circleBuffQueryBusy(CircleBuff* pCircleBuff)
{
    int busyLen, readPos, writePos;

    readPos  = pCircleBuff->readPos;
    writePos = pCircleBuff->writePos;

    if (readPos > writePos)
    {
        busyLen = pCircleBuff->length - (readPos - writePos);
    }
    else
    {
        busyLen = writePos - readPos;
    }

    return busyLen;
}

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

/* The audio function callback takes the following parameters: 
 * stream: A pointer to the audio buffer to be filled 
 * len: The length (in bytes) of the audio buffer 
*/ 
void  fill_audio(void *udata,Uint8 *stream,int len){ 
    //SDL 2.0
    SDL_memset(stream, 0, len);
    if(g_pPcmFrame->audio_len==0)
        return;

    len=(len > g_pPcmFrame->audio_len ? g_pPcmFrame->audio_len : len);  /*  Mix  as  much  data  as  possible  */ 

    SDL_MixAudio(stream, g_pPcmFrame->audio_pos, len, SDL_MIX_MAXVOLUME);
    g_pPcmFrame->audio_pos += len;
    g_pPcmFrame->audio_len -= len;
    g_pLoopBuf->readPos += len;
}

int main(void)
{
    AVFormatContext*    pFormatCtx = NULL;
    AVCodecContext*     pCodecCtx = NULL;
    AVCodec*            pCodec= NULL;
    AVPacket*           packet = NULL;
    AVFrame*            pFrame = NULL;
    SDL_AudioSpec       wanted_spec;  /* SDL组件 */
    struct SwrContext*  au_convert_ctx;
    uint8_t*            out_buffer;

    int index = 0;
    int err = -1;
    int videoIndex = -1, audioIndex = -1;

    g_pLoopBuf  = (CircleBuff*)malloc(sizeof(CircleBuff));
    g_pPcmFrame = (PcmFrame*)malloc(sizeof(PcmFrame));
    err = Ffmpeg_Init(&pFormatCtx, &videoIndex, &audioIndex);
    if(err)
    {
        printf("Error:Ffmpeg_Init failed!\n");
        return -1;
    }

    /* 6.find and open decoder */
    pCodecCtx=pFormatCtx->streams[audioIndex]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL)
    {
        printf("Codec not found. codecid = 0X%x\n", pCodecCtx->codec_id);
        return -1;
    }
    else
    {
        printf("audioIndex codec name  = %s\n", pCodec->name);
    }

    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0)
    {
        printf("Could not open codec.\n");
        return -1;
    }

    /* 7. alloc packet */
    packet=(AVPacket *)av_malloc(sizeof(AVPacket));
    if (NULL == packet)
    {
        printf("Fatal:alloc packet failed!\n");
        return -1;
    }
    av_init_packet(packet);


    /* config output */
    uint64_t            out_channel_layout = AV_CH_LAYOUT_STEREO;
    int                 out_nb_samples     = pCodecCtx->frame_size;   /* nb_samples: per frame size :AAC-1024 MP3-1152， frame_size not fixed */
    AVSampleFormat      out_sample_fmt     = AV_SAMPLE_FMT_S16;       /* bitwide */
    int                 out_sample_rate    = 44100;                   /* sample rate */
    int                 out_channels       = av_get_channel_layout_nb_channels(out_channel_layout); /* channels */
    int                 out_buffer_size    = av_samples_get_buffer_size(NULL, out_channels , out_nb_samples, out_sample_fmt, 1);//Out Buffer Size
    out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    /* cieclebuffer init */
    g_pLoopBuf->buffer   = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    g_pLoopBuf->length   = MAX_AUDIO_FRAME_SIZE * 2;
    g_pLoopBuf->readPos  = 0;
    g_pLoopBuf->writePos = 0;
    memset(g_pLoopBuf->buffer, 0, g_pLoopBuf->length);

    pFrame = av_frame_alloc();
    if (NULL == pFrame)
    {
        printf("Error:alloc pFrame failed!\n");
        return -1;
    }

#if USE_SDL
        //Init
        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  
            printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
            return -1;
        }
        //SDL_AudioSpec
        wanted_spec.freq        = out_sample_rate; 
        wanted_spec.format      = AUDIO_S16SYS; 
        wanted_spec.channels    = out_channels; 
        wanted_spec.silence     = 0; 
        wanted_spec.samples     = out_nb_samples; 
        wanted_spec.callback    = fill_audio; 
        wanted_spec.userdata    = pCodecCtx; 

        if (SDL_OpenAudio(&wanted_spec, NULL)<0){ 
            printf("can't open audio.\n"); 
            return -1; 
        } 
#endif

    //FIX:Some Codec's Context Information is missing
    int64_t in_channel_layout=av_get_default_channel_layout(pCodecCtx->channels);

    /* 重采样 */
    au_convert_ctx = swr_alloc();
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
                                        in_channel_layout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    /* playback */
    SDL_PauseAudio(0);

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

#ifdef DUMP_PCM
    g_pPcmFile = fopen("./ffmpeg_pcm.pcm", "wb");
    if (!g_pPcmFile)
    {
        printf("Error:open pcm file failed!\n");
    }
#endif

    while(1)
    {
        /* go to quit by user input */
        if (true == g_bQuit)
        {
            g_bQuit = false;
            break;
        }
        /* judge buffer for pcm */
        if (out_buffer_size > circleBuffQueryFree(g_pLoopBuf))
        {
            usleep(5);
            continue;
        }
        if (av_read_frame(pFormatCtx, packet)>=0)
        {
            if(packet->stream_index==audioIndex)
            {
                ret = avcodec_decode_audio4( pCodecCtx, pFrame,&got_picture, packet);
                if ( ret < 0 )
                {
                    printf("Error in decoding audio frame.\n");
                    return -1;
                }
                if ( got_picture > 0 )
                {
                    swr_convert(au_convert_ctx,&out_buffer, MAX_AUDIO_FRAME_SIZE,(const uint8_t **)pFrame->data , pFrame->nb_samples);

                    memcpy(g_pLoopBuf->buffer, out_buffer, out_buffer_size);
                    g_pLoopBuf->writePos += out_buffer_size;
#ifdef DUMP_PCM
                    if (g_pPcmFile)
                    {
                        int ret = 0;
                        ret = fwrite(out_buffer, 1, out_buffer_size, g_pPcmFile);
                    }
#endif
                    index++;
                }
#if USE_SDL
                /* Set SDL audio pcm buffer */
                g_pPcmFrame->audio_chunk = (Uint8 *)g_pLoopBuf->buffer;
                g_pPcmFrame->audio_len = circleBuffQueryBusy(g_pLoopBuf);
                g_pPcmFrame->audio_pos = (Uint8 *)g_pLoopBuf->buffer;
                while(g_pPcmFrame->audio_len>0)//Wait until finish
                SDL_Delay(1);
#endif
            }
            av_free_packet(packet);
        }				
    }


    swr_free(&au_convert_ctx);
 
#if USE_SDL
    SDL_CloseAudio();//Close SDL
    SDL_Quit();
#endif
#ifdef DUMP_PCM
    if (g_pPcmFile)
    {
        fclose(g_pPcmFile);
        g_pPcmFile = NULL;
    }
#endif
    av_free(out_buffer);
    av_free(g_pLoopBuf->buffer);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);	
    return 0;
}

