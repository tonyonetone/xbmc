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

#include "system.h"

#include <sstream>

#include <unistd.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/configuration.h>
#include <jni.h>

#include "XBMCApp.h"

#include "input/MouseStat.h"
#include "input/XBMC_keysym.h"
#include "guilib/Key.h"
#include "windowing/XBMC_events.h"
#include <android/log.h>

#include "Application.h"
#include "settings/AdvancedSettings.h"
#include "xbmc.h"
#include "windowing/WinEvents.h"
#include "guilib/GUIWindowManager.h"
#include "utils/log.h"
#include "ApplicationMessenger.h"
#include <android/bitmap.h>
#include "android/jni/JNIThreading.h"
#include "android/jni/BroadcastReceiver.h"
#include "android/jni/Intent.h"
#include "android/jni/PackageManager.h"
#include "android/jni/Context.h"
#include "android/jni/AudioManager.h"
#include "android/jni/PowerManager.h"
#include "android/jni/WakeLock.h"
#include "android/jni/Environment.h"
#include "android/jni/File.h"
#include "android/jni/IntentFilter.h"
#include "android/jni/NetworkInfo.h"
#include "android/jni/ConnectivityManager.h"
#include "android/jni/System.h"
#include "android/jni/ApplicationInfo.h"
#include "android/jni/StatFs.h"
#include "android/jni/BitmapDrawable.h"
#include "android/jni/Bitmap.h"
#include "android/jni/CharSequence.h"

#ifdef HAVE_LIBSTAGEFRIGHT
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define ANDROID_GRAPHICS_SURFACETEXTURE_JNI_ID "mSurfaceTexture"
#endif

#define GIGABYTES       1073741824

using namespace std;

template<class T, void(T::*fn)()>
void* thread_run(void* obj)
{
  (static_cast<T*>(obj)->*fn)();
  return NULL;
}

ANativeActivity *CXBMCApp::m_activity = NULL;
ANativeWindow* CXBMCApp::m_window = NULL;
int CXBMCApp::m_batteryLevel = 0;

CXBMCApp::CXBMCApp()
  : CJNIContext(), m_wakeLock(NULL)
  , m_VideoNativeWindow(NULL)
{
  m_firstrun = true;
  m_exiting=false;
}

void CXBMCApp::SetActivity(ANativeActivity *nativeActivity)
{
  if (nativeActivity == NULL)
  {
    android_printf("CXBMCApp: invalid ANativeActivity instance");
    exit(1);
    return;
  }
  m_activity = nativeActivity;
  CJNIContext::SetActivity(nativeActivity);
}

CXBMCApp::~CXBMCApp()
{
}

void CXBMCApp::onStart()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
  if (!m_firstrun)
  {
    android_printf("%s: Already running, ignoring request to start", __PRETTY_FUNCTION__);
    return;
  }
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&m_thread, &attr, thread_run<CXBMCApp, &CXBMCApp::run>, this);
  pthread_attr_destroy(&attr);
}

void CXBMCApp::onResume()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
}

void CXBMCApp::onPause()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
}

void CXBMCApp::onStop()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
}

void CXBMCApp::onDestroy()
{
  android_printf("%s", __PRETTY_FUNCTION__);

  // If android is forcing us to stop, ask XBMC to exit then wait until it's
  // been destroyed.
  if (!m_exiting)
  {
    XBMC_Stop();
    pthread_join(m_thread, NULL);
    android_printf(" => XBMC finished");
  }

  if (m_wakeLock != NULL && m_activity != NULL)
  {
    delete m_wakeLock;
    m_wakeLock = NULL;
  }
}

void CXBMCApp::onSaveState(void **data, size_t *size)
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
  // no need to save anything as XBMC is running in its own thread
}

void CXBMCApp::onConfigurationChanged()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
  // ignore any configuration changes like screen rotation etc
}

void CXBMCApp::onLowMemory()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
  // can't do much as we don't want to close completely
}

