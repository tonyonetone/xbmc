#pragma once
/*
 *      Copyright (C) 2016 Team Kodi
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

#include "MediaCrypto.h"

#include "jutils/jutils-details.hpp"

using namespace jni;

void CJNIMediaCrypto::setMediaDrmSession(const std::vector<char>& sessionId)
{
  jsize size;
  JNIEnv *env = xbmc_jnienv();

  size = sessionId.size();
  jbyteArray SID = env->NewByteArray(size);
  jbyte *bytedata = (jbyte*)sessionId.data();
  env->SetByteArrayRegion(SID, 0, size, bytedata);

  call_method<void>(m_object,
    "setMediaDrmSession", "([B)V", SID);

  env->DeleteLocalRef(SID);
}

bool CJNIMediaCrypto::requiresSecureDecoderComponent(const std::string& mime)
{
  return call_method<jboolean>(m_object,
    "requiresSecureDecoderComponent", "(Ljava/lang/String;)Z",
    jcast<jhstring>(mime));
}
