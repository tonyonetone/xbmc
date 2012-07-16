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
*  along with XBMC; see the file COPYING.  If not, write to
*  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*  http://www.gnu.org/copyleft/gpl.html
*
*/

#include "system.h"

#include "DAVFile.h"

#include "DAVCommon.h"
#include "utils/log.h"
#include "DllLibCurl.h"
#include "utils/XBMCTinyXML.h"
#include "utils/RegExp.h"

using namespace XFILE;
using namespace XCURL;

CDAVFile::CDAVFile(void)
  : CCurlFile()
  , lastResponseCode(0)
{
}

CDAVFile::~CDAVFile(void)
{
}

bool CDAVFile::Execute(const CURL& url)
{
  CURL url2(url);
  ParseAndCorrectUrl(url2);

  CLog::Log(LOGDEBUG, "CDAVFile::Execute(%p) %s", (void*)this, m_url.c_str());

  ASSERT(!(!m_state->m_easyHandle ^ !m_state->m_multiHandle));
  if( m_state->m_easyHandle == NULL )
    g_curlInterface.easy_aquire(url2.GetProtocol(), url2.GetHostName(), &m_state->m_easyHandle, &m_state->m_multiHandle );

  // setup common curl options
  SetCommonOptions(m_state);
  SetRequestHeaders(m_state);

  lastResponseCode = m_state->Connect(m_bufferSize);
  if( lastResponseCode < 0 || lastResponseCode >= 400)
    return false;

  char* efurl;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_EFFECTIVE_URL,&efurl) && efurl)
    m_url = efurl;

  if (lastResponseCode == 207)
  {
    CStdString strResponse;
    ReadData(strResponse);

    CXBMCTinyXML davResponse;
    davResponse.Parse(strResponse.c_str());

    if (!davResponse.Parse(strResponse))
    {
      CLog::Log(LOGERROR, "%s - Unable to process dav response (%s)", __FUNCTION__, m_url.c_str());
      Close();
      return false;
    }

    TiXmlNode *pChild;
    // Iterate over all responses
    for (pChild = davResponse.RootElement()->FirstChild(); pChild != 0; pChild = pChild->NextSibling())
    {
      if (CDAVCommon::ValueWithoutNamespace(pChild, "response"))
      {
        CStdString sRetCode = CDAVCommon::GetStatusTag(pChild->ToElement());
        CRegExp rxCode;
        rxCode.RegComp("HTTP/1\\.1\\s(\\d+)\\s.*"); 
        if (rxCode.RegFind(sRetCode) >= 0)
        {
          if (rxCode.GetSubCount())
          {
            lastResponseCode = atoi(rxCode.GetMatch(1).c_str());
            if( lastResponseCode < 0 || lastResponseCode >= 400)
              return false;
          }
        }

      }
    }
  }

  return true;
}

bool CDAVFile::Open(const CURL& url)
{
  m_opened = true;

  CURL url2(url);
  ParseAndCorrectUrl(url2);

  CLog::Log(LOGDEBUG, "CDAVFile::Open(%p) %s", (void*)this, m_url.c_str());

  ASSERT(!(!m_state->m_easyHandle ^ !m_state->m_multiHandle));
  if( m_state->m_easyHandle == NULL )
    g_curlInterface.easy_aquire(url2.GetProtocol(), url2.GetHostName(), &m_state->m_easyHandle, &m_state->m_multiHandle );

  // setup common curl options
  SetCommonOptions(m_state);
  SetRequestHeaders(m_state);

  lastResponseCode = m_state->Connect(m_bufferSize);
  if( lastResponseCode < 0 || lastResponseCode >= 400)
    return false;

  SetCorrectHeaders(m_state);

  // since we can't know the stream size up front if we're gzipped/deflated
  // flag the stream with an unknown file size rather than the compressed
  // file size.
  if (m_contentencoding.size() > 0)
    m_state->m_fileSize = 0;

  m_multisession = true;

  if(m_state->m_httpheader.GetValue("Transfer-Encoding").Equals("chunked"))
    m_state->m_fileSize = 0;

  m_seekable = false;
  if(m_state->m_fileSize > 0 && !(m_state->m_httpheader.GetValue("Accept-Ranges").Equals("none")))
  {
    m_seekable = true;
  }

  char* efurl;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_EFFECTIVE_URL,&efurl) && efurl)
    m_url = efurl;

  return true;
}