void CXBMCApp::onCreateWindow(ANativeWindow* window)
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
  if (window == NULL)
  {
    android_printf(" => invalid ANativeWindow object");
    return;
  }
  m_window = window;
  if (getWakeLock() &&  m_wakeLock)
    m_wakeLock->acquire();
  if(!m_firstrun)
  {
    XBMC_SetupDisplay();
    XBMC_Pause(false);
  }
}

void CXBMCApp::onResizeWindow()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
  // no need to do anything because we are fixed in fullscreen landscape mode
}

void CXBMCApp::onDestroyWindow()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);

  // If we have exited XBMC, it no longer exists.
  if (!m_exiting)
  {
    XBMC_DestroyDisplay();
    XBMC_Pause(true);
  }

  if (m_wakeLock)
    m_wakeLock->release();

  m_window=NULL;
}

void CXBMCApp::onGainFocus()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
}

void CXBMCApp::onLostFocus()
{
  android_printf("%s: ", __PRETTY_FUNCTION__);
}

bool CXBMCApp::getWakeLock()
{
  if (m_wakeLock)
    return true;

  m_wakeLock = new CJNIWakeLock(CJNIPowerManager(getSystemService("power")).newWakeLock("org.xbmc.xbmc"));

  return true;
}

void CXBMCApp::run()
{
  int status = 0;

  SetupEnv();

  CJNIIntent startIntent = getIntent();
  android_printf("XBMC Started with action: %s\n",startIntent.getAction().c_str());

  CJNIIntentFilter batteryFilter;
  batteryFilter.addAction("android.intent.action.BATTERY_CHANGED");
  registerReceiver(*this, batteryFilter);

  android_printf(" => waiting for a window");
  // Hack!
  // TODO: Change EGL startup so that we can start headless, then create the
  // window once android gives us a surface to play with.
  while(!m_window)
    usleep(1000);
  m_firstrun=false;
  android_printf(" => running XBMC_Run...");
  try
  {
    status = XBMC_Run(true);
    android_printf(" => XBMC_Run finished with %d", status);
  }
  catch(...)
  {
    android_printf("ERROR: Exception caught on main loop. Exiting");
  }

  // If we are have not been force by Android to exit, notify its finish routine.
  // This will cause android to run through its teardown events, it calls:
  // onPause(), onLostFocus(), onDestroyWindow(), onStop(), onDestroy().
  ANativeActivity_finish(m_activity);
  m_exiting=true;
}

void CXBMCApp::XBMC_Pause(bool pause)
{
  android_printf("XBMC_Pause(%s)", pause ? "true" : "false");
  // Only send the PAUSE action if we are pausing XBMC and something is currently playing
  if (pause && g_application.IsPlaying() && !g_application.IsPaused())
    CApplicationMessenger::Get().SendAction(CAction(ACTION_PAUSE), WINDOW_INVALID, true);
}

void CXBMCApp::XBMC_Stop()
{
  CApplicationMessenger::Get().Quit();
}

bool CXBMCApp::XBMC_SetupDisplay()
{
  android_printf("XBMC_SetupDisplay()");
  return CApplicationMessenger::Get().SetupDisplay();
}

bool CXBMCApp::XBMC_DestroyDisplay()
{
  android_printf("XBMC_DestroyDisplay()");
  return CApplicationMessenger::Get().DestroyDisplay();
}

int CXBMCApp::SetBuffersGeometry(int width, int height, int format)
{
  return ANativeWindow_setBuffersGeometry(m_window, width, height, format);
}

int CXBMCApp::android_printf(const char *format, ...)
{
  // For use before CLog is setup by XBMC_Run()
  va_list args;
  va_start(args, format);
  int result = __android_log_vprint(ANDROID_LOG_VERBOSE, "XBMC", format, args);
  va_end(args);
  return result;
}

int CXBMCApp::GetDPI()
{
  if (m_activity == NULL || m_activity->assetManager == NULL)
    return 0;

  // grab DPI from the current configuration - this is approximate
  // but should be close enough for what we need
  AConfiguration *config = AConfiguration_new();
  AConfiguration_fromAssetManager(config, m_activity->assetManager);
  int dpi = AConfiguration_getDensity(config);
  AConfiguration_delete(config);

  return dpi;
}

