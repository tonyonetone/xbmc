/*
 *      Copyright (C) 2010-2013 Team XBMC
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

#include "DVDAudioCodecPassthroughRaw.h"
#include "DVDCodecs/DVDCodecs.h"
#include "utils/log.h"

#include <algorithm>

#include "cores/AudioEngine/AEFactory.h"

static enum AEChannel OutputMaps[2][9] = {
  {AE_CH_RAW, AE_CH_RAW, AE_CH_NULL},
  {AE_CH_RAW, AE_CH_RAW, AE_CH_RAW, AE_CH_RAW, AE_CH_RAW, AE_CH_RAW, AE_CH_RAW, AE_CH_RAW, AE_CH_NULL}
};

#define AC3_DIVISOR (1536 * 1000)
#define DTS_DIVISOR (512 * 1000)
#define CONSTANT_BUFFER_SIZE 5120

CDVDAudioCodecPassthroughRaw::CDVDAudioCodecPassthroughRaw(void) :
  m_buffer    (NULL),
  m_bufferSize(0),
  m_bufferUsed(0),
  m_sampleRate(0)
{
}

CDVDAudioCodecPassthroughRaw::~CDVDAudioCodecPassthroughRaw(void)
{
  Dispose();
}

bool CDVDAudioCodecPassthroughRaw::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  m_hints = hints;

  bool bSupportsAC3Out    = CAEFactory::SupportsRaw(AE_FMT_AC3_RAW, hints.samplerate);
  bool bSupportsDTSOut    = CAEFactory::SupportsRaw(AE_FMT_DTS_RAW, hints.samplerate);
  bool bSupportsEAC3Out   = CAEFactory::SupportsRaw(AE_FMT_EAC3_RAW, hints.samplerate);
  bool bSupportsTrueHDOut = CAEFactory::SupportsRaw(AE_FMT_TRUEHD_RAW, hints.samplerate);
  bool bSupportsDTSHDOut  = CAEFactory::SupportsRaw(AE_FMT_DTSHD_RAW, hints.samplerate);

  if ((hints.codec == AV_CODEC_ID_AC3 && bSupportsAC3Out) ||
      (hints.codec == AV_CODEC_ID_EAC3 && bSupportsEAC3Out) ||
      ((hints.codec == AV_CODEC_ID_DTS && hints.channels == 2) && bSupportsDTSOut) ||
      (hints.codec == AV_CODEC_ID_TRUEHD && bSupportsTrueHDOut) ||
      ((hints.codec == AV_CODEC_ID_DTS && hints.channels > 2) && bSupportsDTSHDOut) )
  {
    return true;
  }

  return false;
}

void CDVDAudioCodecPassthroughRaw::GetData(DVDAudioFrame &frame)
{
  frame.nb_frames = 0;
  frame.data_format           = GetDataFormat();
  frame.channel_count         = GetChannels();
  frame.bits_per_sample       = 8;
  frame.framesize             = 1;
  frame.nb_frames             = GetData(frame.data)/frame.framesize;
  frame.channel_layout        = GetChannelMap();
  frame.channel_count         = GetChannels();
  frame.planes                = 1;
  frame.encoded_channel_count = GetEncodedChannels();
  frame.encoded_sample_rate   = GetEncodedSampleRate();
  frame.passthrough           = NeedPassthrough();
  frame.pts                   = DVD_NOPTS_VALUE;

  int64_t unscaledbitrate = (frame.nb_frames*frame.framesize) * frame.bits_per_sample * frame.encoded_sample_rate;
  switch(m_hints.codec)
  {
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
    case AV_CODEC_ID_TRUEHD:
    {
      int bitrate = (unscaledbitrate + AC3_DIVISOR / 2) / AC3_DIVISOR;
      m_sampleRate = bitrate * 1000 / frame.bits_per_sample;
      break;
    }

    case AV_CODEC_ID_DTS:
      if (m_hints.channels > 6)
      {
        int bitrate = (unscaledbitrate + DTS_DIVISOR / 2) / DTS_DIVISOR;
        m_sampleRate = bitrate * 1000 / frame.bits_per_sample;
      }
      else
      {
        int bitrate = (unscaledbitrate + DTS_DIVISOR / 2) / DTS_DIVISOR;
        m_sampleRate = bitrate * 1000 / frame.bits_per_sample;
      }
      break;

    default:
      m_sampleRate = GetSampleRate();
      break;
  }
  frame.sample_rate           = GetSampleRate();

  // compute duration.
  if (frame.sample_rate)
    frame.duration = ((double)frame.nb_frames * DVD_TIME_BASE) / frame.sample_rate;
  else
    frame.duration = 0.0;
}

int CDVDAudioCodecPassthroughRaw::GetSampleRate()
{
  if (m_sampleRate)
    return m_sampleRate;
  else
    return GetEncodedSampleRate();
}

int CDVDAudioCodecPassthroughRaw::GetEncodedSampleRate()
{
  return m_hints.samplerate;
}

enum AEDataFormat CDVDAudioCodecPassthroughRaw::GetDataFormat()
{
  switch(m_hints.codec)
  {
    case AV_CODEC_ID_AC3:
      return AE_FMT_AC3_RAW;

    case AV_CODEC_ID_DTS:
      if (m_hints.channels > 6)
        return AE_FMT_DTSHD_RAW;
      else
        return AE_FMT_DTS_RAW;

    case AV_CODEC_ID_EAC3:
      return AE_FMT_EAC3_RAW;

    case AV_CODEC_ID_TRUEHD:
      return AE_FMT_TRUEHD_RAW;

    default:
      return AE_FMT_INVALID; //Unknown stream type
  }
}

int CDVDAudioCodecPassthroughRaw::GetChannels()
{
  switch(m_hints.codec)
  {
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      return 2;

    case AV_CODEC_ID_DTS:
      if (m_hints.channels > 6)
        return 8;
      else
        return 2;


    case AV_CODEC_ID_TRUEHD:
      return 8;

    default:
      return 0; //Unknown stream type
  }
}

int CDVDAudioCodecPassthroughRaw::GetEncodedChannels()
{
  return m_hints.channels;
}

CAEChannelInfo CDVDAudioCodecPassthroughRaw::GetChannelMap()
{
  if (m_hints.channels == 2)
    return CAEChannelInfo(OutputMaps[0]);
  else
    return CAEChannelInfo(OutputMaps[1]);
}

void CDVDAudioCodecPassthroughRaw::Dispose()
{
  if (m_buffer)
  {
    delete[] m_buffer;
    m_buffer = NULL;
  }

  m_bufferSize = 0;
  m_sampleRate = 0;
}

int CDVDAudioCodecPassthroughRaw::Decode(uint8_t* pData, int iSize)
{
  CLog::Log(LOGDEBUG, "CDVDAudioCodecPassthroughRaw::Decode %d", iSize);
  if (iSize <= 0) return 0;

  // HD audio has variable bitrate; Do pseudo-encapsulation to make it constant
  if ((m_hints.codec == AV_CODEC_ID_DTS && m_hints.channels > 6) || m_hints.codec == AV_CODEC_ID_TRUEHD)
  {
    if (!m_buffer)
    {
      m_bufferSize = CONSTANT_BUFFER_SIZE;
      m_buffer = (uint8_t*)malloc(m_bufferSize);
    }
    ((int*)m_buffer)[0] = iSize;
    memcpy(m_buffer+sizeof(int), pData, iSize);

    m_bufferUsed = m_bufferSize;
  }
  else
  {
    if (iSize > m_bufferSize)
    {
      m_bufferSize = iSize;
      m_buffer = (uint8_t*)realloc(m_buffer, m_bufferSize);
    }

    memcpy(m_buffer, pData, iSize);
    m_bufferUsed = iSize;
  }
  return iSize;
}

int CDVDAudioCodecPassthroughRaw::GetData(uint8_t** dst)
{
  *dst     = m_buffer;
  return m_bufferUsed;
}

void CDVDAudioCodecPassthroughRaw::Reset()
{
}

int CDVDAudioCodecPassthroughRaw::GetBufferSize()
{
  return (int)m_bufferUsed;
}
