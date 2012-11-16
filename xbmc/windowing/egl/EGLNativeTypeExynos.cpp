/*
 *      Copyright (C) 2011-2012 Team XBMC
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
#include <EGL/egl.h>
#include "EGLNativeTypeExynos.h"
#include "utils/log.h"
#include <stdlib.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include "utils/StringUtils.h"
#include "guilib/gui3d.h"
#include <linux/media.h>

#include <unistd.h>

#include "linux/LinuxV4l2.h"

#ifdef CLASSNAME
#undef CLASSNAME
#endif
#define CLASSNAME "CEGLNativeTypeExynos"

// hints : V4L2_OUT_CAP_PRESETS

CEGLNativeTypeExynos::CEGLNativeTypeExynos()
{
  m_iDeviceHandle = -1;
  m_iFBHandle = -1;

#if 0
  m_iFBHandle = open("/dev/fb0", O_RDWR, 0);
  if(m_iFBHandle < 0)
  {
    CLog::Log(LOGERROR, "%s::%s - open dmebuffer evice /dev/fb0\n", CLASSNAME, __func__);
    printf("%s::%s - open dmebuffer evice /dev/fb0\n", CLASSNAME, __func__);
  }
  else
  {
    struct fb_fix_screeninfo finfo;
    if(ioctl(m_iFBHandle, FBIOGET_FSCREENINFO, &finfo) == -1)
    {
      CLog::Log(LOGERROR, "%s::%s - FBIOGET_FSCREENINFO\n", CLASSNAME, __func__);
      printf("%s::%s - FBIOGET_FSCREENINFO\n", CLASSNAME, __func__);
    }

    struct fb_var_screeninfo info;
    if(ioctl(m_iFBHandle, FBIOGET_VSCREENINFO, &info) == -1)
    {
      CLog::Log(LOGERROR, "%s::%s - FBIOGET_VSCREENINFO\n", CLASSNAME, __func__);
      printf("%s::%s - FBIOGET_FSCREENINFO\n", CLASSNAME, __func__);
    }
    printf("info.xres %d info.yres %d info.upper_margin %d info.lower_margin %d info.pixclock %d\n", 
        info.xres, info.yres, info.upper_margin, info.lower_margin, info.pixclock);
  }
#endif

  m_iDeviceHandle = open("/dev/video10", O_RDWR, 0);
  if(m_iDeviceHandle < 0)
  {
    CLog::Log(LOGERROR, "%s::%s - open decoder device /dev/video10\n", CLASSNAME, __func__);
    printf("%s::%s - open decoder device /dev/video10\n", CLASSNAME, __func__);
  }

  m_iDeviceHandle1 = open("/dev/video11", O_RDWR, 0);
  if(m_iDeviceHandle1 < 0)
  {
    CLog::Log(LOGERROR, "%s::%s - open decoder device /dev/video11\n", CLASSNAME, __func__);
    printf("%s::%s - open decoder device /dev/video11\n", CLASSNAME, __func__);
  }
}

CEGLNativeTypeExynos::~CEGLNativeTypeExynos()
{
  if(m_iFBHandle >= 0)
    close(m_iFBHandle);
  if(m_iDeviceHandle >= 0)
    close(m_iDeviceHandle);
  if(m_iDeviceHandle1 >= 0)
    close(m_iDeviceHandle1);

  while(!m_Presets.empty())
  {
    ExynosPreset *preset = m_Presets.front();
    m_Presets.pop_front();
    free(preset);
  }
}

bool CEGLNativeTypeExynos::CheckCompatibility()
{

  struct media_entity_desc me_desc;
  struct stat devstat;
  char devname[64];
  char sysname[32];
  char target[1024];
  int ret;
  char *p;

  memset(&me_desc, 0, sizeof(me_desc));

  int fdMedia = open("/dev/media0", O_RDWR, 0);
  if(fdMedia >= 0)
  {
    while(true)
    {
      me_desc.id |= MEDIA_ENT_ID_FLAG_NEXT;

      if(ioctl(fdMedia, MEDIA_IOC_ENUM_ENTITIES, &me_desc))
        break;

      CStdString strTmp = me_desc.name;
      if(strTmp == "exynos4-fimc.0.m2m")
      {
        return true;
      }
    }
  }
  return false;
}

void CEGLNativeTypeExynos::Initialize()
{
  return;
}
void CEGLNativeTypeExynos::Destroy()
{
  return;
}

bool CEGLNativeTypeExynos::CreateNativeDisplay()
{
  m_nativeDisplay = EGL_DEFAULT_DISPLAY;
  return true;
}

bool CEGLNativeTypeExynos::CreateNativeWindow()
{
#if defined(_FBDEV_WINDOW_H_)
  fbdev_window *nativeWindow = new fbdev_window;
  if (!nativeWindow)
    return false;

  nativeWindow->width = 1280;
  nativeWindow->height = 720;
  m_nativeWindow = nativeWindow;
  return true;
#else
  return false;
#endif
}

bool CEGLNativeTypeExynos::GetNativeDisplay(XBNativeDisplayType **nativeDisplay) const
{
  if (!nativeDisplay)
    return false;
  *nativeDisplay = (XBNativeDisplayType*) &m_nativeDisplay;
  return true;
}

bool CEGLNativeTypeExynos::GetNativeWindow(XBNativeWindowType **nativeWindow) const
{
  if (!nativeWindow)
    return false;
  *nativeWindow = (XBNativeWindowType*) &m_nativeWindow;
  return true;
}

bool CEGLNativeTypeExynos::DestroyNativeDisplay()
{
  return true;
}

bool CEGLNativeTypeExynos::DestroyNativeWindow()
{
  free(m_nativeWindow);
  return true;
}

bool CEGLNativeTypeExynos::GetNativeResolution(RESOLUTION_INFO *res) const
{
  struct v4l2_format fmt;
  struct v4l2_dv_preset dv_preset;
  int iRet;
  bool bFound = false;

  memset(&dv_preset, 0, sizeof(struct v4l2_dv_preset));

  if (ioctl(m_iDeviceHandle, VIDIOC_G_DV_PRESET, &dv_preset) >= 0) 
  {
    for(std::list<ExynosPreset *>::const_iterator it = m_Presets.begin(); it != m_Presets.end(); ++it)
    {
      ExynosPreset *preset = *it;

      if(preset->iId == dv_preset.preset)
      {
        bFound = PresetToResolution((char *)preset->strPreset, res);
        if(bFound)
        {
          CLog::Log(LOGDEBUG, "%s::%s preset got %d width %d height %d refresh %f\n", CLASSNAME, __func__,
            preset->iId, res->iWidth, res->iHeight, res->fRefreshRate);
          printf("%s::%s preset got %d width %d height %d refresh %f\n", CLASSNAME, __func__,
            preset->iId, res->iWidth, res->iHeight, res->fRefreshRate);
          break;
        }
      }
    }
  }

  if(!bFound)
  {
    res->iWidth = 1280;
    res->iHeight= 720;

    res->fRefreshRate   = 60;
    res->dwFlags        = D3DPRESENTFLAG_PROGRESSIVE;
    res->iScreen        = 0;
    res->bFullScreen    = true;
    res->iSubtitles     = (int)(0.965 * res->iHeight);
    res->fPixelRatio    = 1.0f;
    res->iScreenWidth   = res->iWidth;
    res->iScreenHeight  = res->iHeight;
    res->strMode.Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
    res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  }

  CLog::Log(LOGNOTICE,"Current resolution: %s\n",res->strMode.c_str());
  return true;
}

bool CEGLNativeTypeExynos::SetNativeResolution(const RESOLUTION_INFO &res)
{
  struct v4l2_format fmt;
  struct v4l2_dv_preset dv_preset;
  int iRet;
  bool bFound = false;

  std::string strPreset = ResolutionToPreset(res);
  printf("%s::%s preset : %s width %d height %d\n", CLASSNAME, __func__, 
      strPreset.c_str(), res.iWidth, res.iHeight);

  for(std::list<ExynosPreset *>::const_iterator it = m_Presets.begin(); it != m_Presets.end(); ++it)
  {
    ExynosPreset *preset = *it;

    printf("%s::%s search %s %s\n", CLASSNAME, __func__,
        strPreset.c_str(), preset->strPreset);
    if(strPreset == std::string(preset->strPreset))
    {
      bFound = true;

      memset(&dv_preset, 0, sizeof(struct v4l2_dv_preset));
      dv_preset.preset = preset->iId;

      break;
    }
  }

  if(!bFound)
    return false;

#if 0
  struct fb_var_screeninfo info;
  if(ioctl(m_iFBHandle, FBIOGET_VSCREENINFO, &info) == -1)
  {
    CLog::Log(LOGERROR, "%s::%s - FBIOGET_VSCREENINFO\n", CLASSNAME, __func__);
    printf("%s::%s - FBIOGET_FSCREENINFO\n", CLASSNAME, __func__);
  }

  info.reserved[0] = 0;
  info.reserved[1] = 0;
  info.reserved[2] = 0;
  info.xoffset = 0;
  info.yoffset = 0;
  info.activate = FB_ACTIVATE_NOW;

  info.bits_per_pixel = 32;
  info.red.offset     = 16;
  info.red.length     = 8;
  info.green.offset   = 8;
  info.green.length   = 8;
  info.blue.offset    = 0;
  info.blue.length    = 8;
  info.transp.offset  = 0;
  info.transp.length  = 0;

  info.xres = res.iScreenWidth;
  info.yres = res.iScreenHeight;

  //info.yres_virtual = info.yres * 2;
  info.yres_virtual = info.yres;

  if (ioctl(m_iFBHandle, FBIOPUT_VSCREENINFO, &info) == -1) 
  {
    info.yres_virtual = info.yres;
    CLog::Log(LOGERROR, "%s::%s - FBIOPUT_VSCREENINFO\n", CLASSNAME, __func__);
    printf("%s::%s - errror FBIOPUT_VSCREENINFO\n", CLASSNAME, __func__);
    return false;
  }

  if (ioctl(m_iFBHandle, FBIOPAN_DISPLAY, &info) == -1) 
  {
    CLog::Log(LOGERROR, "%s::%s - FBIOPAN_DISPLAY\n", CLASSNAME, __func__);
    printf("%s::%s - error FBIOPAN_DISPLAY\n", CLASSNAME, __func__);
    return false;
  }
#endif

  SetDisplayResolution(m_iDeviceHandle, dv_preset.preset, res);
  SetDisplayResolution(m_iDeviceHandle1, dv_preset.preset, res);

  printf("%s::%s width %d height %d refresh %f preset %d\n", CLASSNAME, __func__,
    res.iScreenWidth, res.iScreenHeight, res.fRefreshRate, dv_preset.preset);

  return false;
}

bool CEGLNativeTypeExynos::ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions)
{
  struct v4l2_format fmt;
  struct v4l2_dv_enum_preset enum_preset;
  int iRet;
  RESOLUTION_INFO res;

  for (int index = 0; ; index++) 
  {
    enum_preset.index = index;
    iRet = ioctl(m_iDeviceHandle, VIDIOC_ENUM_DV_PRESETS, &enum_preset);

    if (iRet < 0 || errno == EINVAL) 
      break;

    if(PresetToResolution((char *)enum_preset.name, &res))
    {
      resolutions.push_back(res);
      printf("%s::%s - VIDIOC_ENUM_DV_PRESETS preset got %d width %d height %d refresh %f\n", 
          CLASSNAME, __func__, enum_preset.preset, res.iWidth, res.iHeight, res.fRefreshRate);

      ExynosPreset *preset = (ExynosPreset *)malloc(sizeof(struct ExynosPreset));

      memset(preset, 0, sizeof(struct ExynosPreset));
      preset->iId = enum_preset.preset;
      strcpy(preset->strPreset, (char *)enum_preset.name);

      m_Presets.push_back(preset);
    }
  }

  iRet = GetNativeResolution(&res);
  if (iRet && res.iWidth > 1 && res.iHeight > 1)
  {
    resolutions.push_back(res);
    return true;
  }
  return false;
}

bool CEGLNativeTypeExynos::GetPreferredResolution(RESOLUTION_INFO *res) const
{
  res->iWidth = 1280;
  res->iHeight= 720;
  res->fRefreshRate = 60;
  res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->iScreenWidth  = res->iWidth;
  res->iScreenHeight = res->iHeight;
  res->strMode.Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
     res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  return true;
}

bool CEGLNativeTypeExynos::ShowWindow(bool show)
{
  return false;
}

bool CEGLNativeTypeExynos::SetDisplayResolution(int deviceHandle, int preset, const RESOLUTION_INFO &res)
{
  struct v4l2_format fmt;
  struct v4l2_dv_preset dv_preset;
  int ret;

  memset(&dv_preset, 0, sizeof(struct v4l2_dv_preset));
  dv_preset.preset = preset;

//  CLinuxV4l2::StreamOn(deviceHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMOFF);

  ret = ioctl(deviceHandle, VIDIOC_S_DV_PRESET, &dv_preset);
  if(ret < 0)
  {
    printf("%s::%s - VIDIOC_S_DV_PRESET failed %d %d\n", CLASSNAME, __func__, ret, errno);
  }

  ret = ioctl(deviceHandle, VIDIOC_G_DV_PRESET, &dv_preset);
  if(ret < 0)
  {
    printf("%s::%s - VIDIOC_G_DV_PRESET failed %d %d\n", CLASSNAME, __func__, ret, errno);
  }

  printf("%s::%s - VIDIOC_G_DV_PRESET : %d\n", CLASSNAME, __func__, dv_preset.preset);

  memset(&fmt, 0, sizeof(struct v4l2_format));
  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

  ret = ioctl(deviceHandle, VIDIOC_G_FMT, &fmt);
  if (ret != 0)
  {
    CLog::Log(LOGERROR, "%s::%s - set decoder format failed\n", CLASSNAME, __func__);
    printf("%s::%s - get format failed\n", CLASSNAME, __func__);
  }

  printf("%s::%s - width %d height %d\n", CLASSNAME, __func__, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);

  fmt.fmt.pix_mp.width = res.iScreenWidth;
  fmt.fmt.pix_mp.height = res.iScreenHeight;

  ret = ioctl(deviceHandle, VIDIOC_S_FMT, &fmt);
  if (ret != 0)
  {
    CLog::Log(LOGERROR, "%s::%s - set decoder format failed\n", CLASSNAME, __func__);
    printf("%s::%s - set format failed\n", CLASSNAME, __func__);
  }

  struct v4l2_crop crop;

  memset(&crop, 0, sizeof(struct v4l2_crop));
  crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

  crop.c.left = 0;
  crop.c.top = 0;
  crop.c.width = res.iScreenWidth;
  crop.c.height = res.iScreenHeight;

  ret = ioctl(deviceHandle, VIDIOC_S_CROP, &crop);
  if (ret)
  {
    CLog::Log(LOGERROR, "%s::%s - set cropping\n", CLASSNAME, __func__);
    printf("%s::%s - set cropping\n", CLASSNAME, __func__);
  }

  ret = ioctl(deviceHandle, VIDIOC_G_FMT, &fmt);
  if (ret != 0)
  {
    CLog::Log(LOGERROR, "%s::%s - set decoder format failed\n", CLASSNAME, __func__);
    printf("%s::%s - get format failed\n", CLASSNAME, __func__);
  }

  printf("%s::%s - width %d height %d\n", CLASSNAME, __func__, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);

//  CLinuxV4l2::RequestBuffer(deviceHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 
//      V4L2_MEMORY_MMAP, 3);

//  CLinuxV4l2::StreamOn(deviceHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON);

  return true;
}

bool CEGLNativeTypeExynos::PresetToResolution(const char *mode, RESOLUTION_INFO *res) const
{
  if (!res)
    return false;

  res->iWidth = 0;
  res->iHeight= 0;

  if(!mode)
    return false;

  CStdString fromMode = mode;

  printf("%s::%s - %s\n", CLASSNAME, __func__, mode);

  if (fromMode.Equals("720p@60"))
  {
    res->iWidth = 1280;
    res->iHeight= 720;
    res->iScreenWidth  = res->iWidth;
    res->iScreenHeight = res->iHeight;
    res->fRefreshRate = 60;
    res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  }
  else if (fromMode.Equals("720p@50"))
  {
    res->iWidth = 1280;
    res->iHeight= 720;
    res->iScreenWidth  = res->iWidth;
    res->iScreenHeight = res->iHeight;
    res->fRefreshRate = 50;
    res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  }
  else if (fromMode.Equals("720p@59.94"))
  {
    res->iWidth = 1280;
    res->iHeight= 720;
    res->iScreenWidth  = res->iWidth;
    res->iScreenHeight = res->iHeight;
    res->fRefreshRate = 59.94f;
    res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  }
/*
  else if (fromMode.Equals("1080p@50"))
  {
    res->iWidth = 1920;
    res->iHeight= 1080;
    //res->iScreenWidth = 1280;
    //res->iScreenHeight= 720;
    res->iScreenWidth  = res->iWidth;
    res->iScreenHeight = res->iHeight;
    res->fRefreshRate = 50;
    res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  }
  else if (fromMode.Equals("1080p@30"))
  {
    res->iWidth = 1920;
    res->iHeight= 1080;
    //res->iScreenWidth = 1280;
    //res->iScreenHeight= 720;
    res->iScreenWidth  = res->iWidth;
    res->iScreenHeight = res->iHeight;
    res->fRefreshRate = 30;
    res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  }
  else if (fromMode.Equals("1080p@60"))
  {
    res->iWidth = 1920;
    res->iHeight= 1080;
    //res->iScreenWidth = 1280;
    //res->iScreenHeight= 720;
    res->iScreenWidth  = res->iWidth;
    res->iScreenHeight = res->iHeight;
    res->fRefreshRate = 60;
    res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  }
  else if (fromMode.Equals("480p@59.94"))
  {
    res->iWidth = 720;
    res->iHeight= 480;
    res->iScreenWidth  = res->iWidth;
    res->iScreenHeight = res->iHeight;
    res->fRefreshRate = 59.94f;
    res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE;
  }
*/
  else
  {
    return false;
  }

  res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->strMode.Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
    res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");

  return res->iWidth > 0 && res->iHeight> 0;
}