bool CXBMCApp::ListApplications(vector<androidPackage> *applications)
{
  CJNIList<CJNIApplicationInfo> packageList = GetPackageManager().getInstalledApplications(CJNIPackageManager::GET_ACTIVITIES);
  int numPackages = packageList.size();
  for (int i = 0; i < numPackages; i++)
  {
    androidPackage newPackage;
    newPackage.packageName = packageList.get(i).packageName;
    newPackage.packageLabel = GetPackageManager().getApplicationLabel(packageList.get(i)).toString();
    CJNIIntent intent = GetPackageManager().getLaunchIntentForPackage(newPackage.packageName);
    if (!intent || !intent.hasCategory("android.intent.category.LAUNCHER"))
      continue;

    applications->push_back(newPackage);
  }
  return true;
}

bool CXBMCApp::GetIconSize(const string &packageName, int *width, int *height)
{
  JNIEnv* env = xbmc_jnienv();
  AndroidBitmapInfo info;
  CJNIBitmapDrawable drawable = (CJNIBitmapDrawable)GetPackageManager().getApplicationIcon(packageName);
  CJNIBitmap icon(drawable.getBitmap());
  AndroidBitmap_getInfo(env, icon.get(), &info);
  *width = info.width;
  *height = info.height;
  return true;
}

bool CXBMCApp::GetIcon(const string &packageName, void* buffer, unsigned int bufSize)
{
  void *bitmapBuf = NULL;
  JNIEnv* env = xbmc_jnienv();
  CJNIBitmapDrawable drawable = (CJNIBitmapDrawable)GetPackageManager().getApplicationIcon(packageName);
  CJNIBitmap bitmap(drawable.getBitmap());
  AndroidBitmap_lockPixels(env, bitmap.get(), &bitmapBuf);
  if (bitmapBuf)
  {
    memcpy(buffer, bitmapBuf, bufSize);
    AndroidBitmap_unlockPixels(env, bitmap.get());
    return true;
  }
  return false;
}

bool CXBMCApp::HasLaunchIntent(const string &package)
{
  return GetPackageManager().getLaunchIntentForPackage(package) != NULL;
}

// Note intent, dataType, dataURI all default to ""
bool CXBMCApp::StartActivity(const string &package, const string &intent, const string &dataType, const string &dataURI)
{
  CJNIIntent newIntent = GetPackageManager().getLaunchIntentForPackage(package);
  if (!newIntent)
    return false;

  if (!dataURI.empty())
    newIntent.setData(dataURI);

  if (!intent.empty())
    newIntent.setAction(intent);

   startActivity(newIntent);
  return true;
}

int CXBMCApp::GetBatteryLevel()
{
  return m_batteryLevel;
}

bool CXBMCApp::GetExternalStorage(std::string &path, const std::string &type /* = "" */)
{
  std::string sType;
  std::string mountedState;
  bool mounted = false;

  if(type == "files" || type.empty())
  {
    CJNIFile external = CJNIEnvironment::getExternalStorageDirectory();
    if (external)
      path = external.getAbsolutePath();
  }
  else
  {
    if (type == "music")
      sType = "Music"; // Environment.DIRECTORY_MUSIC
    else if (type == "videos")
      sType = "Movies"; // Environment.DIRECTORY_MOVIES
    else if (type == "pictures")
      sType = "Pictures"; // Environment.DIRECTORY_PICTURES
    else if (type == "photos")
      sType = "DCIM"; // Environment.DIRECTORY_DCIM
    else if (type == "downloads")
      sType = "Download"; // Environment.DIRECTORY_DOWNLOADS
    if (!sType.empty())
    {
      CJNIFile external = CJNIEnvironment::getExternalStoragePublicDirectory(sType);
      if (external)
        path = external.getAbsolutePath();
    }
  }
  mountedState = CJNIEnvironment::getExternalStorageState();
  mounted = (mountedState == "mounted" || mountedState == "mounted_ro");
  return mounted && !path.empty();
}

