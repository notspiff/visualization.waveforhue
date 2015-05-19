/*  XMMS - Cross-platform multimedia player
*  Copyright (C) 1998-2000  Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/*
*  Wed May 24 10:49:37 CDT 2000
*  Fixes to threading/context creation for the nVidia X4 drivers by
*  Christian Zander <phoenix@minion.de>
*/

/*
*  Ported to GLES by gimli
*/



#include <string.h>
#include <cmath>
#if defined(__APPLE__)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#include "addons/include/xbmc_vis_dll.h"
#include "VisGUIShader.h"

//th
#include <curl/curl.h>
#include "fft.h"
//#include <string>
#include <math.h>
#include <sstream>
#include <vector>
//

#define NUM_BANDS 16

#ifndef M_PI
#define M_PI       3.141592654f
#endif
#define DEG2RAD(d) ( (d) * M_PI/180.0f )

/*GLfloat x_angle = 20.0f, x_speed = 0.0f;
GLfloat y_angle = 45.0f, y_speed = 0.5f;
GLfloat z_angle = 0.0f, z_speed = 0.0f;
GLfloat heights[16][16], cHeights[16][16], scale;
GLfloat hSpeed = 0.025f;
GLenum  g_mode = GL_TRIANGLES;
*/
float g_fWaveform[2][512];

const char *frag = "precision mediump float; \n"
"varying lowp vec4 m_colour; \n"
"void main () \n"
"{ \n"
"  gl_FragColor = m_colour; \n"
"}\n";

const char *vert = "attribute vec4 m_attrpos;\n"
"attribute vec4 m_attrcol;\n"
"attribute vec4 m_attrcord0;\n"
"attribute vec4 m_attrcord1;\n"
"varying vec4   m_cord0;\n"
"varying vec4   m_cord1;\n"
"varying lowp   vec4 m_colour;\n"
"uniform mat4   m_proj;\n"
"uniform mat4   m_model;\n"
"void main ()\n"
"{\n"
"  mat4 mvp    = m_proj * m_model;\n"
"  gl_Position = mvp * m_attrpos;\n"
"  m_colour    = m_attrcol;\n"
"  m_cord0     = m_attrcord0;\n"
"  m_cord1     = m_attrcord1;\n"
"}\n";

CVisGUIShader  *vis_shader = NULL;

//th
#define BUFFERSIZE 1024
#define NUM_FREQUENCIES (512)
CURL *curl;
CURLcode res;

namespace
{
  // User config settings
  //UserSettings g_Settings;

  FFT g_fftobj;

  float fTime, fElapsedTime, fAppTime, fElapsedAppTime, fUpdateTime, fLastTime, fLightTime;
  int iFrames = 0;
  float fFPS = 0;
}

struct SoundData
{
  float   imm[2][3];                // bass, mids, treble, no damping, for each channel (long-term average is 1)
  float   avg[2][3];               // bass, mids, treble, some damping, for each channel (long-term average is 1)
  float   med_avg[2][3];          // bass, mids, treble, more damping, for each channel (long-term average is 1)
  //    float   long_avg[2][3];        // bass, mids, treble, heavy damping, for each channel (long-term average is 1)
  float   fWaveform[2][576];             // Not all 576 are valid! - only NUM_WAVEFORM_SAMPLES samples are valid for each channel (note: NUM_WAVEFORM_SAMPLES is declared in shell_defines.h)
  float   fSpectrum[2][NUM_FREQUENCIES]; // NUM_FREQUENCIES samples for each channel (note: NUM_FREQUENCIES is declared in shell_defines.h)

  float specImm[32];
  float specAvg[32];
  float specMedAvg[32];

  float bigSpecImm[512];
  float leftBigSpecAvg[512];
  float rightBigSpecAvg[512];
};

SoundData g_sound;
float g_bass, g_bassLast;
float g_treble, g_trebleLast;
float g_middle, g_middleLast;
float g_timePass;
bool g_finished;
float g_movingAvgMid[128];
float g_movingAvgMidSum;