bool CDAVFile::OpenForWrite(const CURL& url, bool bOverWrite)
{
  // if file is already running, get info from it
  if( m_opened )
  {
    if (bOverWrite)
      return true;  
    else
      return false;
  }

  CURL url2(url);
  ParseAndCorrectUrl(url2);

  CLog::Log(LOGDEBUG, "CDAVFile::OpenForWrite(%p) %s", (void*)this, m_url.c_str());

  ASSERT(m_state->m_easyHandle == NULL);
  g_curlInterface.easy_aquire(url2.GetProtocol(), url2.GetHostName(), &m_state->m_easyHandle, NULL);

  SetCommonOptions(m_state);
  SetRequestHeaders(m_state);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_TIMEOUT, 5);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_NOBODY, 1);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEDATA, NULL); /* will cause write failure*/

  CURLcode result = g_curlInterface.easy_perform(m_state->m_easyHandle);

  if (result == CURLE_WRITE_ERROR || result == CURLE_OK)
    if (!bOverWrite) 
    {
      g_curlInterface.easy_release(&m_state->m_easyHandle, NULL);
      return false;
    }

  char* efurl;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_EFFECTIVE_URL,&efurl) && efurl)
  m_url = efurl;

  m_opened = true;
  return true;
}

bool CDAVFile::Exists(const CURL& url)
{
  // if file is already running, get info from it
  if( m_opened )
  {
    CLog::Log(LOGWARNING, "%s - Exist called on open file", __FUNCTION__);
    return true;
  }

  CURL url2(url);
  ParseAndCorrectUrl(url2);

  ASSERT(m_state->m_easyHandle == NULL);
  g_curlInterface.easy_aquire(url2.GetProtocol(), url2.GetHostName(), &m_state->m_easyHandle, NULL);

  SetCommonOptions(m_state);
  SetRequestHeaders(m_state);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_TIMEOUT, 5);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_NOBODY, 1);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEDATA, NULL); /* will cause write failure*/

  CURLcode result = g_curlInterface.easy_perform(m_state->m_easyHandle);
  g_curlInterface.easy_release(&m_state->m_easyHandle, NULL);

  if (result == CURLE_WRITE_ERROR || result == CURLE_OK)
    return true;

  errno = ENOENT;
  return false;
}

int CDAVFile::Write(const void* lpBuf, int64_t uiBufSize)
{
  if (!m_opened)
    return -1;

  ASSERT(m_state->m_easyHandle);

  SetCommonOptions(m_state);
  SetRequestHeaders(m_state);
  m_state->SetReadBuffer(lpBuf, uiBufSize);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_UPLOAD, 1);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_INFILESIZE_LARGE, uiBufSize);

  CURLcode result = g_curlInterface.easy_perform(m_state->m_easyHandle);

  if (result != CURLE_OK) 
  {
    long code;
    if(g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK )
      CLog::Log(LOGERROR, "%s - unable to write dav resource (%s) - %d", __FUNCTION__, m_url, code);
    return -1;
  }

  return uiBufSize;
}

bool CDAVFile::Delete(const CURL& url)
{
  if (m_opened)
    return false;

  CDAVFile dav;
  CStdString strRequest = "DELETE";

  dav.SetCustomRequest(strRequest);
 
  if (!dav.Execute(url))
  {
    CLog::Log(LOGERROR, "%s - Unable to delete dav resource (%s)", __FUNCTION__, url.Get());
    return false;
  }

  dav.Close();

  return true;
}

bool CDAVFile::Rename(const CURL& url, const CURL& urlnew)
{
  if (m_opened)
    return false;

  CDAVFile dav;

  CURL url2(urlnew);
  CStdString strProtocol = url2.GetTranslatedProtocol();
  url2.SetProtocol(strProtocol);

  CStdString strRequest = "MOVE";
  dav.SetCustomRequest(strRequest);
  dav.SetRequestHeader("Destination", url2.GetWithoutUserDetails());

  if (!dav.Execute(url))
  {
    CLog::Log(LOGERROR, "%s - Unable to rename dav resource (%s)", __FUNCTION__, url.Get());
    return false;
  }

  dav.Close();

  return true;
}

bool CDAVFile::SetHidden(const CURL& url, bool hidden)
{
  return CCurlFile::SetHidden(url, hidden);
}