bool CXBMCApp::GetStorageUsage(const std::string &path, std::string &usage)
{
  if (path.empty())
  {
    std::ostringstream fmt;
    fmt.width(24);  fmt << std::left  << "Filesystem";
    fmt.width(12);  fmt << std::right << "Size";
    fmt.width(12);  fmt << "Used";
    fmt.width(12);  fmt << "Avail";
    fmt.width(12);  fmt << "Use %";

    usage = fmt.str();
    return false;
  }

  CJNIStatFs fileStat(path);
  int blockSize = fileStat.getBlockSize();
  int blockCount = fileStat.getBlockCount();
  int freeBlocks = fileStat.getFreeBlocks();

  if (blockSize <= 0 || blockCount <= 0 || freeBlocks < 0)
    return false;

  float totalSize = (float)blockSize * blockCount / GIGABYTES;
  float freeSize = (float)blockSize * freeBlocks / GIGABYTES;
  float usedSize = totalSize - freeSize;
  float usedPercentage = usedSize / totalSize * 100;

  std::ostringstream fmt;
  fmt << std::fixed;
  fmt.precision(1);
  fmt.width(24);  fmt << std::left  << path;
  fmt.width(12);  fmt << std::right << totalSize << "G"; // size in GB
  fmt.width(12);  fmt << usedSize << "G"; // used in GB
  fmt.width(12);  fmt << freeSize << "G"; // free
  fmt.precision(0);
  fmt.width(12);  fmt << usedPercentage << "%"; // percentage used

  usage = fmt.str();
  return true;
}

// Used in Application.cpp to figure out volume steps
int CXBMCApp::GetMaxSystemVolume()
{
  JNIEnv* env = xbmc_jnienv();
  static int maxVolume = -1;
  if (maxVolume == -1)
  {
    maxVolume = GetMaxSystemVolume(env);
  }
  android_printf("CXBMCApp::GetMaxSystemVolume: %i",maxVolume);
  return maxVolume;
}

int CXBMCApp::GetMaxSystemVolume(JNIEnv *env)
{
  CJNIAudioManager audioManager(getSystemService("audio"));
  if (audioManager)
    return audioManager.getStreamMaxVolume();
    android_printf("CXBMCApp::SetSystemVolume: Could not get Audio Manager");
  return 0;
}

void CXBMCApp::SetSystemVolume(JNIEnv *env, float percent)
{
  CJNIAudioManager audioManager(getSystemService("audio"));
  int maxVolume = (int)(GetMaxSystemVolume() * percent);
  if (audioManager)
    audioManager.setStreamVolume(maxVolume);
  else
    android_printf("CXBMCApp::SetSystemVolume: Could not get Audio Manager");
}

void CXBMCApp::onReceive(CJNIIntent intent)
{
  std::string action = intent.getAction();
  android_printf("CXBMCApp::onReceive Got intent. Action: %s", action.c_str());
  if (action == "android.intent.action.BATTERY_CHANGED")
    m_batteryLevel = intent.getIntExtra("level",-1);
}

void CXBMCApp::SetupEnv()
{
  setenv("XBMC_ANDROID_SYSTEM_LIBS", CJNISystem::getProperty("java.library.path").c_str(), 0);
  setenv("XBMC_ANDROID_DATA", getApplicationInfo().dataDir.c_str(), 0);
  setenv("XBMC_ANDROID_LIBS", getApplicationInfo().nativeLibraryDir.c_str(), 0);
  setenv("XBMC_ANDROID_APK", getPackageResourcePath().c_str(), 0);

  std::string cacheDir = getCacheDir().getAbsolutePath();
  setenv("XBMC_TEMP", (cacheDir + "/temp").c_str(), 0);
  setenv("XBMC_BIN_HOME", (cacheDir + "/apk/assets").c_str(), 0);
  setenv("XBMC_HOME", (cacheDir + "/apk/assets").c_str(), 0);

  std::string externalDir;
  CJNIFile androidPath = getExternalFilesDir("");
  if (!androidPath)
    androidPath = getDir("org.xbmc.xbmc", 1);

  if (androidPath)
    externalDir = androidPath.getAbsolutePath();

  if (!externalDir.empty())
    setenv("HOME", externalDir.c_str(), 0);
  else
    setenv("HOME", getenv("XBMC_TEMP"), 0);
}

