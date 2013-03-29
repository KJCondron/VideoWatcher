//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#include "ffmpeg_integration.h"

#include <vector>
#include <algorithm>

// Disable type conversion warning to remove annoying complaint about the ffmpeg "libavutil\common.h" file
#pragma warning( disable : 4244 )

#ifdef __cplusplus

#define __STDC_CONSTANT_MACROS
#ifdef _STDINT_H
#undef _STDINT_H
#endif
#include <stdint.h>

extern "C"
{
#endif

#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavcodec/avcodec.h"

#ifdef __cplusplus
} // extern "C"
#endif

// -------------------------------------------------------------------

// Demuxer parameter context
mfxU32						g_videoType;
AVFormatContext*			g_pFormatCtx		= NULL;
int							g_videoStreamIdx;
int							g_videoStreamMuxIdx;
AVBitStreamFilterContext*	g_pBsfc				= NULL;

// Muxer parameter context
AVOutputFormat*				g_pFmt				= NULL;
AVFormatContext*			g_pFormatCtxMux		= NULL;
AVStream*					g_pVideoStream		= NULL;
mfxU8*						g_pExtDataBuffer	= NULL;	 
mfxU16						g_GopRefDist;

// Common video parameter context
AVRational					g_dec_time_base;	// time base of the decoded time stamps
std::vector<int64_t>		g_ptsStack;			// Store window of decoded timestamps. Used for determing DTS timestamps

#ifdef PROCESS_AUDIO
// Demux and Mux parameters required for audio processing
int							g_audioStreamIdx;
int							g_audioStreamMuxIdx;
AVStream*					g_pAudioStream		= NULL;
AVStream*					g_pAudioStreamMux	= NULL;
AVRational					g_audio_dec_time_base;
uint8_t*					g_audioExtraData	= NULL;
#endif

// -------------------------------------------------------------------

mfxStatus FFmpeg_Reader_Init(const char *strFileName, mfxU32 videoType)
{
    MSDK_CHECK_POINTER(strFileName, MFX_ERR_NULL_PTR);

    int res;

    g_videoType = videoType;

    // Initialize libavcodec, and register all codecs and formats
    av_register_all();

    // Open input container
    res = avformat_open_input(&g_pFormatCtx, strFileName, NULL, NULL);
    if(res) {
        printf("FFMPEG: Could not open input container\n");
        return MFX_ERR_UNKNOWN;
    }

    // Retrieve stream information
    res = avformat_find_stream_info(g_pFormatCtx, NULL);
    if(res < 0) {
        printf("FFMPEG: Couldn't find stream information\n");
        return MFX_ERR_UNKNOWN;
    }
    

    // Dump container info to console
    av_dump_format(g_pFormatCtx, 0, strFileName, 0);

    // Find the streams in the container
    g_videoStreamIdx = -1;
    for(unsigned int i=0; i<g_pFormatCtx->nb_streams; i++)
    {
        if(g_pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && g_videoStreamIdx == -1)
        {
            g_videoStreamIdx = i;
 
            // save decoded stream timestamp time base
            g_dec_time_base = g_pFormatCtx->streams[i]->time_base;

            if(videoType == MFX_CODEC_AVC)
            {
                // Retrieve required h264_mp4toannexb filter
                g_pBsfc = av_bitstream_filter_init("h264_mp4toannexb");
                if (!g_pBsfc) {
                    printf("FFMPEG: Could not aquire h264_mp4toannexb filter\n");
                    return MFX_ERR_UNKNOWN;
                }
            }
        }
#ifdef PROCESS_AUDIO
        else if(g_pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            g_audioStreamIdx = i;
            g_pAudioStream = g_pFormatCtx->streams[i];
            g_audio_dec_time_base = g_pAudioStream->time_base;
        }
#endif
    }
    if(g_videoStreamIdx == -1)
        return MFX_ERR_UNKNOWN; // Didn't find any video streams in container

    return MFX_ERR_NONE;
}

mfxStatus FFmpeg_Reader_Get_Framerate(mfxU32 *pFrameRateN, mfxU32 *pFrameRateD)
{
    *pFrameRateN=g_pFormatCtx->streams[g_videoStreamIdx ]->r_frame_rate.num;
    *pFrameRateD=g_pFormatCtx->streams[g_videoStreamIdx ]->r_frame_rate.den;
    return MFX_ERR_NONE;
}