//th
std::string strHueBridgeIPAddress = "192.168.10.6";
std::string strHost = "Host: " + strHueBridgeIPAddress;
std::string strURLRegistration = "http://" + strHueBridgeIPAddress + "/api";
std::string strURLLight, strJson;
std::vector<std::string> lightIDs;
int numberOfLights = 3;
int lastHue, initialHue, targetHue, maxBri, targetBri;
int currentBri = 75;
float beatThreshold = 0.25f;
bool useWaveForm = true;
float rgb[3] = { 1.0f, 1.0f, 1.0f };


struct timespec systemClock;


//hsv to rgb conversion
void hsvToRgb(float h, float s, float v, float _rgb[]) {
  float r = 0.0f, g = 0.0f, b = 0.0f;

  int i = int(h * 6);
  float f = h * 6 - i;
  float p = v * (1 - s);
  float q = v * (1 - f * s);
  float t = v * (1 - (1 - f) * s);

  switch (i % 6){
  case 0: r = v, g = t, b = p; break;
  case 1: r = q, g = v, b = p; break;
  case 2: r = p, g = v, b = t; break;
  case 3: r = p, g = q, b = v; break;
  case 4: r = t, g = p, b = v; break;
  case 5: r = v, g = p, b = q; break;
  }

  _rgb[0] = r;
  _rgb[1] = g;
  _rgb[2] = b;
}

void HTTP_POST(int bri, int sat, int transitionTime, bool on, bool off)
{

  if (on) //turn on
    strJson = "{\"on\":true}";
  else if (off) //turn light off
    strJson = "{\"on\":false}";
  else if (sat > 0) //change saturation
  {
    std::ostringstream oss;
    oss << "{\"bri\":" << bri << ",\"hue\":" << lastHue <<
      ",\"sat\":" << sat << ",\"transitiontime\":"
      << transitionTime << "}";
    strJson = oss.str();
  }
  else //change lights
  {
    std::ostringstream oss;
    oss << "{\"bri\":" << bri << ",\"hue\":" << lastHue <<
      ",\"transitiontime\":" << transitionTime << "}";
    strJson = oss.str();
  }

  if (curl) {
    //append headers

    // Now specify we want to PUT data, but not using a file, so it has o be a CUSTOMREQUEST
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    //curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strJson.c_str());

    for (int i = 0; i < numberOfLights; i++)
    {
      strURLLight = "http://" + strHueBridgeIPAddress +
        "/api/KodiVisWave/lights/" + lightIDs[i] + "/state";
      // Set the URL that is about to receive our POST. 
      curl_easy_setopt(curl, CURLOPT_URL, strURLLight.c_str());
      // Perform the request, res will get the return code
      res = curl_easy_perform(curl);
    }
  }
}

void TurnLightsOn()
{
  HTTP_POST(0, 0, 0, true, false);
}

void TurnLightsOff()
{
  HTTP_POST(0, 0, 0, false, true);
}

void UpdateLights(int bri, int sat, int transitionTime)
{
  HTTP_POST(bri, sat, transitionTime, false, false);
}

void AdjustBrightness() //nicely bring the brightness up or down
{
  int briDifference = currentBri - targetBri;
  if (briDifference > 7) currentBri = currentBri - 7;
  else if (briDifference < -7) currentBri = currentBri + 7;
  else currentBri = targetBri;
}

void FastBeatLights()
{
  AdjustBrightness();
  //figure out a good brightness increase
  int beatBri = (int)(currentBri * 1.5f);
  if (beatBri > 255) beatBri = 255;
  //transition the color immediately
  UpdateLights(beatBri, 0, 0);
  //fade brightness
  UpdateLights(5, 0, 10); //fade
}

void SlowBeatLights()
{
  AdjustBrightness();
  //figure out a good brightness increase
  int beatBri = (int)(currentBri * 1.25f);
  if (beatBri > 255) beatBri = 255;
  //transition the color immediately
  UpdateLights(beatBri, 0, 2);
  //fade brightness
  UpdateLights(5, 0, 8); //fade
}

