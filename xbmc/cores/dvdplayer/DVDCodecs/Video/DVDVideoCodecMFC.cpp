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

#include "system.h"
#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#endif
#include "DVDVideoCodecMFC.h"
#include "DVDDemuxers/DVDDemux.h"
#include "DVDStreamInfo.h"
#include "DVDClock.h"
#include "DVDCodecs/DVDCodecs.h"
#include "DVDCodecs/DVDCodecUtils.h"

#include "settings/Settings.h"
#include "settings/DisplaySettings.h"
#include "settings/AdvancedSettings.h"
#include "utils/fastmemcpy.h"

#include <linux/LinuxV4l2.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>

#ifdef CLASSNAME
#undef CLASSNAME
#endif
#define CLASSNAME "CDVDVideoCodecMFC"

//#define USE_FIMC

#define V4L2_PIX_FMT_MPEG2    v4l2_fourcc('M', 'P', 'G', '2') /* MPEG-2 ES     */

CDVDVideoCodecMFC::CDVDVideoCodecMFC() : CDVDVideoCodec()
{
  m_iDecodedWidth = 0;
  m_iDecodedHeight = 0;
  m_iDecoderHandle = -1;
  m_bVideoConvert = false;
  m_iMinBuffers = 0;
  m_v4l2Buffers = NULL;
  m_v4l2ConvertBuffer = NULL;
  m_bDropPictures = false;

  m_iBufferIndex = -1;
  m_decode_buffer = NULL;

  memset(&m_v4l2StreamBuffer, 0, sizeof(V4L2Buffer));
  memset(&m_videoBuffer, 0, sizeof(DVDVideoPicture));
}

CDVDVideoCodecMFC::~CDVDVideoCodecMFC()
{
  Dispose();
}

bool CDVDVideoCodecMFC::AllocateDecodeOutputBuffers()
{
  struct v4l2_control ctrl;
  int ret;
  int i, j;

  if(m_iDecoderHandle < 0)
    return false;

  ctrl.id = V4L2_CID_CODEC_REQ_NUM_BUFS;
  ret = ioctl(m_iDecoderHandle, VIDIOC_G_CTRL, &ctrl);
  if (ret)
  {
    CLog::Log(LOGERROR, "%s::%s - getting min buffers ioctl\n", CLASSNAME, __func__);
    return false;
  }

  m_iMinBuffers = ctrl.value + 1;

  int buffers = CLinuxV4l2::RequestBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 
      V4L2_MEMORY_MMAP, m_iMinBuffers);

  if(m_iMinBuffers != buffers)
  {
    CLog::Log(LOGERROR, "%s::%s - getting min buffers buffers %d got %d\n", CLASSNAME, __func__,
        m_iMinBuffers, buffers);
    return false;
  }

  m_v4l2Buffers = (V4L2Buffer *)calloc(m_iMinBuffers, sizeof(V4L2Buffer));
  if(!m_v4l2Buffers)
  {
    CLog::Log(LOGERROR, "%s::%s - cannot allocate buffers\n", CLASSNAME, __func__);
    return false;
  }

  if(!CLinuxV4l2::MmapBuffers(m_iDecoderHandle, m_iMinBuffers, m_v4l2Buffers, 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP))
  {
    CLog::Log(LOGERROR, "%s::%s - cannot mmap output buffers\n", CLASSNAME, __func__);
    return false;
  }

  return true;
}

