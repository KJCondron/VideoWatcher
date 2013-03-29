//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Windows.h>


#define MASTER_THREADNUM 0
#define MAX_LINE 1024
#define MAX_TASKS 500

int main(int argc, char **argv)
{
    if (argc<3)
    {
        puts("usage: multi_omp taskfile nthreads");
        exit(1);
    }

    char taskstrings[MAX_TASKS][MAX_LINE];
    memset(taskstrings,0,MAX_TASKS*MAX_LINE);
    omp_set_num_threads(atoi(argv[2]));
    FILE * taskfile=fopen(argv[1],"r");
    if (!taskfile)
    {
        puts("could not open task file. exiting.");
        exit(1);
    }

    int ntasks=0;
    while (!feof(taskfile))
    {
        fgets(taskstrings[ntasks++],MAX_LINE-1,taskfile);
    }
    fclose(taskfile);


#pragma omp parallel 
    {
        int omp_threadnum = omp_get_thread_num();
        if (MASTER_THREADNUM == omp_threadnum)
        {
            printf("Starting %d threads\n",omp_get_num_threads());
        }

#pragma omp for 
        for (int i=0; i<ntasks; i++)
        {
            if (strnlen(taskstrings[i],MAX_LINE)>1)
            {
                printf("thread %d cmd: %s\n",omp_threadnum,taskstrings[i]);
                system(taskstrings[i]);
            }
            //system("c:/videos/run_mfx_transcoder.bat");
            //system("c:/videos/run_simple.bat");
        }

    }  /* end of parallel section */

}