void CycleHue(int huePoints)
{
  int hueGap;
  if ((lastHue - targetHue) > 0) hueGap = lastHue - targetHue;
  else hueGap = (lastHue - targetHue) * -1;
  if (hueGap > huePoints)
  {
    if (lastHue > targetHue) lastHue = lastHue - huePoints;
    else lastHue = lastHue + huePoints;
  }
  else
  {
    lastHue = targetHue;
    targetHue = initialHue;
    initialHue = lastHue;
  }
  //for the waveform to match the lights
  hsvToRgb(((float)lastHue / 65535.0f), 1.0f, 1.0f, rgb);
}

void CycleLights()
{
  //this is called once per second if no beats are detected
  CycleHue(3000);
  AdjustBrightness();
  UpdateLights(currentBri, 0, 10);
}


//taken from Vortex
float AdjustRateToFPS(float per_frame_decay_rate_at_fps1, float fps1, float actual_fps)
{
  // returns the equivalent per-frame decay rate at actual_fps

  // basically, do all your testing at fps1 and get a good decay rate;
  // then, in the real application, adjust that rate by the actual fps each time you use it.

  float per_second_decay_rate_at_fps1 = powf(per_frame_decay_rate_at_fps1, fps1);
  float per_frame_decay_rate_at_fps2 = powf(per_second_decay_rate_at_fps1, 1.0f / actual_fps);

  return per_frame_decay_rate_at_fps2;
}

