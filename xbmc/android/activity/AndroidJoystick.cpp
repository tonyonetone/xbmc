/*
 *      Copyright (C) 2012-2013 Team XBMC
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

#include "AndroidJoystick.h"
#include "XBMCApp.h"
#include "AndroidExtra.h"

#include "JNIThreading.h"

#include "input/JoystickManager.h"
#include "input/linux/LinuxJoystickAndroid.h"

#include <dlfcn.h>

extern float AMotionEvent_getAxisValue(const AInputEvent* motion_event, int32_t axis, size_t pointer_index);
static typeof(AMotionEvent_getAxisValue) *p_AMotionEvent_getAxisValue;
#define AMotionEvent_getAxisValue (*p_AMotionEvent_getAxisValue)

CAndroidJoystick::CAndroidJoystick()
{
  p_AMotionEvent_getAxisValue = (typeof(AMotionEvent_getAxisValue)*) dlsym(RTLD_DEFAULT, "AMotionEvent_getAxisValue");
  CXBMCApp::android_printf("CAndroidJoystick: AMotionEvent_getAxisValue: %p", p_AMotionEvent_getAxisValue);
}

CAndroidJoystick::~CAndroidJoystick()
{
}

bool CAndroidJoystick::onJoystickMoveEvent(AInputEvent* event)
{
  if (event == NULL)
    return false;

  int32_t deviceid = AInputEvent_getDeviceId(event);
  CLinuxJoystickAndroid* joy = CLinuxJoystickAndroid::getJoystick(deviceid);
  if (joy == NULL)
  {
    CJoystickManager::Get().Reinitialize();
    joy = CLinuxJoystickAndroid::getJoystick(deviceid);
  }
  if (joy == NULL)
    return true;
    
  CXBMCApp::android_printf("CAndroidJoystick: move event (devname: %s; device:%d, src:%d)", joy->m_state.name.c_str(), deviceid, AInputEvent_getSource(event));

  for (int i=0; i<joy->m_state.axisCount; ++i)
  {
    // Map axis ID to XBMC axis ID (Based upon xbox 360 mapping)
    int32_t xbmcAxis = i;
    float val = AMotionEvent_getAxisValue(event, joy->m_axisIds[i], 0);
    switch (joy->m_axisIds[i])
    {
      case AMOTION_EVENT_AXIS_X:
        xbmcAxis = 1;
        break;

      case AMOTION_EVENT_AXIS_Y:
        xbmcAxis = 2;
        break;

      case AMOTION_EVENT_AXIS_LTRIGGER:
        xbmcAxis = 3;
        break;

      case AMOTION_EVENT_AXIS_RTRIGGER:
        xbmcAxis = 3;
        val = -val;
        break;
        
      case AMOTION_EVENT_AXIS_Z:
        xbmcAxis = 4;
        break;

      case AMOTION_EVENT_AXIS_RZ:
        xbmcAxis = 5;
        break;
    }
    if (fabs(val) < 0.25)
      val = 0.0;
    if (val != joy->m_state.axes[xbmcAxis-1])
    {
      CSingleLock lock(joy->m_critSection);
      joy->m_state.axes[xbmcAxis-1] = val;
      // CXBMCApp::android_printf(">> axisId: %d; xbmcAxid: %d; val: %f", joy->m_axisIds[i], xbmcAxis, joy->m_state.axes[i]);
    }
  }

  return true;
}

bool CAndroidJoystick::onJoystickButtonEvent(AInputEvent* event)
{
  if (event == NULL)
    return false;

  int32_t deviceid = AInputEvent_getDeviceId(event);
  CLinuxJoystickAndroid* joy = CLinuxJoystickAndroid::getJoystick(deviceid);
  if (joy == NULL)
  {
    CJoystickManager::Get().Reinitialize();
    joy = CLinuxJoystickAndroid::getJoystick(deviceid);
  }
  if (joy == NULL)
    return true;

  int32_t keycode = AKeyEvent_getKeyCode(event);
  int32_t action = AKeyEvent_getAction(event);

  CXBMCApp::android_printf("CAndroidJoystick: button event (code: %d; action: %d; devname: %s; device:%d, src:%d)", 
    keycode, action, joy->m_state.name.c_str(), deviceid, AInputEvent_getSource(event));
  
  {
    CSingleLock lock(joy->m_critSection);

    for (int i=0; i<joy->m_state.buttonCount; ++i)
      joy->m_state.buttons[i] = 0;
      
    if (action == AKEY_EVENT_ACTION_DOWN)
    {
      // Map keycode to XBMC button ID (Based upon xbox 360 mapping)
      int32_t xbmcButton = -1;
      switch (keycode)
      {
        case AKEYCODE_MENU:
          return false;

        case AKEYCODE_BUTTON_A:
          xbmcButton = 1;
          break;

        case AKEYCODE_BUTTON_B:
          xbmcButton = 2;
          break;

        case AKEYCODE_BUTTON_X:
          xbmcButton = 3;
          break;

        case AKEYCODE_BUTTON_Y:
          xbmcButton = 4;
          break;

        case AKEYCODE_BUTTON_L1:
          xbmcButton = 5;
          break;

        case AKEYCODE_BUTTON_R1:
          xbmcButton = 6;
          break;

        case AKEYCODE_DPAD_UP:
          xbmcButton = 11;
          break;

        case AKEYCODE_DPAD_DOWN:
          xbmcButton = 12;
          break;

        case AKEYCODE_DPAD_LEFT:
          xbmcButton = 13;
          break;

        case AKEYCODE_DPAD_RIGHT:
          xbmcButton = 14;
          break;

        case AKEYCODE_BUTTON_THUMBL:
          xbmcButton = 9;
          break;

        case AKEYCODE_BUTTON_THUMBR:
          xbmcButton = 10;
          break;

        case AKEYCODE_BACK:
          xbmcButton = 7;
          break;

        case AKEYCODE_BUTTON_START:
          xbmcButton = 8;
          break;

        case AKEYCODE_BUTTON_SELECT:
          xbmcButton = 15;
          break;

      }
      if (xbmcButton > 0)
        joy->m_state.buttons[xbmcButton-1] = 1;
    }
  }
  
  return true;
}
