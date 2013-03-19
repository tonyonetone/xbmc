/*
 *      Copyright (C) 2013 Team XBMC
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

#pragma once

#include "input/IJoystick.h"
#include "threads/SingleLock.h"

#include <string>
#include <map>

class CLinuxJoystickAndroid : public IJoystick
{
friend class CAndroidJoystick;

public:
  virtual ~CLinuxJoystickAndroid();

  static void Initialize(JoystickArray &joysticks);
  static void DeInitialize(JoystickArray &joysticks);

  virtual void Update();
  virtual const SJoystick &GetState() const;

  static CLinuxJoystickAndroid* getJoystick(int32_t deviceid);
  static void clearJoysticks();
  
private:
  CCriticalSection m_critSection;
  SJoystick   m_state;
  SJoystick   m_lastState;
  int32_t     m_axisIds[GAMEPAD_AXIS_COUNT];

  static std::map<int32_t, CLinuxJoystickAndroid*> m_joysticks;
};