//taken from Vortex
void AnalyzeSound()
{

  int m_fps = 60;

  // sum (left channel) spectrum up into 3 bands
  // [note: the new ranges do it so that the 3 bands are equally spaced, pitch-wise]
  float min_freq = 200.0f;
  float max_freq = 11025.0f;
  float net_octaves = (logf(max_freq / min_freq) / logf(2.0f));     // 5.7846348455575205777914165223593
  float octaves_per_band = net_octaves / 3.0f;                    // 1.9282116151858401925971388407864
  float mult = powf(2.0f, octaves_per_band); // each band's highest freq. divided by its lowest freq.; 3.805831305510122517035102576162
  // [to verify: min_freq * mult * mult * mult should equal max_freq.]
  //    for (int ch=0; ch<2; ch++)
  {
    for (int i = 0; i<3; i++)
    {
      // old guesswork code for this:
      //   float exp = 2.1f;
      //   int start = (int)(NUM_FREQUENCIES*0.5f*powf(i/3.0f, exp));
      //   int end   = (int)(NUM_FREQUENCIES*0.5f*powf((i+1)/3.0f, exp));
      // results:
      //          old range:      new range (ideal):
      //   bass:  0-1097          200-761
      //   mids:  1097-4705       761-2897
      //   treb:  4705-11025      2897-11025
      int start = (int)(NUM_FREQUENCIES * min_freq*powf(mult, (float)i) / 11025.0f);
      int end = (int)(NUM_FREQUENCIES * min_freq*powf(mult, (float)i + 1) / 11025.0f);
      if (start < 0) start = 0;
      if (end > NUM_FREQUENCIES) end = NUM_FREQUENCIES;

      g_sound.imm[0][i] = 0;
      for (int j = start; j<end; j++)
      {
        g_sound.imm[0][i] += g_sound.fSpectrum[0][j];
        g_sound.imm[0][i] += g_sound.fSpectrum[1][j];
      }
      g_sound.imm[0][i] /= (float)(end - start) * 2;
    }
  }



  // multiply by long-term, empirically-determined inverse averages:
  // (for a trial of 244 songs, 10 seconds each, somewhere in the 2nd or 3rd minute,
  //  the average levels were: 0.326781557	0.38087377	0.199888934
  for (int ch = 0; ch<2; ch++)
  {
    g_sound.imm[ch][0] /= 0.326781557f;//0.270f;   
    g_sound.imm[ch][1] /= 0.380873770f;//0.343f;   
    g_sound.imm[ch][2] /= 0.199888934f;//0.295f;   
  }

  // do temporal blending to create attenuated and super-attenuated versions
  for (int ch = 0; ch<2; ch++)
  {
    for (int i = 0; i<3; i++)
    {
      // g_sound.avg[i]
      {
        float avg_mix;
        if (g_sound.imm[ch][i] > g_sound.avg[ch][i])
          avg_mix = AdjustRateToFPS(0.2f, 14.0f, (float)m_fps);
        else
          avg_mix = AdjustRateToFPS(0.5f, 14.0f, (float)m_fps);
        //                if (g_sound.imm[ch][i] > g_sound.avg[ch][i])
        //                  avg_mix = 0.5f;
        //                else 
        //                  avg_mix = 0.8f;
        g_sound.avg[ch][i] = g_sound.avg[ch][i] * avg_mix + g_sound.imm[ch][i] * (1 - avg_mix);
      }

      {
        float med_mix = 0.91f;//0.800f + 0.11f*powf(t, 0.4f);    // primarily used for velocity_damping
        float long_mix = 0.96f;//0.800f + 0.16f*powf(t, 0.2f);    // primarily used for smoke plumes
        med_mix = AdjustRateToFPS(med_mix, 14.0f, (float)m_fps);
        long_mix = AdjustRateToFPS(long_mix, 14.0f, (float)m_fps);
        g_sound.med_avg[ch][i] = g_sound.med_avg[ch][i] * (med_mix)+g_sound.imm[ch][i] * (1 - med_mix);
        //                g_sound.long_avg[ch][i] = g_sound.long_avg[ch][i]*(long_mix) + g_sound.imm[ch][i]*(1-long_mix);
      }
    }
  }

  float newBass = ((g_sound.avg[0][0] - g_sound.med_avg[0][0]) / g_sound.med_avg[0][0]) * 2;
  float newMiddle = ((g_sound.avg[0][1] - g_sound.med_avg[0][1]) / g_sound.med_avg[0][1]) * 2;
  float newTreble = ((g_sound.avg[0][2] - g_sound.med_avg[0][2]) / g_sound.med_avg[0][2]) * 2;
  newBass = std::max(std::min(newBass, 1.0f), -1.0f);
  newMiddle = std::max(std::min(newMiddle, 1.0f), -1.0f);
  newTreble = std::max(std::min(newTreble, 1.0f), -1.0f);

  g_bassLast = g_bass;
  g_middleLast = g_middle;

  float avg_mix;
  if (newTreble > g_treble)
    avg_mix = 0.5f;
  else
    avg_mix = 0.5f;

  //dealing with NaN's in linux
  if (g_bass != g_bass) g_bass = 0;
  if (g_middle != g_middle) g_middle = 0;
  if (g_treble != g_treble) g_treble = 0;

  g_bass = g_bass*avg_mix + newBass*(1 - avg_mix);
  g_middle = g_middle*avg_mix + newMiddle*(1 - avg_mix);
  //g_treble = g_treble*avg_mix + newTreble*(1 - avg_mix);

  g_bass = std::max(std::min(g_bass, 1.0f), -1.0f);
  g_middle = std::max(std::min(g_middle, 1.0f), -1.0f);
  //g_treble = std::max(std::min(g_treble, 1.0f), -1.0f);

  if (g_middle < 0) g_middle = g_middle * -1.0f;
  if (g_bass < 0) g_bass = g_bass * -1.0f;

  if (((g_middle - g_middleLast) > beatThreshold ||
    (g_bass - g_bassLast > beatThreshold))
    && ((fAppTime - fLightTime) > 0.3f))
  {
    //beat
    FastBeatLights();
    CycleHue(1500);
    //changed lights
    fLightTime = fAppTime;
  }
}

void InitTime()
{
  // Save the start time
  clock_gettime(CLOCK_MONOTONIC, &systemClock);
  fTime = ((float)systemClock.tv_nsec / 1000000000.0) + (float)systemClock.tv_sec;

  fAppTime = 0;
  fElapsedTime = 0;
  fElapsedAppTime = 0;
  fLastTime = 0;
  fLightTime = 0;
  fUpdateTime = 0;

}