mfxStatus FFmpeg_Reader_ReadNextFrame(mfxBitstream *pBS)
{
    MSDK_CHECK_POINTER(pBS, MFX_ERR_NULL_PTR);

    AVPacket packet;
    bool videoFrameFound = false;

    // Read until video frame is found or no more packets (audio or video) in container.
    while(!videoFrameFound)
    {
        if(!av_read_frame(g_pFormatCtx, &packet))
        {
            if(packet.stream_index == g_videoStreamIdx)
            {
                if(g_videoType == MFX_CODEC_AVC)
                {
                    //
                    // Apply MP4 to H264 Annex B filter on buffer
                    //
                    uint8_t *pOutBuf;
                    int outBufSize;
                    int isKeyFrame = packet.flags & AV_PKT_FLAG_KEY;
                    av_bitstream_filter_filter(g_pBsfc, g_pFormatCtx->streams[g_videoStreamIdx]->codec, NULL, &pOutBuf, &outBufSize, packet.data, packet.size, isKeyFrame);

                    // Current approach leads to a duplicate SPS and PPS....., does not seem to be an issue!

                    //
                    // Copy filtered buffer to bitstream
                    //
                    memmove(pBS->Data, pBS->Data + pBS->DataOffset, pBS->DataLength);
                    pBS->DataOffset = 0;
                    memcpy(pBS->Data + pBS->DataLength, pOutBuf, outBufSize);
                    pBS->DataLength += outBufSize; 

                    av_free(pOutBuf);
                }
                else  // MPEG2
                {
                    memmove(pBS->Data, pBS->Data + pBS->DataOffset, pBS->DataLength);
                    pBS->DataOffset = 0;
                    memcpy(pBS->Data + pBS->DataLength, packet.data, packet.size);
                    pBS->DataLength += packet.size; 
                }

                // We are required to tell MSDK that complete frame is in the bitstream!
                pBS->DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;

                // Save PTS timestamp in stream and in PTS window vector
                //   - DTS is discarded since it's not important)
                pBS->TimeStamp = packet.pts;
                g_ptsStack.push_back(pBS->TimeStamp);

                videoFrameFound = true;
            }
#ifdef PROCESS_AUDIO
            else if(packet.stream_index == g_audioStreamIdx)
            {
                // Write the unmodified compressed frame in the media file
                //   - Since this function is called during retrieval of video stream header during which the 
                //     muxer context (g_pFormatCtxMux) is not available any audio frame preceeding the first video frame will be dropped.
                //     (this limitation should be addressed in future code revision...)
                if(g_pFormatCtxMux) {
                    // Rescale audio time base (likely not needed...)
                    packet.pts			= av_rescale_q(packet.pts, g_audio_dec_time_base, g_pAudioStreamMux->time_base);
                    packet.dts			= packet.pts;
                    packet.stream_index	= g_audioStreamMuxIdx;

                    //double realPTS = av_q2d(g_pAudioStreamMux->time_base) * packet.pts;
                    //printf("PTS A: %6lld (%.3f), DTS: %6lld\n", packet.pts, realPTS, packet.dts);

                    // Write unmodified compressed sample to destination container
                    if (av_interleaved_write_frame(g_pFormatCtxMux, &packet)) {
                        printf("FFMPEG: Error while writing audio frame\n");
                        return MFX_ERR_UNKNOWN;
                    }
                }
            }
#endif

            // Free the packet that was allocated by av_read_frame
            av_free_packet(&packet);
        }
        else
        {
            return MFX_ERR_MORE_DATA;  // Indicates that we reached end of container and to stop video decode
        }
    }

    return MFX_ERR_NONE;
}

void FFmpeg_Reader_Reset()
{
    // Move position to beginning of stream
    int res = av_seek_frame(g_pFormatCtx, g_videoStreamIdx, 0, 0);
    if(res < 0) {
        printf("FFMPEG: Couldn't seek to beginning of stream!!!\n");
    }
}

mfxStatus FFmpeg_Reader_Close()
{
    if(g_pBsfc)
        av_bitstream_filter_close(g_pBsfc);
    if(g_pFormatCtx)
        av_close_input_file(g_pFormatCtx);

    return MFX_ERR_NONE;
}

// -------------------------------------------------------------