bool CDVDVideoCodecMFC::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  struct v4l2_format fmt;
  struct v4l2_capability cap;
  int ret = 0;

  Dispose();

  m_bVideoConvert = m_converter.Open(hints.codec, (uint8_t *)hints.extradata, hints.extrasize, true);

  memset(&fmt, 0, sizeof(struct v4l2_format));

  switch(hints.codec)
  {
    case CODEC_ID_H264:
      fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
      m_name = "mfc-h264";
      break;
    case CODEC_ID_MPEG1VIDEO:
      fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MPEG2;
      m_name = "mfc-mpeg1";
      break;
    case CODEC_ID_MPEG2VIDEO:
      fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MPEG2;
      m_name = "mfc-mpeg2";
      break;
    case CODEC_ID_MPEG4:
      fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MPEG4;
      m_name = "mfc-mpeg4";
      break;
    case CODEC_ID_H263:
      fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H263;
      m_name = "mfc-h263";
      break;
    default:
      return false;
      break;
  }

  uint8_t *extraData = NULL;
  unsigned int extraSize = 0;

  if(m_bVideoConvert)
  {
    if(m_converter.GetExtraData() != NULL && m_converter.GetExtraSize() > 0)
    {
      extraSize = m_converter.GetExtraSize();
      extraData = m_converter.GetExtraData();
    }
  }
  else
  {
    if(hints.extrasize > 0 && hints.extradata != NULL)
    {
      extraSize = hints.extrasize;
      extraData = (uint8_t*)hints.extradata;
    }
  }

  m_iDecoderHandle = open("/dev/video6", O_RDWR | O_NONBLOCK, 0);
  if(m_iDecoderHandle < 0)
  {
    CLog::Log(LOGERROR, "%s::%s - open decoder device /dev/video6", CLASSNAME, __func__);
    return false;
  }

  ret = ioctl(m_iDecoderHandle, VIDIOC_QUERYCAP, &cap);
  if (ret != 0)
  {
    CLog::Log(LOGERROR, "%s::%s - query decoder caps", CLASSNAME, __func__);
    return false;
  }

  // ref: https://patchwork.linuxtv.com/patch/8081//
  // MFC decoder returns wrong caps
  // should be V4L2_CAP_VIDEO_CAPTURE_MPLANE & V4L2_CAP_VIDEO_OUTPUT_MPLANE
  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
    !(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) ||
    !(cap.capabilities & V4L2_CAP_STREAMING))
  {
    CLog::Log(LOGERROR, "%s::%s - insufficient decoder caps\n", CLASSNAME, __func__);
    return false;
  }

  m_iConverterHandle = open("/dev/video4", O_RDWR, 0);
  if(m_iConverterHandle < 0)
  {
    CLog::Log(LOGERROR, "%s::%s - open converter device /dev/video4", CLASSNAME, __func__);
    return false;
  }

  ret = ioctl(m_iConverterHandle, VIDIOC_QUERYCAP, &cap);
  if (ret != 0)
  {
    CLog::Log(LOGERROR, "%s::%s - query converter fimc caps", CLASSNAME, __func__);
    return false;
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
    !(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) ||
    !(cap.capabilities & V4L2_CAP_STREAMING))
  {
    CLog::Log(LOGERROR, "%s::%s - insufficient converter fimc caps\n", CLASSNAME, __func__);
    return false;
  }

  // input format
  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 1024 * 3072;
  fmt.fmt.pix_mp.num_planes = 1;

  ret = ioctl(m_iDecoderHandle, VIDIOC_S_FMT, &fmt);
  if (ret != 0)
  {
    CLog::Log(LOGERROR, "%s::%s - set decoder format failed\n", CLASSNAME, __func__);
    return false;
  }
    
  // allocate input buffers
  int buffers = CLinuxV4l2::RequestBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, 1);

  if(buffers != 1)
  {
    CLog::Log(LOGERROR, "%s::%s - allocate input buffers\n", CLASSNAME, __func__);
    return false;
  }

  struct v4l2_buffer buf;
  struct v4l2_plane planes[V4L2_NUM_MAX_PLANES];
  memset(&buf, 0, sizeof(struct v4l2_buffer));
  buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = 0;
  buf.m.planes = planes;
  buf.length = NUM_INPUT_PLANES;

  ret = ioctl(m_iDecoderHandle, VIDIOC_QUERYBUF, &buf);
  if (ret)
  {
    CLog::Log(LOGERROR, "%s::%s - buffer query failed\n", CLASSNAME, __func__);
    return false;
  }

  m_v4l2StreamBuffer.iSize[0] = buf.m.planes[0].length;
  m_v4l2StreamBuffer.cPlane[0] = mmap(NULL, buf.m.planes[0].length, PROT_READ | PROT_WRITE,
                  MAP_SHARED, m_iDecoderHandle, buf.m.planes[0].m.mem_offset);
  m_v4l2StreamBuffer.iNumPlanes = NUM_INPUT_PLANES;

  if(m_v4l2StreamBuffer.cPlane[0] == MAP_FAILED)
  {
    CLog::Log(LOGERROR, "%s::%s - failed to map stream buffer\n", CLASSNAME, __func__);
    return false;
  }

  // send header/extradata
  if(extraSize && (extraSize < m_v4l2StreamBuffer.iSize[0]))
  {
    memcpy((uint8_t *)m_v4l2StreamBuffer.cPlane[0], extraData, extraSize);
    m_v4l2StreamBuffer.iOffset[0] = extraSize;

    struct v4l2_plane planes[V4L2_NUM_MAX_PLANES];
    struct v4l2_buffer qbuf;
    int ret;

    memset(&qbuf, 0, sizeof(struct v4l2_buffer));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = 0;
    qbuf.m.planes = planes;
    qbuf.length = NUM_INPUT_PLANES;
    qbuf.m.planes[0].bytesused = extraSize;

    ret = ioctl(m_iDecoderHandle, VIDIOC_QBUF, &qbuf);

    if (ret)
    {
      CLog::Log(LOGERROR, "%s::%s - failed to map stream buffer\n", CLASSNAME, __func__);
      return false;
    }

    // Processing the header requires running streamon
    // on OUTPUT queue
    if (!CLinuxV4l2::StreamOn(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON))
    {
      CLog::Log(LOGERROR, "%s::%s - failed enable input stream\n", CLASSNAME, __func__);
      return false;
    }
  }
  else
  {
    CLog::Log(LOGERROR, "%s::%s - no stream header\n", CLASSNAME, __func__);
    return false;
  }

  memset(&fmt, 0, sizeof(struct v4l2_format));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ret = ioctl(m_iDecoderHandle, VIDIOC_G_FMT, &fmt);
  if (ret)
  {
    CLog::Log(LOGERROR, "%s::%s - read process header\n", CLASSNAME, __func__);
    return false;
  }

  m_iDecodedWidth = fmt.fmt.pix_mp.width;
  m_iDecodedHeight = fmt.fmt.pix_mp.height;

  struct v4l2_crop crop;
  memset(&crop, 0, sizeof(struct v4l2_crop));
  crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ret = ioctl(m_iDecoderHandle, VIDIOC_G_CROP, &crop);
  if (ret)
  {
    CLog::Log(LOGERROR, "%s::%s - get cropping\n", CLASSNAME, __func__);
    return false;
  }

  printf("crop.c.left %d crop.c.top %d crop.c.width %d crop.c.height %d\n",
      crop.c.left, crop.c.top, crop.c.width, crop.c.height);