#ifdef HAVE_LIBSTAGEFRIGHT
bool CXBMCApp::InitStagefrightSurface()
{
   if (m_VideoNativeWindow != NULL)
    return true;
    
  JNIEnv* env = xbmc_jnienv();

  m_VideoTextureId = -1;

  glGenTextures(1, &m_VideoTextureId);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_VideoTextureId);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  
  jclass cSurfaceTexture = env->FindClass("android/graphics/SurfaceTexture");
  jmethodID midSurfaceTextureCtor = env->GetMethodID(cSurfaceTexture, "<init>", "(I)V");
  midSurfaceTextureRelease = env->GetMethodID(cSurfaceTexture, "release", "()V");
  m_midUpdateTexImage = env->GetMethodID(cSurfaceTexture, "updateTexImage", "()V");
  m_midGetTransformMatrix  = env->GetMethodID(cSurfaceTexture, "getTransformMatrix", "([F)V");
  jobject oSurfTexture = env->NewObject(cSurfaceTexture, midSurfaceTextureCtor, m_VideoTextureId);

  //jfieldID fidSurfaceTexture = env->GetFieldID(cSurfaceTexture, ANDROID_GRAPHICS_SURFACETEXTURE_JNI_ID, "I");
  //m_SurfaceTexture = (android::SurfaceTexture*)env->GetIntField(oSurfTexture, fidSurfaceTexture);

  env->DeleteLocalRef(cSurfaceTexture);
  m_SurfTexture = env->NewGlobalRef(oSurfTexture);
  env->DeleteLocalRef(oSurfTexture);

  jclass cSurface = env->FindClass("android/view/Surface");
  jmethodID midSurfaceCtor = env->GetMethodID(cSurface, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
  midSurfaceRelease = env->GetMethodID(cSurface, "release", "()V");
  jobject oSurface = env->NewObject(cSurface, midSurfaceCtor, m_SurfTexture);
  env->DeleteLocalRef(cSurface);
  m_Surface = env->NewGlobalRef(oSurface);
  env->DeleteLocalRef(oSurface);

  m_VideoNativeWindow = ANativeWindow_fromSurface(env, m_Surface);

  return true;
}

void CXBMCApp::UninitStagefrightSurface()
{
  if (m_VideoNativeWindow == NULL)
    return;

  JNIEnv* env = xbmc_jnienv();

  ANativeWindow_release(m_VideoNativeWindow);
  m_VideoNativeWindow = NULL;

  env->CallVoidMethod(m_Surface, midSurfaceRelease);
  env->DeleteGlobalRef(m_Surface);

  env->CallVoidMethod(m_SurfTexture, midSurfaceTextureRelease);
  env->DeleteGlobalRef(m_SurfTexture);

  glDeleteTextures(1, &m_VideoTextureId);
}

void CXBMCApp::UpdateStagefrightTexture()
{
//  m_SurfaceTexture->updateTexImage();

  JNIEnv* env = xbmc_jnienv();

  env->CallVoidMethod(m_SurfTexture, m_midUpdateTexImage);
}

void CXBMCApp::GetStagefrightTransformMatrix(float* transformMatrix)
{
//  m_SurfaceTexture->getTransformMatrix(transformMatrix);

  JNIEnv* env = xbmc_jnienv();

  jfloatArray arr = (jfloatArray)env->NewFloatArray(16);
  env->SetFloatArrayRegion(arr, 0, 16, transformMatrix);

  env->CallVoidMethod(m_SurfTexture, m_midGetTransformMatrix, arr);
  env->GetFloatArrayRegion(arr, 0, 16, transformMatrix);
  env->DeleteLocalRef(arr);
}

#endif