mfxStatus FFmpeg_Writer_Init(	const char *strFileName,
                                mfxU32 videoType,
                                mfxU16 nBitRate,
                                mfxU16 nDstWidth,
                                mfxU16 nDstHeight,
                                mfxU16 GopRefDist,
                                mfxU8* SPSbuf,
                                int SPSbufsize,
                                mfxU8* PPSbuf,
                                int PPSbufsize)
{
    MSDK_CHECK_POINTER(strFileName, MFX_ERR_NULL_PTR);

    g_GopRefDist = GopRefDist;

    // Initialize libavcodec, and register all codecs and formats
    avcodec_register_all();
    av_register_all();
    avformat_network_init();  //not necessary for file-only transcode

    // Get default output format config based on selected container type
    g_pFmt = av_guess_format(FORMAT_SHORT_NAME, FORMAT_FILENAME, NULL);

    // Sub title processing ignored
    g_pFmt->subtitle_codec = AV_CODEC_ID_NONE;

    switch (videoType)
    {
        case MFX_CODEC_AVC:
            g_pFmt->video_codec = AV_CODEC_ID_H264;
            break;
        case MFX_CODEC_MPEG2:
            g_pFmt->video_codec = AV_CODEC_ID_MPEG2VIDEO;
            break;
        default:
            printf("Unsupported video format\n");
            return MFX_ERR_UNSUPPORTED;
    }

    if (!g_pFmt) {
        printf("FFMPEG: Could not find suitable output format\n");
        return MFX_ERR_UNSUPPORTED;
    }

    // Allocate the output media context
    g_pFormatCtxMux = avformat_alloc_context();
    if (!g_pFormatCtxMux) {
        printf("FFMPEG: avformat_alloc_context error\n");
        return MFX_ERR_UNSUPPORTED;
    }

    g_pFormatCtxMux->oformat = g_pFmt;

    sprintf_s(g_pFormatCtxMux->filename, "%s", strFileName);
    
    if (g_pFmt->video_codec == CODEC_ID_NONE) 
        return MFX_ERR_UNSUPPORTED;

    g_pVideoStream = avformat_new_stream(g_pFormatCtxMux, NULL);
    if (!g_pVideoStream) {
        printf("FFMPEG: Could not alloc video stream\n");
        return MFX_ERR_UNKNOWN;
    }

    g_videoStreamMuxIdx = g_pVideoStream->index;

    AVCodecContext *c = g_pVideoStream->codec;
    c->codec_id		= g_pFmt->video_codec;
    c->codec_type	= AVMEDIA_TYPE_VIDEO;
    c->bit_rate		= nBitRate*1000;
    c->width		= nDstWidth; 
    c->height		= nDstHeight;

    // time base: this is the fundamental unit of time (in seconds) in terms of which frame timestamps are represented. for fixed-fps content,
    //            timebase should be 1/framerate and timestamp increments should be identically 1.
    c->time_base.den = g_pFormatCtx->streams[g_videoStreamIdx]->r_frame_rate.num;
    c->time_base.num = g_pFormatCtx->streams[g_videoStreamIdx]->r_frame_rate.den;

    // Some formats want stream headers to be separate
    if(g_pFormatCtxMux->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

#ifdef PROCESS_AUDIO
    g_pFmt->audio_codec = g_pAudioStream->codec->codec_id;

    // Create new audio stream for the container
    g_pAudioStreamMux = avformat_new_stream(g_pFormatCtxMux, NULL);
    if (!g_pAudioStreamMux) {
        printf("FFMPEG: Could not alloc audio stream\n");
        return MFX_ERR_UNKNOWN;
    }

    g_audioStreamMuxIdx = g_pAudioStreamMux->index;
 
    // Copy audio codec config from input stream to output stream
    AVCodecContext *ca			= g_pAudioStreamMux->codec;
    ca->codec_id				= g_pAudioStream->codec->codec_id;
    ca->codec_type				= AVMEDIA_TYPE_AUDIO;
    ca->sample_rate				= g_pAudioStream->codec->sample_rate; 
    ca->channels				= g_pAudioStream->codec->channels;
    ca->bit_rate				= g_pAudioStream->codec->bit_rate; 
    ca->sample_fmt				= g_pAudioStream->codec->sample_fmt;
    ca->frame_size				= g_pAudioStream->codec->frame_size;
    ca->bits_per_coded_sample	= g_pAudioStream->codec->bits_per_coded_sample;
    ca->channel_layout			= g_pAudioStream->codec->channel_layout;

    ca->time_base = g_pAudioStream->codec->time_base;
    g_pAudioStreamMux->time_base = g_pAudioStream->codec->time_base;
    

    // Extra data apparently contains essential channel config info (must be copied!)
    ca->extradata_size = g_pAudioStream->codec->extradata_size;
    g_audioExtraData = (uint8_t*)av_malloc(ca->extradata_size);
    ca->extradata = g_audioExtraData;
    memcpy(ca->extradata, g_pAudioStream->codec->extradata, ca->extradata_size);


    // Some formats want stream headers to be separate
    if(g_pFormatCtxMux->oformat->flags & AVFMT_GLOBALHEADER)
        ca->flags |= CODEC_FLAG_GLOBAL_HEADER;
#endif

    // Open the output container file
    if (avio_open(&g_pFormatCtxMux->pb, g_pFormatCtxMux->filename, AVIO_FLAG_WRITE) < 0)
    {
        printf("FFMPEG: Could not open '%s'\n", g_pFormatCtxMux->filename);
        return MFX_ERR_UNKNOWN;
    }
 
    g_pExtDataBuffer = (mfxU8*)av_malloc(SPSbufsize + PPSbufsize);
    if(!g_pExtDataBuffer) {
        printf("FFMPEG: could not allocate required buffer\n");
        return MFX_ERR_UNKNOWN;
    }

    memcpy(g_pExtDataBuffer, SPSbuf, SPSbufsize);
    memcpy(g_pExtDataBuffer + SPSbufsize, PPSbuf, PPSbufsize);

    // Codec "extradata" conveys the H.264 stream SPS and PPS info (MPEG2: sequence header is housed in SPS buffer, PPS buffer is empty)
    c->extradata		= g_pExtDataBuffer;
    c->extradata_size	= SPSbufsize + PPSbufsize;

    // Write container header
    if(avformat_write_header(g_pFormatCtxMux, NULL)) {
        printf("FFMPEG: avformat_write_header error!\n");
        return MFX_ERR_UNKNOWN;
    }

    return MFX_ERR_NONE;
}

mfxStatus FFmpeg_Writer_WriteNextFrame(mfxBitstream *pMfxBitstream)
{
    MSDK_CHECK_POINTER(pMfxBitstream, MFX_ERR_NULL_PTR);

    // Note for H.264 :
    //    At the moment the SPS/PPS will be written to container again for the first frame here
    //    To eliminate this we would have to search to first slice (or right after PPS)

    AVPacket pkt;
    av_init_packet(&pkt);

    // Quick and dirty way of keeping timestamp list sorted
    std::sort(g_ptsStack.begin(), g_ptsStack.end());

    pkt.pts = av_rescale_q(pMfxBitstream->TimeStamp, g_dec_time_base, g_pVideoStream->time_base);
    pkt.dts = av_rescale_q(g_ptsStack.front(), g_dec_time_base, g_pVideoStream->time_base);

    // Determine DTS based on number of encoded B frames (g_GopRefDist).
    // This simplistic approach just applies required delay between DTS and PTS to ensure monotonic DTS
    //   - However, this approach assumes that GOP pattern is static. If encoder is inserting frames not following the statoc GOP pattern this approach will likely not work
    //   - Note: Media SDK 2013 will introduce automatic computation of DTS, thus rendering this approach obsolete
    if(g_GopRefDist > 2) {
        g_GopRefDist--;
        pkt.dts = 0;
    }
    else
        g_ptsStack.erase(g_ptsStack.begin(), g_ptsStack.begin()+1);  // Remove first element in PTS vector

    //double realPTS = av_q2d(g_pVideoStream->time_base) * pkt.pts;
    //printf("PTS V: %6lld (%.3f), DTS: %6lld\n", pkt.pts, realPTS, pkt.dts);

    pkt.stream_index	= g_videoStreamMuxIdx;
    pkt.data			= pMfxBitstream->Data;
    pkt.size			= pMfxBitstream->DataLength;

    if(pMfxBitstream->FrameType == (MFX_FRAMETYPE_I | MFX_FRAMETYPE_REF | MFX_FRAMETYPE_IDR))
        pkt.flags |= AV_PKT_FLAG_KEY;

    // Write the compressed frame in the media file
    if (av_interleaved_write_frame(g_pFormatCtxMux, &pkt)) {
        printf("FFMPEG: Error while writing video frame\n");
        return MFX_ERR_UNKNOWN;
    }

    
#ifdef REALTIME_DELAY
    //Assuming constant frame rate.  Will be more complex for VFR
    Sleep((
        (double)g_pFormatCtx->streams[g_videoStreamIdx]->r_frame_rate.den/
        (double)g_pFormatCtx->streams[g_videoStreamIdx]->r_frame_rate.num)*1000);
#endif

    return MFX_ERR_NONE;
}

mfxStatus FFmpeg_Writer_Close()
{
    // Write the trailer, if any. 
    //   The trailer must be written before you close the CodecContexts open when you wrote the
    //   header; otherwise write_trailer may try to use memory that was freed on av_codec_close()
    av_write_trailer(g_pFormatCtxMux);

    // Free the streams
    for(unsigned int i = 0; i < g_pFormatCtxMux->nb_streams; i++) {
        av_freep(&g_pFormatCtxMux->streams[i]->codec);
        av_freep(&g_pFormatCtxMux->streams[i]);
    }

    avio_close(g_pFormatCtxMux->pb);

    av_free(g_pFormatCtxMux);

    if(g_pExtDataBuffer)
        av_free(g_pExtDataBuffer);

#ifdef PROCESS_AUDIO
    if(g_audioExtraData)
        av_free(g_audioExtraData);
#endif

    return MFX_ERR_NONE;
}