void UpdateTime()
{
  clock_gettime(CLOCK_MONOTONIC, &systemClock);
  fTime = ((float)systemClock.tv_nsec / 1000000000.0) + (float)systemClock.tv_sec;
  fElapsedTime = fTime - fLastTime;
  fLastTime = fTime;
  fAppTime += fElapsedTime;
  //fElapsedAppTime = fElapsedTime;

  // Keep track of the frame count
  iFrames++;

  //fBeatTime = 60.0f / (float)(bpm); //skip every other beat

  // If beats aren't doing anything then cycle colors nicely
  if (fAppTime - fLightTime > 1.5f)
  {
    CycleLights();
    fLightTime = fAppTime;
  }

  g_movingAvgMidSum = 0.0f;
  //update the max brightness based on the moving avg of the mid levels
  for (int i = 0; i<128; i++)
  {
    g_movingAvgMidSum += g_movingAvgMid[i];
    if (i != 127)
      g_movingAvgMid[i] = g_movingAvgMid[i + 1];
    else
      g_movingAvgMid[i] = (g_sound.avg[0][1] + g_sound.avg[1][1]) / 2.0f;
  }

  if ((g_movingAvgMidSum*1000.0f / 15.0f) < 0.5f &&
    (g_movingAvgMidSum*1000.0f / 15.0f) > 0.1f)
    targetBri = (int)(maxBri * 2 * g_movingAvgMidSum*1000.0f / 15.0f);
  else if (g_movingAvgMidSum*1000.0f / 15.0f > 0.5f)
    targetBri = maxBri;
  else if (g_movingAvgMidSum*1000.0f / 15.0f < 0.1f)
    targetBri = (int)(maxBri * 0.1f);

  // Update the scene stats once per second
  if (fAppTime - fUpdateTime > 1.0f)
  {
    fFPS = (float)(iFrames / (fAppTime - fLastTime));
    fUpdateTime = fAppTime;
    iFrames = 0;
  }
}


//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!props)
    return ADDON_STATUS_UNKNOWN;

  vis_shader = new CVisGUIShader(vert, frag);

  if (!vis_shader)
    return ADDON_STATUS_UNKNOWN;

  if (!vis_shader->CompileAndLink())
  {
    delete vis_shader;
    return ADDON_STATUS_UNKNOWN;
  }

  lightIDs.push_back("1");
  lightIDs.push_back("2");
  lightIDs.push_back("3");

  return ADDON_STATUS_NEED_SETTINGS;
}

extern "C" void Start(int iChannels, int iSamplesPerSec, int iBitsPerSample, const char* szSongName)
{
  //set Hue registration command
  const char json[] = "{\"devicetype\":\"Kodi\",\"username\":\"KodiVisWave\"}";

  //struct curl_slist *headers = NULL;
  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    //curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);

    // Set the URL that is about to receive our POST.
    curl_easy_setopt(curl, CURLOPT_URL, strURLRegistration.c_str());

    // Perform the request, res will get the return code
    res = curl_easy_perform(curl);

    // always cleanup (at the end)
  }

  //turn the lights on
  TurnLightsOn();
  UpdateLights(currentBri, 255, 30);

  //initialize the beat detection
  InitTime();
  g_fftobj.Init(576, NUM_FREQUENCIES);

  //initialize the moving average of mids
  for (int i = 0; i<15; i++)
  {
    g_movingAvgMid[i] = 0;
  }
}

