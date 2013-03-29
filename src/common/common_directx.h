//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#pragma once

#include "common_utils.h"

#include <initguid.h>
#pragma warning(disable : 4201) // Disable annoying DX warning
#include <d3d9.h>
#include <dxva2api.h>

// =================================================================
// DirectX functionality required to manage D3D surfaces
//

// Create DirectX 9 device context
// - Required when using D3D surfaces.
// - D3D Device created and handed to Intel Media SDK
// - Intel graphics device adapter id will be determined automatically (does not have to be primary),
//   but with the following caveats:
//     - Device must be active. Normally means a monitor has to be physically attached to device
//     - Device must be enabled in BIOS. Required for the case when used together with a discrete graphics card
//     - For switchable graphics solutions (mobile) make sure that Intel device is the active device
mfxStatus CreateHWDevice(mfxSession session, mfxHDL* deviceHandle, HWND hWnd);
void CleanupHWDevice();
IDirect3DDevice9Ex* GetDevice();

// Intel Media SDK memory allocator entrypoints....
// - A slightly different allocation procedure is used for encode, decode and VPP
mfxStatus simple_alloc(mfxHDL pthis, mfxFrameAllocRequest *request, mfxFrameAllocResponse *response);
mfxStatus simple_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr);
mfxStatus simple_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr);
mfxStatus simple_gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL *handle);
mfxStatus simple_free(mfxHDL pthis, mfxFrameAllocResponse *response);