//  m_iDecodedWidth = crop.c.width;
//  m_iDecodedHeight = crop.c.height;

  printf("m_iDecodedWidth %d m_iDecodedHeight %d\n", 
      m_iDecodedWidth, m_iDecodedHeight);

  if(m_iDecodedWidth == 0 || m_iDecodedHeight == 0)
  {
    CLog::Log(LOGERROR, "%s::%s - invalid buffer size\n", CLASSNAME, __func__);
    return false;
  }

  if(!AllocateDecodeOutputBuffers())
  {
    CLog::Log(LOGERROR, "%s::%s - allocate output buffers\n", CLASSNAME, __func__);
    return false;
  }

#ifdef USE_FIMC
  m_res_info =  CDisplaySettings::Get().GetResolutionInfo(g_graphicsContext.GetVideoResolution());

  memset(&fmt, 0, sizeof(struct v4l2_format));
  fmt.fmt.pix_mp.pixelformat  = V4L2_PIX_FMT_NV12MT;
  fmt.fmt.pix_mp.num_planes   = 2;
  fmt.fmt.pix_mp.plane_fmt[0].bytesperline  = m_iDecodedWidth;
  fmt.fmt.pix_mp.plane_fmt[0].sizeimage     = m_iDecodedWidth * m_iDecodedHeight;
  fmt.fmt.pix_mp.plane_fmt[1].bytesperline  = m_iDecodedWidth;
  fmt.fmt.pix_mp.plane_fmt[1].sizeimage     = m_iDecodedWidth * (m_iDecodedHeight >> 1);
  fmt.type                    = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  fmt.fmt.pix_mp.field        = V4L2_FIELD_ANY;
  fmt.fmt.pix_mp.width        = m_iDecodedWidth;
  fmt.fmt.pix_mp.height       = m_iDecodedHeight;

  if (ioctl(m_iConverterHandle, VIDIOC_S_FMT, &fmt) == -1)
  {
    CLog::Log(LOGERROR, "%s::%s - setting converter input format\n", CLASSNAME, __func__);
    return false;
  }

  // round to an even multiple of 16
  m_res_info.iScreenHeight  = ((m_res_info.iScreenHeight/16) + (m_res_info.iScreenHeight/16)%2) * 16;
  printf("m_res_info.iScreenWidth %d m_res_info.iScreenHeight %d\n",
      m_res_info.iScreenWidth, m_res_info.iScreenHeight);


  memset(&fmt, 0, sizeof(struct v4l2_format));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  fmt.fmt.pix_mp.field        = V4L2_FIELD_ANY;
  fmt.fmt.pix_mp.pixelformat  = V4L2_PIX_FMT_YUV420M;
  fmt.fmt.pix_mp.num_planes = 3;

  fmt.fmt.pix_mp.width    = m_iDecodedWidth;
  fmt.fmt.pix_mp.height   = m_iDecodedHeight;

  fmt.fmt.pix_mp.plane_fmt[0].bytesperline  = m_iDecodedWidth;
  fmt.fmt.pix_mp.plane_fmt[0].sizeimage     = m_iDecodedWidth * m_iDecodedHeight;
  for (int i=1; i<3; i++)
  {
    fmt.fmt.pix_mp.plane_fmt[i].bytesperline  = m_iDecodedWidth >> 1;
    fmt.fmt.pix_mp.plane_fmt[i].sizeimage     = (m_iDecodedWidth >> 1) * (m_iDecodedHeight >> 1);
  }

  if (ioctl(m_iConverterHandle, VIDIOC_S_FMT, &fmt) == -1) 
  {
    CLog::Log(LOGERROR, "%s::%s - setting converter capture format\n", CLASSNAME, __func__);
    return false;
  }

  memset(&crop, 0, sizeof(struct v4l2_crop));
  crop.c.left   = 0;
  crop.c.top    = 0;
  crop.c.width  = m_iDecodedWidth;
  crop.c.height = m_iDecodedHeight;
  crop.type     = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

  if (ioctl(m_iConverterHandle, VIDIOC_S_CROP, &crop) == -1)
  {
    CLog::Log(LOGERROR, "%s::%s - setting converter input crop\n", CLASSNAME, __func__);
    return false;
  }