std::string CEGLNativeTypeExynos::ResolutionToPreset(const RESOLUTION_INFO &res)
{
  std::string strPreset = "720p@60";

  printf("%s::%s - width %d height %d fRefreshRate %f\n", CLASSNAME, __func__, 
      res.iWidth, res.iHeight, res.fRefreshRate);

  if(res.iWidth == 1920 && res.iHeight == 1080 && res.fRefreshRate == 50.0f)
    strPreset = "1080p@50";
  else if(res.iWidth == 1920 && res.iHeight == 1080 && res.fRefreshRate == 30.0f)
    strPreset = "1080p@30";
  else if(res.iWidth == 1920 && res.iHeight == 1080 && res.fRefreshRate == 60.0f)
    strPreset = "1080p@60";
  else if(res.iWidth == 1280 && res.iHeight == 720 && res.fRefreshRate == 50.0f)
    strPreset = "720p@50";
  else if(res.iWidth == 1280 && res.iHeight == 720 && res.fRefreshRate == 59.94f)
    strPreset = "720p@59.94";
  else if(res.iWidth == 1280 && res.iHeight == 720 && res.fRefreshRate == 60.0f)
    strPreset = "720p@60";
  else if(res.iWidth == 720 && res.iHeight == 480 && res.fRefreshRate == 59.94f)
    strPreset = "480@59.94";
  else
    printf("%s::%s - not found\n", CLASSNAME, __func__);

  return strPreset;
}