//-- Audiodata ----------------------------------------------------------------
// Called by XBMC to pass new audio data to the vis
//-----------------------------------------------------------------------------
extern "C" void AudioData(const float* pAudioData, int iAudioDataLength, float *pFreqData, int iFreqDataLength)
{
  int ipos = 0;
  while (ipos < 512)
  {
    for (int i = 0; i < iAudioDataLength; i += 2)
    {
      g_fWaveform[0][ipos] = pAudioData[i]; // left channel
      g_fWaveform[1][ipos] = pAudioData[i + 1]; // right channel
      ipos++;
      if (ipos >= 512) break;
    }
  }

  //taken from Vortex
  float tempWave[2][576];

  int iPos = 0;
  int iOld = 0;
  //const float SCALE = (1.0f / 32768.0f ) * 255.0f;
  while (iPos < 576)
  {
    for (int i = 0; i < iAudioDataLength; i += 2)
    {
      g_sound.fWaveform[0][iPos] = float((pAudioData[i] / 32768.0f) * 255.0f);
      g_sound.fWaveform[1][iPos] = float((pAudioData[i + 1] / 32768.0f) * 255.0f);

      // damp the input into the FFT a bit, to reduce high-frequency noise:
      tempWave[0][iPos] = 0.5f * (g_sound.fWaveform[0][iPos] + g_sound.fWaveform[0][iOld]);
      tempWave[1][iPos] = 0.5f * (g_sound.fWaveform[1][iPos] + g_sound.fWaveform[1][iOld]);
      iOld = iPos;
      iPos++;
      if (iPos >= 576)
        break;
    }
  }

  g_fftobj.time_to_frequency_domain(tempWave[0], g_sound.fSpectrum[0]);
  g_fftobj.time_to_frequency_domain(tempWave[1], g_sound.fSpectrum[1]);
  AnalyzeSound();
}

//-- Render -------------------------------------------------------------------
// Called once per frame. Do all rendering here.
//-----------------------------------------------------------------------------
extern "C" void Render()
{
  if (useWaveForm) {
    GLfloat col[256][3];
    GLfloat ver[256][3];
    GLubyte idx[256];

    glDisable(GL_BLEND);

    vis_shader->MatrixMode(MM_PROJECTION);
    vis_shader->PushMatrix();
    vis_shader->LoadIdentity();
    //vis_shader->Frustum(-1.0f, 1.0f, -1.0f, 1.0f, 1.5f, 10.0f);
    vis_shader->MatrixMode(MM_MODELVIEW);
    vis_shader->PushMatrix();
    vis_shader->LoadIdentity();

    vis_shader->PushMatrix();
    vis_shader->Translatef(0.0f, 0.0f, -1.0f);
    vis_shader->Rotatef(0.0f, 1.0f, 0.0f, 0.0f);
    vis_shader->Rotatef(0.0f, 0.0f, 1.0f, 0.0f);
    vis_shader->Rotatef(0.0f, 0.0f, 0.0f, 1.0f);

    vis_shader->Enable();

    GLint   posLoc = vis_shader->GetPosLoc();
    GLint   colLoc = vis_shader->GetColLoc();

    glVertexAttribPointer(colLoc, 3, GL_FLOAT, 0, 0, col);
    glVertexAttribPointer(posLoc, 3, GL_FLOAT, 0, 0, ver);

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(colLoc);

    for (int i = 0; i < 256; i++)
    {
      col[i][0] = rgb[0];
      col[i][1] = rgb[1];
      col[i][2] = rgb[2];
      //ver[i][0] = g_viewport.X + ((i / 255.0f) * g_viewport.Width);
      //ver[i][1] = g_viewport.Y + g_viewport.Height * 0.33f + (g_fWaveform[0][i] * g_viewport.Height * 0.15f);
      ver[i][0] = -1.0f + ((i / 255.0f) * 2.0f);
      ver[i][1] = 0.5f + g_fWaveform[0][i];
      ver[i][2] = 1.0f;
      idx[i] = i;
    }

    glDrawElements(GL_LINE_STRIP, 256, GL_UNSIGNED_BYTE, idx);

    // Right channel
    for (int i = 0; i < 256; i++)
    {
      col[i][0] = rgb[0];
      col[i][1] = rgb[1];
      col[i][2] = rgb[2];
      //ver[i][0] = g_viewport.X + ((i / 255.0f) * g_viewport.Width);
      //ver[i][1] = g_viewport.Y + g_viewport.Height * 0.66f + (g_fWaveform[1][i] * g_viewport.Height * 0.15f);
      ver[i][0] = -1.0f + ((i / 255.0f) * 2.0f);
      ver[i][1] = -0.5f + g_fWaveform[1][i];
      ver[i][2] = 1.0f;
      idx[i] = i;

    }

    glDrawElements(GL_LINE_STRIP, 256, GL_UNSIGNED_BYTE, idx);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(colLoc);

    vis_shader->Disable();

    vis_shader->PopMatrix();

    vis_shader->PopMatrix();
    vis_shader->MatrixMode(MM_PROJECTION);
    vis_shader->PopMatrix();

    glEnable(GL_BLEND);
  }
  //get some interesting numbers to play with
  UpdateTime();
  g_timePass = fElapsedAppTime;

}