//  crop.c.width  = m_res_info.iScreenWidth;
//  crop.c.height = m_res_info.iScreenHeight;

  crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

  if (ioctl(m_iConverterHandle, VIDIOC_S_CROP, &crop) == -1)
  {
    CLog::Log(LOGERROR, "%s::%s - setting converter output crop\n", CLASSNAME, __func__);
    return false;
  }
  
  buffers = CLinuxV4l2::RequestBuffer(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 
      V4L2_MEMORY_USERPTR, NUM_CONVERT_BUFFERS);
  if(buffers != NUM_CONVERT_BUFFERS)
  {
    CLog::Log(LOGERROR, "%s::%s - allocate converter input buffers\n", CLASSNAME, __func__);
    return false;
  }

  buffers = CLinuxV4l2::RequestBuffer(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 
      V4L2_MEMORY_MMAP, NUM_CONVERT_BUFFERS);
  if(buffers != NUM_CONVERT_BUFFERS)
  {
    CLog::Log(LOGERROR, "%s::%s - allocate converter output buffers\n", CLASSNAME, __func__);
    return false;
  }

  // Allocate buffers
  m_v4l2ConvertBuffer = (V4L2Buffer *)calloc(NUM_CONVERT_BUFFERS, sizeof(V4L2Buffer));
  if(!m_v4l2ConvertBuffer)
  {
    CLog::Log(LOGERROR, "%s::%s - cannot allocate converter buffers\n", CLASSNAME, __func__);
    return false;
  }

  // map converter capture buffer
  if(!CLinuxV4l2::MmapBuffers(m_iConverterHandle, NUM_CONVERT_BUFFERS, m_v4l2ConvertBuffer, 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, false))
  {
    CLog::Log(LOGERROR, "%s::%s - cannot mmap converter capture buffers\n", CLASSNAME, __func__);
    return false;
  }
  m_bMFCStartConverter = true;
