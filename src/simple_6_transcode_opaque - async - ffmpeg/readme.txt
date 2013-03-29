FFmpeg binaries and include files (in the tutorial sample folder named "ffmpeg") are not distributed by Intel.

To obtain the binaries and include files required to build this tutorial sample please follow the instructions below:

- FFmpeg files for Microsoft Windows* can be downloaded from http://ffmpeg.zeranoe.com/builds/
  (other Windows builds of FFmpeg also exists, but other packages have not been verified with this integration code)
- Download the following packages:
   (1) "ffmpeg-<build_id>-<arch>-dev.7z" archive file from the "Dev" binary download section on the webpage
   (2) "ffmpeg-<build_id>-<arch>-shared.7z" archive file from the "Shared" binary download section on the webpage
   Note: "<arch>" is either win32 or x64
- Unzip the packages
- Copy the following files from the unzipped folders to the tutorial sample "ffmpeg" folder
   - Copy all files and subfolders in "include" folder from (1) to "ffmpeg/include" folder
   - Copy all *.lib files of "lib" folder from (1) to "ffmpeg/lib_<arch>" folder
   - Copy all *.dll files of "bin" folder from (2) to "ffmpeg/lib_<arch>" folder
   
- The tutorial sample also requires a type bridge to be able to build FFmpeg projects using Microsoft Visual Studio*
   - Download the "msinttypes-r26.zip" package containing the required include files from http://code.google.com/p/msinttypes/ 
   - Copy the files part of "msinttypes-r26.zip" to the "ffmpeg/include/msinttypes-r26"

   
For further details, the following white paper also have instructions on how to obtain FFmpeg binaries and include files:
http://software.intel.com/en-us/articles/integrating-intel-media-sdk-with-ffmpeg-for-muxdemuxing-and-audio-encodedecode-usages

Observe:
The tutorial sample was tested and integrated using FFmpeg build 1.1.1 (from January 20 2013) from http://ffmpeg.zeranoe.com/builds/ .
Since FFmpeg is a constantly changing open source framework there is no guarantee that this sample will be compatible with future revisions of FFmpeg.
