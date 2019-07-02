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

#define USE_SDL 1
#define MAX_AUDIO_FRAME_SIZE 192000 /* 48khz mono/stereo 32/16bit */
#define SAMPLE_GET_INPUTCMD(InputCmd) fgets((char *)(InputCmd), (sizeof(InputCmd) - 1), stdin) //TODO

//add jiyi
static FILE* g_pPcmFile = NULL;
static bool  g_bQuit = false;


typedef struct Ffmpeg_Thread
{
    pthread_t threadId;  
} Ff_Thread;

//TODO:PATH
static char* g_FilePath = "/home/jiyi/code/ffmpeg/host/bin/qinglvzhuang.mp3";


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
//Buffer:
//|-----------|-------------|
//chunk-------pos---len-----|
static  Uint8  *audio_chunk; 
static  Uint32  audio_len; 
static  Uint8  *audio_pos; 
/* The audio function callback takes the following parameters: 
 * stream: A pointer to the audio buffer to be filled 
 * len: The length (in bytes) of the audio buffer 
*/ 
void  fill_audio(void *udata,Uint8 *stream,int len){ 
    //SDL 2.0
    SDL_memset(stream, 0, len);
    if(audio_len==0)
        return; 
 
    len=(len>audio_len?audio_len:len);  /*  Mix  as  much  data  as  possible  */ 

    SDL_MixAudio(stream,audio_pos,len,SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}

int main(void)
{
    AVFormatContext*    pFormatCtx = NULL;
    AVCodecContext*     pCodecCtx = NULL;
    AVCodec*            pCodec= NULL;
    AVPacket*           packet = NULL;
    AVFrame*            pFrame = NULL;
    uint8_t*            out_buffer;
    SDL_AudioSpec wanted_spec;  //SDL组件
    struct SwrContext *au_convert_ctx;
    int index = 0;
    int err = -1;
    int videoIndex = -1, audioIndex = -1;

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
    out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
    pFrame=av_frame_alloc();
    if (NULL == pFrame)
    {
        printf("Fatal:alloc pFrame failed!\n");
        return -1;
    }

#if USE_SDL
        //Init
        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  
            printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
            return -1;
        }
        //SDL_AudioSpec
        wanted_spec.freq = out_sample_rate; 
        wanted_spec.format = AUDIO_S16SYS; 
        wanted_spec.channels = out_channels; 
        wanted_spec.silence = 0; 
        wanted_spec.samples = out_nb_samples; 
        wanted_spec.callback = fill_audio; 
        wanted_spec.userdata = pCodecCtx; 

        if (SDL_OpenAudio(&wanted_spec, NULL)<0){ 
            printf("can't open audio.\n"); 
            return -1; 
        } 
#endif

    //FIX:Some Codec's Context Information is missing
    int64_t in_channel_layout=av_get_default_channel_layout(pCodecCtx->channels);
    //Swr

    au_convert_ctx = swr_alloc();
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
                                        in_channel_layout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    //Play
    SDL_PauseAudio(0);

    int ret;
    int got_picture;
    Ff_Thread*  audioThread = new Ff_Thread;
    /* creat keyboard thread */
    ret = pthread_create(&audioThread->threadId, NULL, &Ffmpeg_Keyboard_Quit, NULL);
    if (ret)
    {
        printf("Error:pthread_create failed!\n");
    }
#if 1 
    //add jiyi
    g_pPcmFile = fopen("./ffmpeg_pcm.pcm", "wb");
    if (!g_pPcmFile)
    {
        printf("jy:open pcm file failed!\n");
    }
#endif
    while(1)
    {
        if (true == g_bQuit)
        {
            g_bQuit = false;
            break;
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
#if 1
                    //printf("index:%5d\t pts:%lld\t packet size:%d\n",index,packet->pts,packet->size);
#endif
                    //add jiyi
                    if (g_pPcmFile)
                    {
                        int ret = 0;
                        ret = fwrite(out_buffer, 1, out_buffer_size, g_pPcmFile);
                    }
                    index++;
                }
#if USE_SDL
                while(audio_len>0)//Wait until finish
                SDL_Delay(1);

                //Set audio buffer (PCM data)
                audio_chunk = (Uint8 *) out_buffer;
                //Audio buffer length
                audio_len =out_buffer_size;
                audio_pos = audio_chunk;

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
    //add jiyi
    if (g_pPcmFile)
    {
        fclose(g_pPcmFile);
        g_pPcmFile = NULL;
    }

    av_free(out_buffer);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);	
    return 0;
}