#endif

  if(!CLinuxV4l2::StreamOn(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMON))
  {
    CLog::Log(LOGERROR, "%s::%s - failed enable output stream\n", CLASSNAME, __func__);
    return false;
  }
  
  CLog::Log(LOGDEBUG, "%s::%s - success : %dx%d buffers %d\n", CLASSNAME, __func__,
      m_iDecodedWidth, m_iDecodedHeight, m_iMinBuffers);
  printf("%s::%s - success : %dx%d buffers %d\n", CLASSNAME, __func__,
      m_iDecodedWidth, m_iDecodedHeight, m_iMinBuffers);

  return true;
}

void CDVDVideoCodecMFC::Dispose()
{
  int i, j;

  if(m_iDecoderHandle >= 0)
  {
    /*
    if(m_v4l2Buffers)
    {
      for(int i = 0; i < m_iMinBuffers; i++)
      {
        V4L2Buffer *buffer = &m_v4l2Buffers[i];

        if(buffer)
        {
          CDVDVideoCodecMFC::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
            V4L2_MEMORY_MMAP, buffer->iNumPlanes, i, buffer);
        }
      }
    }
    */

    CLinuxV4l2::StreamOn(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMOFF);
    CLinuxV4l2::StreamOn(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMOFF);
  }

  if(m_v4l2StreamBuffer.cPlane[0] && m_v4l2StreamBuffer.cPlane[0] != MAP_FAILED)
  {
    /*
    CDVDVideoCodecMFC::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
      V4L2_MEMORY_MMAP, m_v4l2StreamBuffer.iNumPlanes, 0, &m_v4l2StreamBuffer);
    */
    munmap(m_v4l2StreamBuffer.cPlane[0], m_v4l2StreamBuffer.iSize[0]);
  }
  memset(&m_v4l2StreamBuffer, 0, sizeof(V4L2Buffer));

  m_v4l2Buffers = CLinuxV4l2::FreeBuffers(m_iMinBuffers, m_v4l2Buffers);

  if(m_iDecoderHandle >= 0)
    close(m_iDecoderHandle);

#ifdef USE_FIMC
  if(m_iConverterHandle >= 0)
  {
    CLinuxV4l2::StreamOn(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMOFF);
    CLinuxV4l2::StreamOn(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMOFF);
  }

  if(m_v4l2ConvertBuffer)
  {
    m_v4l2ConvertBuffer = CLinuxV4l2::FreeBuffers(NUM_OUTPUT_BUFFERS, m_v4l2ConvertBuffer);
  }

  if(m_iConverterHandle >= 0)
  {
    close(m_iConverterHandle);
    m_iConverterHandle = -1;
  }
#endif

  while(!m_pts.empty())
    m_pts.pop();

  m_iDecodedWidth = 0;
  m_iDecodedHeight = 0;
  m_iDecoderHandle = -1;
  m_bVideoConvert = false;
  m_iMinBuffers = 0;
  m_iBufferIndex = -1;
  m_decode_buffer = NULL;
  m_bDropPictures = false;

  memset(&m_videoBuffer, 0, sizeof(DVDVideoPicture));
}

void CDVDVideoCodecMFC::SetDropState(bool bDrop)
{
  m_bDropPictures = bDrop;

  if (m_bDropPictures)
  {
    if(m_iBufferIndex>=0)
    {
      V4L2Buffer *buffer = &m_v4l2Buffers[m_iBufferIndex];
      if(buffer && !buffer->bQueue)
      {
        int ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                                          V4L2_NUM_MAX_PLANES, buffer->iIndex, buffer);
        if (ret < 0)
        {
          CLog::Log(LOGERROR, "%s::%s - queue output buffer\n", CLASSNAME, __func__);
        }
        buffer->bQueue = true;
      }

      if(m_pts.size())
        m_pts.pop();
      m_iBufferIndex = -1;
    }
  }
}