//-- GetInfo ------------------------------------------------------------------
// Tell XBMC our requirements
//-----------------------------------------------------------------------------
extern "C" void GetInfo(VIS_INFO* pInfo)
{
  pInfo->bWantsFreq = false;
  pInfo->iSyncDelay = 0;
}

//-- GetSubModules ------------------------------------------------------------
// Return any sub modules supported by this vis
//-----------------------------------------------------------------------------
extern "C" unsigned int GetSubModules(char ***names)
{
  return 0; // this vis supports 0 sub modules
}

//-- OnAction -----------------------------------------------------------------
// Handle XBMC actions such as next preset, lock preset, album art changed etc
//-----------------------------------------------------------------------------
extern "C" bool OnAction(long flags, const void *param)
{
  bool ret = false;
  return ret;
}

//-- GetPresets ---------------------------------------------------------------
// Return a list of presets to XBMC for display
//-----------------------------------------------------------------------------
extern "C" unsigned int GetPresets(char ***presets)
{
  return 0;
}

//-- GetPreset ----------------------------------------------------------------
// Return the index of the current playing preset
//-----------------------------------------------------------------------------
extern "C" unsigned GetPreset()
{
  return 0;
}

//-- IsLocked -----------------------------------------------------------------
// Returns true if this add-on use settings
//-----------------------------------------------------------------------------
extern "C" bool IsLocked()
{
  return true;
}

//-- Stop ---------------------------------------------------------------------
// This dll must stop all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" void ADDON_Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" void ADDON_Destroy()
{
  if (vis_shader)
  {
    vis_shader->Free();
    delete vis_shader;
    vis_shader = NULL;
  }

  TurnLightsOff();
  g_fftobj.CleanUp();
  // always cleanup 
  curl_easy_cleanup(curl);
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" bool ADDON_HasSettings()
{
  return true;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

extern "C" void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  if (!strSetting || !value)
    return ADDON_STATUS_UNKNOWN;


  if (strcmp(strSetting, "UseWaveForm") == 0)
    useWaveForm = *(bool*)value == 1;
  else if (strcmp(strSetting, "NamesOfLights") == 0)
  {
    char* array;
    array = (char*)value;
    std::string lightIDsUnsplit = std::string(array);
    lightIDs.clear();
    std::string delimiter = ",";
    int last = 0;
    int next = 0;
    while ((next = lightIDsUnsplit.find(delimiter, last)) != std::string::npos)
    {
      lightIDs.push_back(lightIDsUnsplit.substr(last, next - last));
      last = next + 1;
    }
    //do the last light token
    lightIDs.push_back(lightIDsUnsplit.substr(last));
    numberOfLights = lightIDs.size();
  }
  else if (strcmp(strSetting, "HueBridgeIP") == 0)
  {
    char* array;
    array = (char*)value;
    strHueBridgeIPAddress = std::string(array);
    strURLRegistration = "http://" + strHueBridgeIPAddress + "/api";
  }
  else if (strcmp(strSetting, "BeatThreshold") == 0)
    beatThreshold = *(float*)value;
  else if (strcmp(strSetting, "MaxBri") == 0)
    maxBri = *(int*)value;
  else if (strcmp(strSetting, "HueRangeUpper") == 0)
  {
    lastHue = *(int*)value;
    initialHue = lastHue;
  }
  else if (strcmp(strSetting, "HueRangeLower") == 0)
    targetHue = *(int*)value;
  else
    return ADDON_STATUS_UNKNOWN;

  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
extern "C" void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}
