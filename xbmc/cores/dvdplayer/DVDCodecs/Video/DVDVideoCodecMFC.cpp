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

#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"

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

  memset(&m_v4l2StreamBuffer, 0, sizeof(V4L2Buffer));
  memset(&m_videoBuffer, 0, sizeof(DVDVideoPicture));
}

CDVDVideoCodecMFC::~CDVDVideoCodecMFC()
{
  Dispose();
}

bool CDVDVideoCodecMFC::AllocateOutputBuffers()
{
  struct v4l2_control ctrl;
  int ret;
  int i, j;

  if(m_iDecoderHandle < 0)
    return false;

  ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
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

  m_iDecoderHandle = open("/dev/video8", O_RDWR, 0);
  if(m_iDecoderHandle < 0)
  {
    CLog::Log(LOGERROR, "%s::%s - open decoder device /dev/video8", CLASSNAME, __func__);
    return false;
  }

  ret = ioctl(m_iDecoderHandle, VIDIOC_QUERYCAP, &cap);
  if (ret != 0)
  {
    CLog::Log(LOGERROR, "%s::%s - query decoder caps", CLASSNAME, __func__);
    return false;
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
    !(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) ||
    !(cap.capabilities & V4L2_CAP_STREAMING))
  {
    CLog::Log(LOGERROR, "%s::%s - insufficient decoder caps\n", CLASSNAME, __func__);
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

  m_iDecodedWidth = crop.c.width;
  m_iDecodedHeight = crop.c.height;

  printf("m_iDecodedWidth %d m_iDecodedHeight %d\n", 
      m_iDecodedWidth, m_iDecodedHeight);

  if(m_iDecodedWidth == 0 || m_iDecodedHeight == 0)
  {
    CLog::Log(LOGERROR, "%s::%s - invalid buffer size\n", CLASSNAME, __func__);
    return false;
  }

  if(!AllocateOutputBuffers())
  {
    CLog::Log(LOGERROR, "%s::%s - allocate output buffers\n", CLASSNAME, __func__);
    return false;
  }

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

  while (!m_MFCDemuxPacket.empty())
  {
    MFCDemuxPacket demux_packet = m_MFCDemuxPacket.front();
    delete demux_packet.buff;
    m_MFCDemuxPacket.pop();
  }

  while (!m_MFCDecodeTimeStamp.empty())
  {
    MFCDemuxPacket *demux_packet = m_MFCDecodeTimeStamp.front();
    free(demux_packet);
    m_MFCDecodeTimeStamp.pop_front();
  }

  while (!m_MFCFrameTimeStamp.empty())
  {
    MFCDemuxPacket *demux_packet = m_MFCFrameTimeStamp.front();
    free(demux_packet);
    m_MFCFrameTimeStamp.pop();
  }

  while(!m_pts.empty())
    m_pts.pop();
  while(!m_dts.empty())
    m_dts.pop();

  while(!m_index.empty())
    m_index.pop();

  m_iDecodedWidth = 0;
  m_iDecodedHeight = 0;
  m_iDecoderHandle = -1;
  m_bVideoConvert = false;
  m_iMinBuffers = 0;
  m_iBufferIndex = -1;
  m_bDropPictures = false;

  memset(&m_videoBuffer, 0, sizeof(DVDVideoPicture));
}

void CDVDVideoCodecMFC::SetDropState(bool bDrop)
{
  m_bDropPictures = bDrop;

  if(!m_index.empty())
  {
    int index = m_index.front();

    V4L2Buffer *buffer = &m_v4l2Buffers[index];
    if(buffer && !buffer->bQueue)
    {
      int ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                    V4L2_NUM_MAX_PLANES, index, buffer);
      if (ret < 0)
      {
        CLog::Log(LOGERROR, "%s::%s - queue output buffer\n", CLASSNAME, __func__);
      }
    }

    m_index.pop();
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

    MFCDemuxPacket demux_packet;
    demux_packet.dts = dts;
    demux_packet.pts = pts;

    demux_packet.size = demuxer_bytes;
    demux_packet.buff = new uint8_t[demuxer_bytes];
    memcpy(demux_packet.buff, demuxer_content, demuxer_bytes);

    m_MFCDemuxPacket.push(demux_packet);
  }

  ret = CLinuxV4l2::PollOutput(m_iDecoderHandle, 25);
  if(ret == V4L2_ERROR)
  {
    return VC_ERROR;
  }
  else if (ret == V4L2_READY)
  {
    MFCDemuxPacket demux_packet = m_MFCDemuxPacket.front();
    m_MFCDemuxPacket.pop();

    int demuxer_bytes = 0;
    uint8_t *demuxer_content = NULL;

    if(m_bVideoConvert)
    {
      m_converter.Convert(demux_packet.buff, demux_packet.size);
      demuxer_bytes = m_converter.GetConvertSize();
      demuxer_content = m_converter.GetConvertBuffer();
    }
    else
    {
      demuxer_bytes = demux_packet.size;
      demuxer_content = demux_packet.buff;
    }

    if(demuxer_bytes < m_v4l2StreamBuffer.iSize[0])
    {
      struct v4l2_control ctrl;

      MFCDemuxPacket *tmp_packet = (MFCDemuxPacket *)malloc(sizeof(struct MFCDemuxPacket));
      memset(tmp_packet, 0, sizeof(struct MFCDemuxPacket));

      tmp_packet->pts = demux_packet.pts;
      tmp_packet->dts = demux_packet.dts;

      m_pts.push(demux_packet.pts);
      m_dts.push(demux_packet.dts);

      ctrl.id = V4L2_CID_CODEC_FRAME_TAG;
      ctrl.value = (unsigned int)tmp_packet;

      int index = CLinuxV4l2::DequeueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP,
                    m_v4l2StreamBuffer.iNumPlanes);
      if (index < 0) 
      {
        CLog::Log(LOGERROR, "%s::%s - dequeue input buffer\n", CLASSNAME, __func__);
        return VC_ERROR;
      }

      memcpy((uint8_t *)m_v4l2StreamBuffer.cPlane[0], demuxer_content, demuxer_bytes);
      m_v4l2StreamBuffer.iBytesUsed[0] = demuxer_bytes;

      int ret = ioctl(m_iDecoderHandle, VIDIOC_S_CTRL, &ctrl);
      if(ret < 0)
      {
        printf("%s::%s - attach timestamp\n", CLASSNAME, __func__);
        CLog::Log(LOGERROR, "%s::%s - attach timestamp\n", CLASSNAME, __func__);
        free(tmp_packet);
      }
      else
      {
        m_MFCDecodeTimeStamp.push_back(tmp_packet);
      }

      ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP,
                    m_v4l2StreamBuffer.iNumPlanes, index, &m_v4l2StreamBuffer);
      if (ret < 0)
      {
        CLog::Log(LOGERROR, "%s::%s - queue input buffer\n", CLASSNAME, __func__);
        return VC_ERROR;
      }
    }
    else
    {
      CLog::Log(LOGERROR, "%s::%s - packet to big for streambuffer\n", CLASSNAME, __func__);
    }

    delete demux_packet.buff;
  }

  ret = CLinuxV4l2::PollInput(m_iDecoderHandle, 25);
  if (ret == V4L2_ERROR)
  {
    CLog::Log(LOGERROR, "%s::%s - polling output\n", CLASSNAME, __func__);
    return VC_ERROR;
  }
  else if( ret == V4L2_READY)
  {
    // dequeue decoded frame
    int index = CLinuxV4l2::DequeueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                  V4L2_NUM_MAX_PLANES);
    if (index < 0) 
    {
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

    struct v4l2_control ctrl;

    ctrl.id = V4L2_CID_CODEC_FRAME_TAG;
    ctrl.value = 0;

    int ret = ioctl(m_iDecoderHandle, VIDIOC_G_CTRL, &ctrl);
    if(ret < 0)
    {
      printf("%s::%s - get timestamp\n", CLASSNAME, __func__);
      CLog::Log(LOGERROR, "%s::%s - get timestamp\n", CLASSNAME, __func__);
    }
    else if(ctrl.value)
    {
      MFCDemuxPacket *search_packet = (MFCDemuxPacket *)ctrl.value;

      for(std::list<MFCDemuxPacket *>::iterator it = m_MFCDecodeTimeStamp.begin(); 
          it != m_MFCDecodeTimeStamp.end(); ++it)
      {
        if(*it == search_packet)
        {
          m_MFCFrameTimeStamp.push(*it);
          m_MFCDecodeTimeStamp.erase(it);
          break;
        }
      }

    }

    // queue decoded frame
    /*
    ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                  V4L2_NUM_MAX_PLANES, index, buffer);
    if (ret < 0)
    {
      CLog::Log(LOGERROR, "%s::%s - queue output buffer\n", CLASSNAME, __func__);
      return VC_ERROR;
    }
    */

    m_index.push(index);

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
  m_videoBuffer.format          = RENDER_FMT_MFC; //RENDER_FMT_YUV420P;

  if(m_MFCFrameTimeStamp.size())
  {
    MFCDemuxPacket *demux_packet = m_MFCFrameTimeStamp.front();
    m_MFCFrameTimeStamp.pop();

    /*
    m_videoBuffer.pts = demux_packet->pts;
    m_videoBuffer.dts = demux_packet->dts;
    */

    free(demux_packet);

    //printf("%s::%s pts %f dts %f\n", CLASSNAME, __func__, pDvdVideoPicture->pts, pDvdVideoPicture->dts);
  }
  if(m_pts.size())
  {
    m_videoBuffer.pts = m_pts.front();
    m_videoBuffer.dts = m_dts.front();

    m_pts.pop();
    m_dts.pop();
  }
  else
  {
    printf("no pts value\n");
    m_videoBuffer.pts           = DVD_NOPTS_VALUE;
    m_videoBuffer.dts           = DVD_NOPTS_VALUE;
  }

  m_videoBuffer.iFlags        = DVP_FLAG_ALLOCATED;

  m_videoBuffer.mfcBuffer     = NULL;
  //if(m_iBufferIndex >= 0 && m_iBufferIndex < m_iMinBuffers)
  if(!m_index.empty())
  {
    int index = m_index.front();
    //V4L2Buffer *buffer          = &m_v4l2Buffers[m_iBufferIndex];
    V4L2Buffer *buffer          = &m_v4l2Buffers[index];

    m_index.pop();

    m_videoBuffer.mfcBuffer     = buffer;
    buffer->iIndex              = index;
    buffer->bQueue              = true;

    int ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                V4L2_NUM_MAX_PLANES, index, buffer);
    if (ret < 0)
    {
      CLog::Log(LOGERROR, "%s::%s - queue output buffer\n", CLASSNAME, __func__);
      m_videoBuffer.iFlags      |= DVP_FLAG_DROPPED;
      m_videoBuffer.iFlags      &= DVP_FLAG_ALLOCATED;
      return false;
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

  *pDvdVideoPicture = m_videoBuffer;

  return true;
}

bool CDVDVideoCodecMFC::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if(pDvdVideoPicture->mfcBuffer)
  {
    V4L2Buffer *buffer = pDvdVideoPicture->mfcBuffer;

    if(buffer && !buffer->bQueue)
    {
      printf("%s::%s - index %d m_bDropPictures %d\n", CLASSNAME, __func__, buffer->iIndex, m_bDropPictures);
      int ret = CLinuxV4l2::QueueBuffer(m_iDecoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP,
                  V4L2_NUM_MAX_PLANES, buffer->iIndex, buffer);
      if (ret < 0)
      {
        CLog::Log(LOGERROR, "%s::%s - queue output buffer\n", CLASSNAME, __func__);
      }
    }
    pDvdVideoPicture->mfcBuffer = NULL;
  }

  return CDVDVideoCodec::ClearPicture(pDvdVideoPicture);
}