int CDVDVideoCodecMFC::Decode(BYTE* pData, int iSize, double dts, double pts)
{
  int ret = 0;
  int retStatus = VC_BUFFER;

  if(pData)
  {
    int demuxer_bytes = iSize;
    uint8_t *demuxer_content = pData;

    ret = CLinuxV4l2::PollOutput(m_iDecoderHandle, 25);
    if(ret == V4L2_ERROR)
    {
      return VC_ERROR;
    }
    else if (ret == V4L2_READY)
    {
      if(m_bVideoConvert)
      {
        m_converter.Convert(demuxer_content, demuxer_bytes);
        demuxer_bytes = m_converter.GetConvertSize();
        demuxer_content = m_converter.GetConvertBuffer();
      }

      if(demuxer_bytes < m_v4l2StreamBuffer.iSize[0])
      {
        int index = CLinuxV4l2::DequeueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP,
                                              m_v4l2StreamBuffer.iNumPlanes);
        if (index < 0)
        {
          CLog::Log(LOGERROR, "%s::%s - dequeue input buffer\n", CLASSNAME, __func__);
          return VC_ERROR;
        }

        fast_memcpy((uint8_t *)m_v4l2StreamBuffer.cPlane[0], demuxer_content, demuxer_bytes);
        m_v4l2StreamBuffer.iBytesUsed[0] = demuxer_bytes;

        ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP,
                                      m_v4l2StreamBuffer.iNumPlanes, index, &m_v4l2StreamBuffer);
        if (ret < 0)
        {
          CLog::Log(LOGERROR, "%s::%s - queue input buffer\n", CLASSNAME, __func__);
          return VC_ERROR;
        }
        m_pts.push(pts);
      }
      else
      {
        CLog::Log(LOGERROR, "%s::%s - packet to big for streambuffer\n", CLASSNAME, __func__);
      }
    }
  }


  {
    // dequeue decoded frame
    int index = CLinuxV4l2::DequeueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                  V4L2_NUM_MAX_PLANES);
    if (index < 0) 
    {
      if (index == -EAGAIN)
        return retStatus;
      CLog::Log(LOGERROR, "%s::%s - dequeue output buffer\n", CLASSNAME, __func__);
      return VC_ERROR;
    }

    V4L2Buffer *buffer = &m_v4l2Buffers[index];
    if(!buffer)
    {
      CLog::Log(LOGERROR, "%s::%s - picture buffer index\n", CLASSNAME, __func__);
      return VC_ERROR;
    }

    buffer->bQueue = false;
    m_iBufferIndex = index;

    retStatus |= VC_PICTURE;
  }

  return retStatus;
}

void CDVDVideoCodecMFC::Reset()
{
}

bool CDVDVideoCodecMFC::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
#define INT_ROUND(x, y) ((x % y) > 0 ? (int(x/y)+1)*y : (int(x/y))*y )
#ifdef USE_FIMC
  V4L2Buffer *convert_buffer    = NULL;
  m_videoBuffer.format          = RENDER_FMT_YUV420P;
#else
  m_videoBuffer.format          = RENDER_FMT_NV12MT;
