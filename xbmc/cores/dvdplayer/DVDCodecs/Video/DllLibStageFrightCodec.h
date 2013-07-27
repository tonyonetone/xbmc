#pragma once

/*
 *      Copyright (C) 2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#if (defined HAVE_CONFIG_H) && (!defined TARGET_WINDOWS)
#include "config.h"
#endif

#include "DynamicDll.h"
#include "DVDVideoCodec.h"

class CWinSystemEGL;

class DllLibStageFrightCodecInterface
{
public:
  virtual ~DllLibStageFrightCodecInterface() {}

  virtual void stf_ctor(void*)=0;
  virtual void stf_dtor(void*)=0;
  
  virtual bool stf_Open(void*, CDVDStreamInfo &hints, CWinSystemEGL* windowing) = 0;
  virtual void stf_Close(void*) = 0;
  virtual int  stf_Decode(void*, uint8_t *pData, int iSize, double dts, double pts) = 0;
  virtual void stf_Reset(void*) = 0;
  virtual bool stf_GetPicture(void*, DVDVideoPicture *pDvdVideoPicture) = 0;
  virtual bool stf_ClearPicture(void*, DVDVideoPicture* pDvdVideoPicture) = 0;
  virtual void stf_SetDropState(void*, bool bDrop) = 0;
  virtual void stf_SetSpeed(void*, int iSpeed) = 0;

  virtual void stf_LockBuffer(void*, EGLImageKHR eglimg) = 0;
  virtual void stf_ReleaseBuffer(void*, EGLImageKHR eglimg) = 0;
};

class DllLibStageFrightCodec : public DllDynamic, DllLibStageFrightCodecInterface
{
  DECLARE_DLL_WRAPPER(DllLibStageFrightCodec, DLL_PATH_LIBSTAGEFRIGHTICS)
  DEFINE_METHOD1(void, stf_ctor, (void* p1))
  DEFINE_METHOD1(void, stf_dtor, (void* p1))
  DEFINE_METHOD3(bool, stf_Open, (void* p1, CDVDStreamInfo &p2, CWinSystemEGL* p3))
  DEFINE_METHOD1(void, stf_Close, (void* p1))
  DEFINE_METHOD5(int, stf_Decode, (void* p1, uint8_t *p2, int p3, double p4, double p5))
  DEFINE_METHOD1(void, stf_Reset, (void* p1))
  DEFINE_METHOD2(bool, stf_GetPicture, (void* p1, DVDVideoPicture * p2))
  DEFINE_METHOD2(bool, stf_ClearPicture, (void* p1, DVDVideoPicture * p2))
  DEFINE_METHOD2(void, stf_SetDropState, (void* p1, bool p2))
  DEFINE_METHOD2(void, stf_SetSpeed, (void* p1, int p2))
  DEFINE_METHOD2(void, stf_LockBuffer, (void* p1, EGLImageKHR p2))
  DEFINE_METHOD2(void, stf_ReleaseBuffer, (void* p1, EGLImageKHR p2))
  BEGIN_METHOD_RESOLVE()
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideoC1Ev, stf_ctor)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideoD1Ev, stf_dtor)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo4OpenER14CDVDStreamInfoP13CWinSystemEGL, stf_Open)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo5CloseEv, stf_Close)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo6DecodeEPhidd, stf_Decode)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo5ResetEv, stf_Reset)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo10GetPictureEP15DVDVideoPicture, stf_GetPicture)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo12ClearPictureEP15DVDVideoPicture, stf_ClearPicture)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo12SetDropStateEb, stf_SetDropState)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo8SetSpeedEi, stf_SetSpeed)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo10LockBufferEPv, stf_LockBuffer)
    RESOLVE_METHOD_RENAME(_ZN17CStageFrightVideo13ReleaseBufferEPv, stf_ReleaseBuffer)
  END_METHOD_RESOLVE()
};
