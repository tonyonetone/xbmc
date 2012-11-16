#pragma once

/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
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

#include "DVDVideoCodec.h"
#include "DVDResource.h"
#include "utils/BitstreamConverter.h"
#include "xbmc/linux/LinuxV4l2.h"
#include <string>
#include <queue>
#include <list>

#define NUM_INPUT_PLANES 1

#define NUM_OUTPUT_BUFFERS 2
#define NUM_CONVERT_BUFFERS 2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MFCDemuxPacket 
{
  uint8_t *buff;
  int size;
  double dts;
  double pts;
} MFCDemuxPacket;

typedef struct MFCBuffer 
{
  int index;
  V4L2Buffer m_v4l2Buffer;
} MFCBuffer;

#ifdef __cplusplus
}
#endif

class CDVDVideoCodecMFC : public CDVDVideoCodec
{
public:
  CDVDVideoCodecMFC();
  virtual ~CDVDVideoCodecMFC();
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose();
  virtual int Decode(BYTE* pData, int iSize, double dts, double pts);
  virtual void Reset();
  bool GetPictureCommon(DVDVideoPicture* pDvdVideoPicture);
  virtual bool GetPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual bool ClearPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual void SetDropState(bool bDrop);
  virtual const char* GetName() { return m_name.c_str(); }; // m_name is never changed after open

protected:
  std::string m_name;
  unsigned int m_iDecodedWidth;
  unsigned int m_iDecodedHeight;
  int m_iDecoderHandle;
#if 0
  int m_iVideoHandle;
  int m_iConverterHandle;
#endif

  V4L2Buffer m_v4l2StreamBuffer;

  bool m_bVideoConvert;

  uint32_t m_iMinBuffers;
  int m_iBufferIndex;
  CBitstreamConverter m_converter;
  V4L2Buffer *m_v4l2Buffers;
  V4L2Buffer *m_v4l2ConvertBuffer;
  std::queue<MFCDemuxPacket> m_MFCDemuxPacket;
  std::list<MFCDemuxPacket *> m_MFCDecodeTimeStamp;
  std::queue<MFCDemuxPacket *> m_MFCFrameTimeStamp;

  std::queue<double> m_pts;
  std::queue<double> m_dts;
  std::queue<int> m_index;

  bool m_bDropPictures;

  int m_iBuffer;

  DVDVideoPicture   m_videoBuffer;

  bool AllocateOutputBuffers();
};

inline int align(int v, int a) {
  return ((v + a - 1) / a) * a;
}