#endif

  if(m_pts.size())
  {
//    m_videoBuffer.pts = m_pts.front();
    m_videoBuffer.pts           = DVD_NOPTS_VALUE;
    m_videoBuffer.dts           = DVD_NOPTS_VALUE;

    m_pts.pop();
  }
  else
  {
    printf("no pts value\n");
    m_videoBuffer.pts           = DVD_NOPTS_VALUE;
    m_videoBuffer.dts           = DVD_NOPTS_VALUE;
  }

  m_videoBuffer.iFlags        = DVP_FLAG_ALLOCATED;

  //m_videoBuffer.mfcBuffer     = NULL;
  //if(m_iBufferIndex >= 0 && m_iBufferIndex < m_iMinBuffers)
  if(m_iBufferIndex>=0)
  {
    m_decode_buffer          = &m_v4l2Buffers[m_iBufferIndex];
    int ret = 0;

    if (m_bDropPictures)
      m_videoBuffer.iFlags      |= DVP_FLAG_DROPPED;
    else
    {
#ifdef USE_FIMC
      // convert
      ret = CLinuxV4l2::QueueBuffer(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                                    V4L2_MEMORY_USERPTR, m_decode_buffer->iNumPlanes, 0, m_decode_buffer);
      if (ret < 0)
      {
        CLog::Log(LOGERROR, "%s::%s - queue converter input buffer\n", CLASSNAME, __func__);
        return false;
      }

      convert_buffer = &m_v4l2ConvertBuffer[0];
      if(!convert_buffer)
      {
        CLog::Log(LOGERROR, "%s::%s - convert buffer index\n", CLASSNAME, __func__);
        return false;
      }

      ret = CLinuxV4l2::QueueBuffer(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                                    V4L2_MEMORY_MMAP, convert_buffer->iNumPlanes, 0, convert_buffer);
      if (ret < 0)
      {
        CLog::Log(LOGERROR, "%s::%s - queue converter output buffer\n", CLASSNAME, __func__);
        return false;
      }

      if(m_bMFCStartConverter)
      {
        CLinuxV4l2::StreamOn(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMON);
        CLinuxV4l2::StreamOn(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON);
        m_bMFCStartConverter = false;
      }

      ret = CLinuxV4l2::DequeueBuffer(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                                      V4L2_MEMORY_USERPTR, decode_buffer->iNumPlanes);
      if (ret < 0)
      {
        CLog::Log(LOGERROR, "%s::%s -  dequeue converter output buffer\n", CLASSNAME, __func__);
        return false;
      }

      ret = CLinuxV4l2::DequeueBuffer(m_iConverterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                                      V4L2_MEMORY_MMAP, convert_buffer->iNumPlanes);
      if(ret < 0)
      {
        CLog::Log(LOGERROR, "%s::%s -  dequeue converter capture buffer\n", CLASSNAME, __func__);
        return false;
      }
#endif
    }


  }
  else
  {
    m_videoBuffer.iFlags        &= DVP_FLAG_ALLOCATED;
    m_videoBuffer.iFlags        |= DVP_FLAG_DROPPED;
  }

  //m_videoBuffer.iFlags          |= m_bDropPictures ? DVP_FLAG_DROPPED : 0;

  m_videoBuffer.color_range     = 0;
  m_videoBuffer.color_matrix    = 4;

  m_videoBuffer.iDisplayWidth   = m_iDecodedWidth;
  m_videoBuffer.iDisplayHeight  = m_iDecodedHeight;
  m_videoBuffer.iWidth          = m_iDecodedWidth;
  m_videoBuffer.iHeight         = m_iDecodedHeight;

  m_videoBuffer.data[0] = 0;
  m_videoBuffer.data[1] = 0;
  m_videoBuffer.data[2] = 0;
  m_videoBuffer.data[3] = 0;

#ifdef USE_FIMC
  m_videoBuffer.iLineSize[0] = m_iDecodedWidth;
  m_videoBuffer.iLineSize[1] = m_iDecodedWidth >> 1;
  m_videoBuffer.iLineSize[2] = m_iDecodedWidth >> 1;
  m_videoBuffer.iLineSize[3] = 0;
  if (!m_bDropPictures)
  {
    m_videoBuffer.data[0] = (BYTE*)convert_buffer->cPlane[0];
    m_videoBuffer.data[1] = (BYTE*)convert_buffer->cPlane[1];
    m_videoBuffer.data[2] = (BYTE*)convert_buffer->cPlane[2];
  }
#else
  m_videoBuffer.iLineSize[0] = INT_ROUND(m_iDecodedWidth, 128);
  m_videoBuffer.iLineSize[1] = INT_ROUND(m_iDecodedWidth, 128);
  m_videoBuffer.iLineSize[2] = 0;
  m_videoBuffer.iLineSize[3] = 0;
  if (!m_bDropPictures)
  {
    m_videoBuffer.data[0] = (BYTE*)m_decode_buffer->cPlane[0];
    m_videoBuffer.data[1] = (BYTE*)m_decode_buffer->cPlane[1];
  }
#endif

  *pDvdVideoPicture = m_videoBuffer;

  return true;
}

bool CDVDVideoCodecMFC::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if (m_decode_buffer && !m_decode_buffer->bQueue)
  {
    int ret = 0;

    ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                                  V4L2_NUM_MAX_PLANES, m_decode_buffer->iIndex, m_decode_buffer);

    if (ret < 0)
    {
      CLog::Log(LOGERROR, "%s::%s - queue output buffer\n", CLASSNAME, __func__);
      return false;
    }
    m_decode_buffer->bQueue = true;
    m_decode_buffer = NULL;
  }

  return CDVDVideoCodec::ClearPicture(pDvdVideoPicture);
}

