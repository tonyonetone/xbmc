/*
 *      Copyright (C) 2013 Team XBMC
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

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include <stdlib.h>

#include "GenericTouchDoubleTapDetector.h"

// maximum time between touch down and up (in nanoseconds)
#define DOUBLETAP_MAX_TIME            500000000
// maxmium distance between touch down and up (in multiples of screen DPI)
#define DOUBLETAP_MAX_DISTANCE        0.5f

CGenericTouchDoubleTapDetector::CGenericTouchDoubleTapDetector(ITouchActionHandler *handler, float dpi)
  : IGenericTouchGestureDetector(handler, dpi),
    m_doubletapInprogress(false), m_size(0)
{ }

bool CGenericTouchDoubleTapDetector::OnTouchDown(unsigned int index, const Pointer &pointer)
{
  if (index < 0 || index >= TOUCH_MAX_POINTERS)
    return false;

  m_size += 1;
  if (m_size > 1)
    return true;

  if (!m_doubletapInprogress)
  {
    // reset all values
    m_done = false;
    m_doubletapDetected = false;
    m_doubletapInprogress = false;
    m_startTime = pointer.current.time;
  }

  return true;
}

bool CGenericTouchDoubleTapDetector::OnTouchUp(unsigned int index, const Pointer &pointer)
{
  if (index < 0 || index >= TOUCH_MAX_POINTERS)
    return false;

  m_size -= 1;
  if (m_done)
    return false;

  // check if the double tap has been performed in the proper time span
  if (m_doubletapInprogress && (pointer.current.time - m_startTime) > DOUBLETAP_MAX_TIME)
  {
    m_done = true;
    return false;
  }

  // first tap of a double-tap
  if (!m_doubletapInprogress)
  {
    m_doubletapInprogress = true;
    return true;
  }

  // call the OnDoubeTap() callback
  OnDoubeTap(pointer.down.x, pointer.down.y, m_size + 1);
  return true;
}

bool CGenericTouchDoubleTapDetector::OnTouchMove(unsigned int index, const Pointer &pointer)
{
  if (index < 0 || index >= TOUCH_MAX_POINTERS)
    return false;

  // only handle double tap of moved pointers
  if (index >= m_size || m_done)
    return false;

  float deltaXabs = abs(pointer.current.x - pointer.down.x);
  float deltaYabs = abs(pointer.current.y - pointer.down.y);
  if (deltaXabs > m_dpi * DOUBLETAP_MAX_DISTANCE || deltaYabs > m_dpi * DOUBLETAP_MAX_DISTANCE)
  {
    m_done = true;
    return false;
  }
  
  return true;
}

bool CGenericTouchDoubleTapDetector::OnTouchUpdate(unsigned int index, const Pointer &pointer)
{
  if (index < 0 || index >= TOUCH_MAX_POINTERS)
    return false;

  if (m_done)
    return true;

  return OnTouchMove(index, pointer);
}
