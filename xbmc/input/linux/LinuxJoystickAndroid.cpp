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

#include "LinuxJoystickAndroid.h"

#include "android/activity/JNIThreading.h"
#include "android/activity/AndroidExtra.h"

#include "utils/log.h"

std::map<int32_t, CLinuxJoystickAndroid*> CLinuxJoystickAndroid::m_joysticks;
CCriticalSection joySection;

CLinuxJoystickAndroid::~CLinuxJoystickAndroid()
{
}

/* static */
void CLinuxJoystickAndroid::Initialize(JoystickArray &joysticks)
{
  CSingleLock lock(joySection);

  JNIEnv* env = xbmc_jnienv();
  
  CLog::Log(LOGDEBUG, "CLinuxJoystickAndroid::Initialize");

    jclass cInputDevice = env->FindClass("android/view/InputDevice");
  jmethodID midGetDeviceIds = env->GetStaticMethodID(cInputDevice, "getDeviceIds", "()[I");
  jmethodID midGetDevice = env->GetStaticMethodID(cInputDevice, "getDevice", "(I)Landroid/view/InputDevice;");
  jmethodID midGetName = env->GetMethodID(cInputDevice, "getName", "()Ljava/lang/String;");
  jmethodID midGetSources = env->GetMethodID(cInputDevice, "getSources", "()I");
  jmethodID midGetMotionRanges = env->GetMethodID(cInputDevice, "getMotionRanges", "()Ljava/util/List;");

  jclass cList = env->FindClass("java/util/List");
  jmethodID midListSize = env->GetMethodID(cList, "size", "()I");
  jmethodID midListGet = env->GetMethodID(cList, "get", "(I)Ljava/lang/Object;");

  jintArray retarr = (jintArray) env->CallStaticObjectMethod(cInputDevice, midGetDeviceIds);
  jsize len = env->GetArrayLength(retarr);
  jint *ids = env->GetIntArrayElements(retarr, 0);
  for (int i=0; i<len; i++)
  {
    jobject oInputDevice = env->CallStaticObjectMethod(cInputDevice, midGetDevice, ids[i]);
    int sources = env->CallIntMethod(oInputDevice, midGetSources);

    jstring sDeviceName = (jstring)env->CallObjectMethod(oInputDevice, midGetName);
    const char *nativeString = env->GetStringUTFChars(sDeviceName, 0);
    std::string name = std::string(nativeString);
    env->ReleaseStringUTFChars(sDeviceName, nativeString);
    env->DeleteLocalRef(sDeviceName);

    CLog::Log(LOGDEBUG, ">> Input device detected: %s; id:%d, types: %d", name.c_str(), ids[i], sources);
    
    if (sources & AINPUT_SOURCE_GAMEPAD || sources & AINPUT_SOURCE_JOYSTICK)
    {
      CLinuxJoystickAndroid* joy = new CLinuxJoystickAndroid();
      joy->m_state.id = joy->m_lastState.id = ids[i];
      joy->m_state.name = joy->m_lastState.name = name;
      joy->m_state.buttonCount = joy->m_lastState.buttonCount = GAMEPAD_BUTTON_COUNT;
      joy->m_state.hatCount = joy->m_lastState.hatCount = 0;
      joy->m_state.axisCount = joy->m_lastState.axisCount = 0;
            
      jobject oListMotionRanges = env->CallObjectMethod(oInputDevice, midGetMotionRanges);
      int numaxis = env->CallIntMethod(oListMotionRanges, midListSize);

      jclass cMotionRange = env->FindClass("android/view/InputDevice$MotionRange");
      jmethodID midGetAxis = env->GetMethodID(cMotionRange, "getAxis", "()I");
      jmethodID midGetSource = env->GetMethodID(cMotionRange, "getSource", "()I");
      
      for (int j=0; j<numaxis; ++j)
      {
        jobject oMotionRange = env->CallObjectMethod(oListMotionRanges, midListGet, j);
        int axisId = env->CallIntMethod(oMotionRange, midGetAxis);
        int src = env->CallIntMethod(oMotionRange, midGetSource);
        env->DeleteLocalRef(oMotionRange);
        
        if (src & AINPUT_SOURCE_GAMEPAD || src & AINPUT_SOURCE_JOYSTICK && joy->m_state.axisCount < GAMEPAD_AXIS_COUNT)
        {
          joy->m_state.axisCount++;
          joy->m_lastState.axisCount = joy->m_state.axisCount;
          joy->m_axisIds[joy->m_state.axisCount-1] = axisId;
          CLog::Log(LOGDEBUG, ">>> added axis: %d; src: %d", axisId, src);
        }
      }
      env->DeleteLocalRef(oListMotionRanges);

      joysticks.push_back(boost::shared_ptr<IJoystick>(joy));
      CLinuxJoystickAndroid::m_joysticks.insert(std::pair<int32_t, CLinuxJoystickAndroid*>(ids[i], joy));
    }

    env->DeleteLocalRef(oInputDevice);
  }
  
  env->DeleteLocalRef(cList);
  env->DeleteLocalRef(retarr);
  env->DeleteLocalRef(cInputDevice);
}

void CLinuxJoystickAndroid::DeInitialize(JoystickArray &joysticks)
{
  for (int i = 0; i < (int)joysticks.size(); i++)
  {
    if (boost::dynamic_pointer_cast<CLinuxJoystickAndroid>(joysticks[i]))
    { 
      joysticks.erase(joysticks.begin() + i--);
    }
  }
  CLinuxJoystickAndroid::clearJoysticks();
}

void CLinuxJoystickAndroid::Update()
{
  CSingleTryLock lock(m_critSection);
  if (!lock.IsOwner())
    return;

  for (int i=0; i<m_state.buttonCount; ++i)
    m_lastState.buttons[i] = m_state.buttons[i];
  for (int i=0; i<m_state.hatCount; ++i)
    m_lastState.hats[i] = m_state.hats[i];
  for (int i=0; i<m_state.axisCount; ++i)
    m_lastState.axes[i] = m_state.axes[i];
}

const SJoystick& CLinuxJoystickAndroid::GetState() const 
{
  return m_lastState; 
}

CLinuxJoystickAndroid* CLinuxJoystickAndroid::getJoystick(int32_t deviceid)
{
  CSingleLock lock(joySection);

  std::map<int32_t, CLinuxJoystickAndroid*>::iterator it = CLinuxJoystickAndroid::m_joysticks.find(deviceid);
  if (it == CLinuxJoystickAndroid::m_joysticks.end())
    return NULL;
    
  return it->second;
}

void CLinuxJoystickAndroid::clearJoysticks()
{
  CSingleLock lock(joySection);

  std::map<int32_t, CLinuxJoystickAndroid*>::iterator it;
  while (!CLinuxJoystickAndroid::m_joysticks.empty())
  {
    it = CLinuxJoystickAndroid::m_joysticks.begin();
    CLinuxJoystickAndroid::m_joysticks.erase(it);
  }
}
