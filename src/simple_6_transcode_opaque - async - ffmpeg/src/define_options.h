//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

// Configuration macros
//
//   Stages:
//
//    -------------------      -----------------------     -----------------
//    | input/splitter  |  ->  | video decode/encode | ->  | output/muxer  |
//    |  (FFmpeg)       |      |        (MSDK)       |     |  (FFmpeg)     |
//    -------------------      -----------------------     ----------------- 
//                       \______(audio packets passed to muxer)______/



// ==== input file to splitter stage ====
#define INPUT_URL               "bbb1920x1080.mp4"

#define PROCESS_AUDIO											// This flag enables the audio pipeline (FFmpeg) 

// ==== video decode/encode stage ====

#define FFMPEG_AUDIO_FORMAT     CODEC_ID_AAC					// Input audio format -- output same, packets are not re-encoded
#define MSDK_DECODE_CODEC       MFX_CODEC_AVC					// Input video format -- match input file format
#define MSDK_ENCODE_CODEC       MFX_CODEC_AVC					// Output video format -- MFX_CODEC_AVC or MFX_CODEC_MPEG2

#define MAXSPSPPSBUFFERSIZE     1000							// Size for SPS/PPS buffer.  Arbitrary... but 1000 should be enough...

// ==== output/muxer stage ====

// Container formats illustrated in this sample is Matroska (mkv) and MPEG4 (mp4). Choose one of them.
//  - UDP streaming also illustrated via the USE_UDP macro
//#define USE_MKV
#define USE_MP4
//#define USE_UDP

#ifdef USE_MKV													// Matroska (mkv) container support
#define OUTPUT_URL              "test.mkv"
#define FORMAT_FILENAME			"dummy.mkv"                         
#define FORMAT_SHORT_NAME		NULL     
#endif
#ifdef USE_MP4													// MPEG-4 (mp4) container support
#define OUTPUT_URL              "test.mp4"
#define FORMAT_FILENAME			NULL                         
#define FORMAT_SHORT_NAME		"mp4"
#endif
#ifdef USE_UDP													// Illustrates UDP streaming
#define OUTPUT_URL              "udp://localhost:1234"
#define FORMAT_FILENAME			NULL                         
#define FORMAT_SHORT_NAME		"mpegts"
#define REALTIME_DELAY 										// for UDP: msec to wait between frames.
#endif


// ==== diagnostic/benchmark output config info ====

#define ENABLE_OUTPUT                                          // Disabling this flag removes printing of progress (saves a few CPU cycles)
#define ENABLE_BENCHMARK                                       // If enabled, show timing info
