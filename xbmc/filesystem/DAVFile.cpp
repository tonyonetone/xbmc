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

using namespace XFILE;
using namespace XCURL;

CDAVFile::CDAVFile(void)
  : CCurlFile()
{
}

CDAVFile::~CDAVFile(void)
{
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

  long response = m_state->Connect(m_bufferSize);
  if( response < 0 || response >= 400)
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
  if(m_state->m_fileSize > 0)
  {
    m_seekable = true;

    // if server says explicitly it can't seek, respect that
    if(m_state->m_httpheader.GetValue("Accept-Ranges").Equals("none"))
      m_seekable = false;
  }

  char* efurl;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_EFFECTIVE_URL,&efurl) && efurl)
    m_url = efurl;

  return true;
}

bool CDAVFile::OpenForWrite(const CURL& url, bool bOverWrite)
{
  return CCurlFile::OpenForWrite(url, bOverWrite);
}

bool CDAVFile::Exists(const CURL& url)
{
  return CCurlFile::Exists(url);
}

int CDAVFile::Stat(const CURL& url, struct __stat64* buffer)
{
  return CCurlFile::Stat(url, buffer);
}

unsigned int CDAVFile::Read(void* lpBuf, int64_t uiBufSize)
{
  return CCurlFile::Read(lpBuf, uiBufSize);
}

int CDAVFile::Write(const void* lpBuf, int64_t uiBufSize)
{
  return CCurlFile::Write(lpBuf, uiBufSize);
}

bool CDAVFile::ReadString(char *szLine, int iLineLength)
{
  return CCurlFile::ReadString(szLine, iLineLength);
}

int64_t CDAVFile::Seek(int64_t iFilePosition, int iWhence)
{
  return CCurlFile::Seek(iFilePosition, iWhence);
}

void CDAVFile::Close()
{
  return CCurlFile::Close();
}

int64_t CDAVFile::GetPosition()
{
  return CCurlFile::GetPosition();
}

int64_t CDAVFile::GetLength()
{
  return CCurlFile::GetLength();
}

void CDAVFile::Flush()
{
  return CCurlFile::Flush();
}

int  CDAVFile::GetChunkSize()
{
  return CCurlFile::GetChunkSize();
}

bool CDAVFile::SkipNext()
{
  return CCurlFile::SkipNext();
}

bool CDAVFile::Delete(const CURL& url)
{
  if (m_opened)
    return false;

  bool ret = false;
  CDAVFile dav;
  CStdString strRequest = "DELETE";

  dav.SetCustomRequest(strRequest);
 
  if (!dav.Open(url))
  {
    CLog::Log(LOGERROR, "%s - Unable to delete dav resource (%s)", __FUNCTION__, url.Get());
    return false;
  }

  dav.Close();

  return ret;
}

bool CDAVFile::Rename(const CURL& url, const CURL& urlnew)
{
  if (m_opened)
    return false;

  bool ret = false;
  CDAVFile dav;

  CURL url2(urlnew);
  CStdString strProtocol = url2.GetTranslatedProtocol();
  url2.SetProtocol(strProtocol);

  CStdString strRequest = "MOVE";
  dav.SetCustomRequest(strRequest);
  dav.SetRequestHeader("Destination", url2.GetWithoutUserDetails());

  if (!dav.Open(url))
  {
    CLog::Log(LOGERROR, "%s - Unable to rename dav resource (%s)", __FUNCTION__, url.Get());
    return false;
  }

  dav.Close();

  return ret;
}

bool CDAVFile::SetHidden(const CURL& url, bool hidden)
{
  return CCurlFile::SetHidden(url, hidden);
}

int CDAVFile::IoControl(EIoControl request, void* param)
{
  return CCurlFile::IoControl(request, param);
}

CStdString CDAVFile::GetContent()
{
  return CCurlFile::GetContent();
}
