//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#include <stdio.h>

#include "mfxvideo++.h"

#define MSDK_PRINT_RET_MSG(ERR)			{printf("\nReturn on error: error code %d,\t%s\t%d\n\n", ERR, __FILE__, __LINE__);}
#define MSDK_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}


int main()
{
    mfxStatus sts = MFX_ERR_NONE;

    // Initalize Intel Media SDK session
    // - MFX_IMPL_AUTO_ANY selects HW accelaration if available (on any adapter)
    // - Version 1.0 is selected for greatest backwards compatibility.
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = {0, 1};
    MFXVideoSession mfxSession;
    sts = mfxSession.Init(impl, &ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Query selected implementation and version
    sts = mfxSession.QueryIMPL(&impl);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    sts = mfxSession.QueryVersion(&ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    printf("Implementation: %s , Version: %d.%d\n", impl==MFX_IMPL_SOFTWARE?"SOFTWARE":"HARDWARE", ver.Major, ver.Minor);

    mfxSession.Close();

    return 0;
}
