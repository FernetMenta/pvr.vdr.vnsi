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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "VNSIAdmin.h"
#include "responsepacket.h"
#include "requestpacket.h"
#include "vnsicommand.h"
#include <queue>
#include <stdio.h>

#if defined(HAS_DX)
#include "D3D9.h"
#include "D3DX9.h"
#elif defined(HAS_GL) || defined(HAS_GLES2)
#include "shaders/GUIShader.h"
#define BUFFER_OFFSET(i) ((char *)NULL + (i))
#endif  // defined(HAS_GLES2)

#if !defined(GL_UNPACK_ROW_LENGTH)
  #if defined(GL_UNPACK_ROW_LENGTH_EXT)
    #define GL_UNPACK_ROW_LENGTH GL_UNPACK_ROW_LENGTH_EXT
    #define GL_UNPACK_SKIP_ROWS GL_UNPACK_SKIP_ROWS_EXT
    #define GL_UNPACK_SKIP_PIXELS GL_UNPACK_SKIP_PIXELS_EXT
  #else
    #undef HAS_GLES2
  #endif
#endif

#define CONTROL_RENDER_ADDON                  9
#define CONTROL_MENU                         10
#define CONTROL_OSD_BUTTON                   13
#define CONTROL_SPIN_TIMESHIFT_MODE          21
#define CONTROL_SPIN_TIMESHIFT_BUFFER_RAM    22
#define CONTROL_SPIN_TIMESHIFT_BUFFER_FILE   23
#define CONTROL_LABEL_FILTERS                31
#define CONTROL_RADIO_ISRADIO                32
#define CONTROL_PROVIDERS_BUTTON             33
#define CONTROL_CHANNELS_BUTTON              34
#define CONTROL_FILTERSAVE_BUTTON            35
#define CONTROL_ITEM_LIST                    36

#define ACTION_NONE                    0
#define ACTION_MOVE_LEFT               1
#define ACTION_MOVE_RIGHT              2
#define ACTION_MOVE_UP                 3
#define ACTION_MOVE_DOWN               4
#define ACTION_SELECT_ITEM             7
#define ACTION_PREVIOUS_MENU          10
#define ACTION_SHOW_INFO              11

#define REMOTE_0                    58  // remote keys 0-9. are used by multiple windows
#define REMOTE_1                    59  // for example in videoFullScreen.xml window id=2005 you can
#define REMOTE_2                    60  // enter time (mmss) to jump to particular point in the movie
#define REMOTE_3                    61
#define REMOTE_4                    62  // with spincontrols you can enter 3digit number to quickly set
#define REMOTE_5                    63  // spincontrol to desired value
#define REMOTE_6                    64
#define REMOTE_7                    65
#define REMOTE_8                    66
#define REMOTE_9                    67
#define ACTION_NAV_BACK             92

#define ACTION_TELETEXT_RED           215 // Teletext Color buttons to control TopText
#define ACTION_TELETEXT_GREEN         216 //    "       "      "    "     "       "
#define ACTION_TELETEXT_YELLOW        217 //    "       "      "    "     "       "
#define ACTION_TELETEXT_BLUE          218 //    "       "      "    "     "       "


using namespace ADDON;
using namespace P8PLATFORM;


class cOSDTexture
{
public:
  cOSDTexture(int bpp, int x0, int y0, int x1, int y1);
  virtual ~cOSDTexture();
  void SetPalette(int numColors, uint32_t *colors);
  void SetBlock(int x0, int y0, int x1, int y1, int stride, void *data, int len);
  void Clear();
  void GetSize(int &width, int &height);
  void GetOrigin(int &x0, int &y0) { x0 = m_x0; y0 = m_y0;};
  bool IsDirty(int &x0, int &y0, int &x1, int &y1);
  void *GetBuffer() {return (void*)m_buffer;};
protected:
  int m_x0, m_x1, m_y0, m_y1;
  int m_dirtyX0, m_dirtyX1, m_dirtyY0, m_dirtyY1;
  int m_bpp;
  int m_numColors;
  uint32_t m_palette[256];
  uint8_t *m_buffer;
  bool m_dirty;
};

cOSDTexture::cOSDTexture(int bpp, int x0, int y0, int x1, int y1)
{
  m_bpp = bpp;
  m_x0 = x0;
  m_x1 = x1;
  m_y0 = y0;
  m_y1 = y1;
  m_buffer = new uint8_t[(x1-x0+1)*(y1-y0+1)*sizeof(uint32_t)];
  memset(m_buffer,0, (x1-x0+1)*(y1-y0+1)*sizeof(uint32_t));
  m_dirtyX0 = m_dirtyY0 = 0;
  m_dirtyX1 = x1 - x0;
  m_dirtyY1 = y1 - y0;
  m_dirty = false;
}

cOSDTexture::~cOSDTexture()
{
  delete [] m_buffer;
}

void cOSDTexture::Clear()
{
  memset(m_buffer,0, (m_x1-m_x0+1)*(m_y1-m_y0+1)*sizeof(uint32_t));
  m_dirtyX0 = m_dirtyY0 = 0;
  m_dirtyX1 = m_x1 - m_x0;
  m_dirtyY1 = m_y1 - m_y0;
  m_dirty = false;
}

void cOSDTexture::SetBlock(int x0, int y0, int x1, int y1, int stride, void *data, int len)
{
  int line = y0;
  int col;
  int color;
  int width = m_x1 - m_x0 + 1;
  uint8_t *dataPtr = (uint8_t*)data;
  int pos = 0;
  uint32_t *buffer = (uint32_t*)m_buffer;
  while (line <= y1)
  {
    int lastPos = pos;
    col = x0;
    int offset = line*width;
    while (col <= x1)
    {
      if (pos >= len)
      {
        XBMC->Log(LOG_ERROR, "cOSDTexture::SetBlock: reached unexpected end of buffer");
        return;
      }
      color = dataPtr[pos];
      if (m_bpp == 8)
      {
        buffer[offset+col] = m_palette[color];
      }
      else if (m_bpp == 4)
      {
        buffer[offset+col] = m_palette[color & 0x0F];
      }
      else if (m_bpp == 2)
      {
        buffer[offset+col] = m_palette[color & 0x03];
      }
      else if (m_bpp == 1)
      {
        buffer[offset+col] = m_palette[color & 0x01];
      }
      pos++;
      col++;
    }
    line++;
    pos = lastPos + stride;
  }
  if (x0 < m_dirtyX0) m_dirtyX0 = x0;
  if (x1 > m_dirtyX1) m_dirtyX1 = x1;
  if (y0 < m_dirtyY0) m_dirtyY0 = y0;
  if (y1 > m_dirtyY1) m_dirtyY1 = y1;
  m_dirty = true;
}

void cOSDTexture::SetPalette(int numColors, uint32_t *colors)
{
  m_numColors = numColors;
  for (int i=0; i<m_numColors; i++)
  {
    // convert from ARGB to RGBA
    m_palette[i] = ((colors[i] & 0xFF000000)) | ((colors[i] & 0x00FF0000) >> 16) | ((colors[i] & 0x0000FF00)) | ((colors[i] & 0x000000FF) << 16);
  }
}

void cOSDTexture::GetSize(int &width, int &height)
{
  width = m_x1 - m_x0 + 1;
  height = m_y1 - m_y0 + 1;
}

bool cOSDTexture::IsDirty(int &x0, int &y0, int &x1, int &y1)
{
  bool ret = m_dirty;
  x0 = m_dirtyX0;
  x1 = m_dirtyX1;
  y0 = m_dirtyY0;
  y1 = m_dirtyY1;
  m_dirty = false;
  return ret;
}
//-----------------------------------------------------------------------------
#define MAX_TEXTURES 16

class cOSDRender
{
public:
  cOSDRender();
  virtual ~cOSDRender();
  void SetOSDSize(int width, int height) {m_osdWidth = width; m_osdHeight = height;};
  void SetControlSize(int width, int height) {m_controlWidth = width; m_controlHeight = height;};
  void AddTexture(int wndId, int bpp, int x0, int y0, int x1, int y1, int reset);
  void SetPalette(int wndId, int numColors, uint32_t *colors);
  void SetBlock(int wndId, int x0, int y0, int x1, int y1, int stride, void *data, int len);
  void Clear(int wndId);
  virtual void DisposeTexture(int wndId);
  virtual void FreeResources();
  virtual void Render() {};
  virtual void SetDevice(void *device) {};
  virtual bool Init() { return true; };
protected:
  cOSDTexture *m_osdTextures[MAX_TEXTURES];
  std::queue<cOSDTexture*> m_disposedTextures;
  int m_osdWidth, m_osdHeight;
  int m_controlWidth, m_controlHeight;
};

cOSDRender::cOSDRender()
{
  for (int i = 0; i < MAX_TEXTURES; i++)
    m_osdTextures[i] = 0;
}

cOSDRender::~cOSDRender()
{
  for (int i = 0; i < MAX_TEXTURES; i++)
  {
    DisposeTexture(i);
  }
  FreeResources();
}

void cOSDRender::DisposeTexture(int wndId)
{
  if (m_osdTextures[wndId])
  {
    m_disposedTextures.push(m_osdTextures[wndId]);
    m_osdTextures[wndId] = 0;
  }
}

void cOSDRender::FreeResources()
{
  while (!m_disposedTextures.empty())
  {
    delete m_disposedTextures.front();
    m_disposedTextures.pop();
  }
}

void cOSDRender::AddTexture(int wndId, int bpp, int x0, int y0, int x1, int y1, int reset)
{
  if (reset)
    DisposeTexture(wndId);
  if (!m_osdTextures[wndId])
    m_osdTextures[wndId] = new cOSDTexture(bpp, x0, y0, x1, y1);
}

void cOSDRender::Clear(int wndId)
{
  if (m_osdTextures[wndId])
    m_osdTextures[wndId]->Clear();
}

void cOSDRender::SetPalette(int wndId, int numColors, uint32_t *colors)
{
  if (m_osdTextures[wndId])
    m_osdTextures[wndId]->SetPalette(numColors, colors);
}

void cOSDRender::SetBlock(int wndId, int x0, int y0, int x1, int y1, int stride, void *data, int len)
{
  if (m_osdTextures[wndId])
    m_osdTextures[wndId]->SetBlock(x0, y0, x1, y1, stride, data, len);
}

#if defined(HAS_GL) || defined(HAS_GLES2)
class ATTRIBUTE_HIDDEN cOSDRenderGL : public cOSDRender
{
public:
  cOSDRenderGL();
  virtual ~cOSDRenderGL();
  void DisposeTexture(int wndId) override;
  void FreeResources() override;
  void Render() override;
  bool Init() override;
protected:
  GLuint m_hwTextures[MAX_TEXTURES];
  std::queue<GLuint> m_disposedHwTextures;
  CGUIShader *m_shader = nullptr;
};

cOSDRenderGL::cOSDRenderGL()
{
  for (int i = 0; i < MAX_TEXTURES; i++)
    m_hwTextures[i] = 0;
}

cOSDRenderGL::~cOSDRenderGL()
{
  for (int i = 0; i < MAX_TEXTURES; i++)
  {
    DisposeTexture(i);
  }
  FreeResources();
  delete m_shader;
  m_shader = nullptr;
}

bool cOSDRenderGL::Init()
{
  m_shader = new CGUIShader("vert.glsl", "frag.glsl");

  if(!m_shader->CompileAndLink())
  {
    delete m_shader;
    m_shader = nullptr;
    return false;
  }
  return true;
}

void cOSDRenderGL::DisposeTexture(int wndId)
{
  if (m_hwTextures[wndId])
  {
    m_disposedHwTextures.push(m_hwTextures[wndId]);
    m_hwTextures[wndId] = 0;
  }
  cOSDRender::DisposeTexture(wndId);
}

void cOSDRenderGL::FreeResources()
{
  while (!m_disposedHwTextures.empty())
  {
    if (glIsTexture(m_disposedHwTextures.front()))
    {
      glFinish();
      glDeleteTextures(1, &m_disposedHwTextures.front());
      m_disposedHwTextures.pop();
    }
  }
  cOSDRender::FreeResources();
}

void cOSDRenderGL::Render()
{
  m_shader->MatrixMode(MM_MODELVIEW);
  m_shader->LoadIdentity();
  m_shader->MatrixMode(MM_PROJECTION);
  m_shader->LoadIdentity();

  glDisable(GL_BLEND);

  //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  for (int i = 0; i < MAX_TEXTURES; i++)
  {
    int width, height, offsetX, offsetY;
    int x0,x1,y0,y1;
    bool dirty;

    if (m_osdTextures[i] == 0)
      continue;

    m_osdTextures[i]->GetSize(width, height);
    m_osdTextures[i]->GetOrigin(offsetX, offsetY);
    dirty = m_osdTextures[i]->IsDirty(x0,y0,x1,y1);

    // create gl texture
    if (dirty && !glIsTexture(m_hwTextures[i]))
    {
      glGenTextures(1, &m_hwTextures[i]);
      glBindTexture(GL_TEXTURE_2D, m_hwTextures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_osdTextures[i]->GetBuffer());
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    // update texture
    else if (dirty)
    {
      glBindTexture(GL_TEXTURE_2D, m_hwTextures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
      glPixelStorei(GL_UNPACK_SKIP_PIXELS, x0);
      glPixelStorei(GL_UNPACK_SKIP_ROWS, y0);
      glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1-x0+1, y1-y0+1, GL_RGBA, GL_UNSIGNED_BYTE, m_osdTextures[i]->GetBuffer());
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    // render texture

    // calculate ndc for OSD texture
    float destX0 = (float)offsetX*2/m_osdWidth -1;
    float destX1 = (float)(offsetX+width)*2/m_osdWidth -1;
    float destY0 = (float)offsetY*2/m_osdHeight -1;
    float destY1 = (float)(offsetY+height)*2/m_osdHeight -1;
    float aspectControl = (float)m_controlWidth/m_controlHeight;
    float aspectOSD = (float)m_osdWidth/m_osdHeight;
    if (aspectOSD > aspectControl)
    {
      destY0 *= aspectControl/aspectOSD;
      destY1 *= aspectControl/aspectOSD;
    }
    else if (aspectOSD < aspectControl)
    {
      destX0 *= aspectOSD/aspectControl;
      destX1 *= aspectOSD/aspectControl;
    }

    // y inveted
    destY0 *= -1;
    destY1 *= -1;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hwTextures[i]);

#if defined(HAS_GL)

    GLubyte idx[4] = {0, 1, 3, 2};
    GLuint vertexVBO;
    GLuint indexVBO;
    struct PackedVertex
    {
      float x, y, z;
      float u1, v1;
    }vertex[4];

    m_shader->Enable();

    GLint posLoc = m_shader->GetPosLoc();
    GLint texLoc = m_shader->GetCordLoc();

    vertex[0].x = destX0;
    vertex[0].y = destY0;
    vertex[0].z = 0.0f;
    vertex[0].u1 = 0.0f;
    vertex[0].v1 = 0.0f;

    vertex[1].x = destX1;
    vertex[1].y = destY0;
    vertex[1].z = 0.0f;
    vertex[1].u1 = 1.0f;
    vertex[1].v1 = 0.0f;

    vertex[2].x = destX1;
    vertex[2].y = destY1;
    vertex[2].z = 0.0f;
    vertex[2].u1 = 1.0f;
    vertex[2].v1 = 1.0f;

    vertex[3].x = destX0;
    vertex[3].y = destY1;
    vertex[3].z = 0.0f;
    vertex[3].u1 = 0.0f;
    vertex[3].v1 = 1.0f;

    glGenBuffers(1, &vertexVBO);
    glBindBuffer(GL_ARRAY_BUFFER, vertexVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(PackedVertex)*4, &vertex[0], GL_STATIC_DRAW);

    glVertexAttribPointer(posLoc,  3, GL_FLOAT, 0, sizeof(PackedVertex), BUFFER_OFFSET(offsetof(PackedVertex, x)));
    glEnableVertexAttribArray(posLoc);

    glVertexAttribPointer(texLoc, 2, GL_FLOAT, 0, sizeof(PackedVertex), BUFFER_OFFSET(offsetof(PackedVertex, u1)));
    glEnableVertexAttribArray(texLoc);

    glGenBuffers(1, &indexVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexVBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLubyte)*4, idx, GL_STATIC_DRAW);

    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vertexVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &indexVBO);

    m_shader->Disable();

#elif defined(HAS_GLES2)

    GLubyte idx[4] = {0, 1, 3, 2};        //determines order of triangle strip
    GLfloat ver[4][4];
    GLfloat tex[4][2];

    m_shader->Enable();
    GLint   posLoc = m_shader->GetPosLoc();
    GLint   texLoc = m_shader->GetCordLoc();

    glVertexAttribPointer(posLoc, 4, GL_FLOAT, 0, 0, ver);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, 0, 0, tex);

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(texLoc);

    // Set vertex coordinates
    for(int i = 0; i < 4; i++)
    {
      ver[i][2] = 0.0f;// set z to 0
      ver[i][3] = 1.0f;
    }
    ver[0][0] = ver[3][0] = destX0;
    ver[0][1] = ver[1][1] = destY0;
    ver[1][0] = ver[2][0] = destX1;
    ver[2][1] = ver[3][1] = destY1;

    // Set texture coordinates
    tex[0][0] = tex[3][0] = 0;
    tex[0][1] = tex[1][1] = 0;
    tex[1][0] = tex[2][0] = 1;
    tex[2][1] = tex[3][1] = 1;

    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);

    m_shader->Disable();
#endif

    glBindTexture(GL_TEXTURE_2D, 0);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
}
#endif

#if defined(HAS_DX)
class cOSDRenderDX : public cOSDRender
{
public:
  cOSDRenderDX();
  virtual ~cOSDRenderDX();
  void DisposeTexture(int wndId) override;
  void FreeResources() override;
  void Render() override;
  void SetDevice(void *device) override { m_device = (LPDIRECT3DDEVICE9)device; };
protected:
  LPDIRECT3DDEVICE9 m_device;
  LPDIRECT3DTEXTURE9 m_hwTextures[MAX_TEXTURES];
  std::queue<LPDIRECT3DTEXTURE9> m_disposedHwTextures;
};

cOSDRenderDX::cOSDRenderDX()
{
  for (int i = 0; i < MAX_TEXTURES; i++)
    m_hwTextures[i] = 0;
}

cOSDRenderDX::~cOSDRenderDX()
{
  for (int i = 0; i < MAX_TEXTURES; i++)
  {
    DisposeTexture(i);
  }
  FreeResources();
}

void cOSDRenderDX::DisposeTexture(int wndId)
{
  if (m_hwTextures[wndId])
  {
    m_disposedHwTextures.push(m_hwTextures[wndId]);
    m_hwTextures[wndId] = 0;
  }
  cOSDRender::DisposeTexture(wndId);
}

void cOSDRenderDX::FreeResources()
{
  while (!m_disposedHwTextures.empty())
  {
    if (m_disposedHwTextures.front())
    {
      m_disposedHwTextures.front()->Release();
      m_disposedHwTextures.pop();
    }
  }
  cOSDRender::FreeResources();
}

void cOSDRenderDX::Render()
{
  m_device->Clear(0, NULL, D3DCLEAR_ZBUFFER, D3DXCOLOR(0.0f, 0.0f, 0.0f, 0.0f), 1.0f, 0);

  D3DXMATRIX matProjection;
  D3DXMatrixIdentity(&matProjection);
  m_device->SetTransform(D3DTS_PROJECTION, &matProjection);

  D3DXMATRIX matView;
  D3DXMatrixIdentity(&matView);
  m_device->SetTransform(D3DTS_VIEW, &matView);

  D3DXMATRIX matWorld;
  D3DXMatrixIdentity(&matWorld);
  m_device->SetTransform(D3DTS_WORLD, &matWorld);

  for (int i = 0; i < MAX_TEXTURES; i++)
  {
    int width, height, offsetX, offsetY;
    int x0,x1,y0,y1;
    bool dirty;

    if (m_osdTextures[i] == 0)
      continue;

    m_osdTextures[i]->GetSize(width, height);
    m_osdTextures[i]->GetOrigin(offsetX, offsetY);
    dirty = m_osdTextures[i]->IsDirty(x0,y0,x1,y1);

    // create texture
    if (dirty && !m_hwTextures[i])
    {
      HRESULT hr = m_device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_hwTextures[i], NULL);
	    if (hr != D3D_OK)
	    {
	      XBMC->Log(LOG_ERROR,"%s - failed to create texture", __FUNCTION__);
        continue;
      }
    }
    // update texture
    if (dirty)
    {
      D3DLOCKED_RECT lockedRect;
      RECT dirtyRect;
      dirtyRect.bottom = y1;
      dirtyRect.left = x0;
      dirtyRect.top = y0;
      dirtyRect.right = x1;
      HRESULT hr = m_hwTextures[i]->LockRect(0, &lockedRect, &dirtyRect, 0);
      if (hr != D3D_OK)
	    {
	      XBMC->Log(LOG_ERROR,"%s - failed to lock texture", __FUNCTION__);
        continue;
      }
      uint8_t *source = (uint8_t*)m_osdTextures[i]->GetBuffer();
      uint8_t *dest = (uint8_t*)lockedRect.pBits;
      for(int y=y0; y<=y1; y++)
      {
        for(int x=x0; x<=x1; x++)
        {
          dest[y*lockedRect.Pitch+x*4] = source[y*width*4+x*4+2];  // blue
          dest[y*lockedRect.Pitch+x*4+1] = source[y*width*4+x*4+1];  // green
          dest[y*lockedRect.Pitch+x*4+2] = source[y*width*4+x*4];    // red
          dest[y*lockedRect.Pitch+x*4+3] = source[y*width*4+x*4+3];  // alpha
        }
      }
      m_hwTextures[i]->UnlockRect(0);
      if (hr != D3D_OK)
	    {
	      XBMC->Log(LOG_ERROR,"%s - failed to unlock texture", __FUNCTION__);
        continue;
      }
    }

    // render texture

    // calculate ndc for OSD texture
    float destX0 = (float)offsetX*2/m_osdWidth -1;
    float destX1 = (float)(offsetX+width)*2/m_osdWidth -1;
    float destY0 = (float)offsetY*2/m_osdHeight -1;
    float destY1 = (float)(offsetY+height)*2/m_osdHeight -1;
    float aspectControl = (float)m_controlWidth/m_controlHeight;
    float aspectOSD = (float)m_osdWidth/m_osdHeight;
    if (aspectOSD > aspectControl)
    {
      destY0 *= aspectControl/aspectOSD;
      destY1 *= aspectControl/aspectOSD;
    }
    else if (aspectOSD < aspectControl)
    {
      destX0 *= aspectOSD/aspectControl;
      destX1 *= aspectOSD/aspectControl;
    }

    // y inveted
    destY0 *= -1;
    destY1 *= -1;

    struct VERTEX
    {
      FLOAT x,y,z;
      DWORD color;
      FLOAT tu, tv;
    };

    VERTEX vertex[] =
    {
		  { destX0, destY0, 0.0f, 0xffffffff, 0.0f, 0.0f },
		  { destX0, destY1, 0.0f, 0xffffffff, 0.0f, 1.0f },
		  { destX1, destY1, 0.0f, 0xffffffff, 1.0f, 1.0f },
		  { destX1, destY0, 0.0f, 0xffffffff, 1.0f, 0.0f },
    };

    m_device->SetTexture(0, m_hwTextures[i]);
    HRESULT hr;
    hr = m_device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
    hr = m_device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    hr = m_device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
    hr = m_device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    hr = m_device->SetTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
    hr = m_device->SetTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

    hr = m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    hr = m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    hr = m_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    hr = m_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    hr = m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    hr = m_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    hr = m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    hr = m_device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED); 

    hr = m_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    hr = m_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    hr = m_device->SetPixelShader(NULL);

    hr = m_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    hr = m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, vertex, sizeof(VERTEX));
    if (hr != D3D_OK)
	  {
	    XBMC->Log(LOG_ERROR,"%s - failed to render texture", __FUNCTION__);
    }
    m_device->SetTexture(0, NULL);
  }
}
#endif


//-----------------------------------------------------------------------------
cVNSIAdmin::cVNSIAdmin()
{
}

cVNSIAdmin::~cVNSIAdmin()
{

}

bool cVNSIAdmin::Open(const std::string& hostname, int port, const char* name)
{
  if(!cVNSIData::Open(hostname, port, name))
    return false;

  if(!cVNSIData::Login())
    return false;

  m_bIsOsdControl = false;
#if defined(HAS_GL) || defined(HAS_GLES2)
  m_osdRender = new cOSDRenderGL();
#elif defined(HAS_DX)
  m_osdRender = new cOSDRenderDX();
#else
  m_osdRender = new cOSDRender();
#endif

  if(!m_osdRender->Init())
  {
    delete m_osdRender;
    m_osdRender = NULL;
    return false;
  }

  m_abort = false;
  m_connectionLost = false;
  CreateThread();

  if (!ConnectOSD())
    return false;

  // Load the Window as Dialog
  m_window = GUI->Window_create("Admin.xml", "skin.estuary", false, true);
  m_window->m_cbhdl   = this;
  m_window->CBOnInit  = OnInitCB;
  m_window->CBOnFocus = OnFocusCB;
  m_window->CBOnClick = OnClickCB;
  m_window->CBOnAction= OnActionCB;

  m_window->DoModal();

  ClearListItems();
  m_window->ClearProperties();

  GUI->Control_releaseRendering(m_renderControl);
  GUI->Control_releaseSpin(m_spinTimeshiftMode);
  GUI->Control_releaseSpin(m_spinTimeshiftBufferRam);
  GUI->Control_releaseSpin(m_spinTimeshiftBufferFile);
  GUI->Control_releaseRadioButton(m_ratioIsRadio);
  GUI->Window_destroy(m_window);
  StopThread();
  Close();

  if (m_osdRender)
  {
    delete m_osdRender;
    m_osdRender = NULL;
  }

  return true;
}

bool cVNSIAdmin::OnClick(int controlId)
{
  if (controlId == CONTROL_SPIN_TIMESHIFT_MODE)
  {
    int value = m_spinTimeshiftMode->GetValue();
    cRequestPacket vrp;
    vrp.init(VNSI_STORESETUP);
    vrp.add_String(CONFNAME_TIMESHIFT);
    vrp.add_U32(value);
    if (!ReadSuccess(&vrp))
    {
      XBMC->Log(LOG_ERROR, "%s - failed to set timeshift mode", __FUNCTION__);
    }
    return true;
  }
  else if (controlId == CONTROL_SPIN_TIMESHIFT_BUFFER_RAM)
  {
    int value = m_spinTimeshiftBufferRam->GetValue();
    cRequestPacket vrp;
    vrp.init(VNSI_STORESETUP);
    vrp.add_String(CONFNAME_TIMESHIFTBUFFERSIZE);
    vrp.add_U32(value);
    if (!ReadSuccess(&vrp))
    {
      XBMC->Log(LOG_ERROR, "%s - failed to set timeshift buffer", __FUNCTION__);
    }
    return true;
  }
  else if (controlId == CONTROL_SPIN_TIMESHIFT_BUFFER_FILE)
  {
    int value = m_spinTimeshiftBufferFile->GetValue();
    cRequestPacket vrp;
    vrp.init(VNSI_STORESETUP);
    vrp.add_String(CONFNAME_TIMESHIFTBUFFERFILESIZE);
    vrp.add_U32(value);
    if (!ReadSuccess(&vrp))
    {
      XBMC->Log(LOG_ERROR, "%s - failed to set timeshift buffer file", __FUNCTION__);
    }
    return true;
  }
  else if (controlId == CONTROL_PROVIDERS_BUTTON)
  {
    if(!m_channels.m_loaded || m_ratioIsRadio->IsSelected() != m_channels.m_radio)
    {
      ReadChannelList(m_ratioIsRadio->IsSelected());
      ReadChannelWhitelist(m_ratioIsRadio->IsSelected());
      ReadChannelBlacklist(m_ratioIsRadio->IsSelected());
      m_channels.CreateProviders();
      m_channels.LoadProviderWhitelist();
      m_channels.LoadChannelBlacklist();
      m_channels.m_loaded = true;
      m_channels.m_radio = m_ratioIsRadio->IsSelected();
      m_window->SetProperty("IsDirty", "0");
    }
    LoadListItemsProviders();
    m_channels.m_mode = CVNSIChannels::PROVIDER;
  }
  else if (controlId == CONTROL_CHANNELS_BUTTON)
  {
    if(!m_channels.m_loaded || m_ratioIsRadio->IsSelected() != m_channels.m_radio)
    {
      ReadChannelList(m_ratioIsRadio->IsSelected());
      ReadChannelWhitelist(m_ratioIsRadio->IsSelected());
      ReadChannelBlacklist(m_ratioIsRadio->IsSelected());
      m_channels.CreateProviders();
      m_channels.LoadProviderWhitelist();
      m_channels.LoadChannelBlacklist();
      m_channels.m_loaded = true;
      m_channels.m_radio = m_ratioIsRadio->IsSelected();
      m_window->SetProperty("IsDirty", "0");
    }
    LoadListItemsChannels();
    m_channels.m_mode = CVNSIChannels::CHANNEL;
  }
  else if (controlId == CONTROL_FILTERSAVE_BUTTON)
  {
    if(m_channels.m_loaded)
    {
      SaveChannelWhitelist(m_ratioIsRadio->IsSelected());
      SaveChannelBlacklist(m_ratioIsRadio->IsSelected());
      m_window->SetProperty("IsDirty", "0");
    }
  }
  else if (controlId == CONTROL_ITEM_LIST)
  {
    if(m_channels.m_mode == CVNSIChannels::PROVIDER)
    {
      int pos = m_window->GetCurrentListPosition();
      GUIHANDLE hdl = m_window->GetListItem(pos);
      int idx = m_listItemsMap[hdl];
      CAddonListItem *item = m_listItems[idx];
      if (m_channels.m_providers[idx].m_whitelist)
      {
        item->SetProperty("IsWhitelist", "false");
        m_channels.m_providers[idx].m_whitelist = false;
      }
      else
      {
        item->SetProperty("IsWhitelist", "true");
        m_channels.m_providers[idx].m_whitelist = true;
      }
      m_window->SetProperty("IsDirty", "1");
    }
    else if(m_channels.m_mode == CVNSIChannels::CHANNEL)
    {
      int pos = m_window->GetCurrentListPosition();
      GUIHANDLE hdl = m_window->GetListItem(pos);
      int idx = m_listItemsMap[hdl];
      CAddonListItem *item = m_listItems[idx];
      int channelidx = m_listItemsChannelsMap[hdl];
      if (m_channels.m_channels[channelidx].m_blacklist)
      {
        item->SetProperty("IsBlacklist", "false");
        m_channels.m_channels[channelidx].m_blacklist = false;
      }
      else
      {
        item->SetProperty("IsBlacklist", "true");
        m_channels.m_channels[channelidx].m_blacklist = true;
      }
      m_window->SetProperty("IsDirty", "1");
    }
  }
  return false;
}

bool cVNSIAdmin::OnFocus(int controlId)
{
  if (controlId == CONTROL_OSD_BUTTON)
  {
    m_window->SetControlLabel(CONTROL_OSD_BUTTON, XBMC->GetLocalizedString(30102));
    m_window->MarkDirtyRegion();
    m_bIsOsdControl = true;
    return true;
  }
  else if (m_bIsOsdControl)
  {
    m_window->SetControlLabel(CONTROL_OSD_BUTTON, XBMC->GetLocalizedString(30103));
    m_window->MarkDirtyRegion();
    m_bIsOsdControl = false;
    return true;
  }
  return false;
}

bool cVNSIAdmin::OnInit()
{
  m_renderControl = GUI->Control_getRendering(m_window, CONTROL_RENDER_ADDON);
  m_renderControl->m_cbhdl   = this;
  m_renderControl->CBCreate = CreateCB;
  m_renderControl->CBRender = RenderCB;
  m_renderControl->CBStop = StopCB;
  m_renderControl->CBDirty = DirtyCB;
  m_renderControl->Init();

  cRequestPacket vrp;
  vrp.init(VNSI_OSD_HITKEY);
  vrp.add_U32(0);
  cVNSISession::TransmitMessage(&vrp);

  // setup parameters
  m_spinTimeshiftMode = GUI->Control_getSpin(m_window, CONTROL_SPIN_TIMESHIFT_MODE);
  m_spinTimeshiftMode->Clear();
  m_spinTimeshiftMode->AddLabel("OFF", 0);
  m_spinTimeshiftMode->AddLabel("RAM", 1);
  m_spinTimeshiftMode->AddLabel("FILE", 2);

  {
    cRequestPacket vrp;
    vrp.init(VNSI_GETSETUP);
    vrp.add_String(CONFNAME_TIMESHIFT);
    auto resp = ReadResult(&vrp);
    if (!resp)
    {
      XBMC->Log(LOG_ERROR, "%s - failed to get timeshift mode", __FUNCTION__);
      return false;
    }
    int mode = resp->extract_U32();
    m_spinTimeshiftMode->SetValue(mode);
  }

  m_spinTimeshiftBufferRam = GUI->Control_getSpin(m_window, CONTROL_SPIN_TIMESHIFT_BUFFER_RAM);
  m_spinTimeshiftBufferRam->Clear();
  char buffer[8];
  for (int i = 1; i <= 80; i++)
  {
    sprintf(buffer, "%d", i);
    m_spinTimeshiftBufferRam->AddLabel(buffer, i);
  }

  {
    cRequestPacket vrp;
    vrp.init(VNSI_GETSETUP);
    vrp.add_String(CONFNAME_TIMESHIFTBUFFERSIZE);
    auto resp = ReadResult(&vrp);
    if (!resp)
    {
      XBMC->Log(LOG_ERROR, "%s - failed to get timeshift buffer size", __FUNCTION__);
      return false;
    }
    int mode = resp->extract_U32();
    m_spinTimeshiftBufferRam->SetValue(mode);
  }
  m_spinTimeshiftBufferFile = GUI->Control_getSpin(m_window, CONTROL_SPIN_TIMESHIFT_BUFFER_FILE);
  m_spinTimeshiftBufferFile->Clear();
  for (int i = 1; i <= 20; i++)
  {
    sprintf(buffer, "%d", i);
    m_spinTimeshiftBufferFile->AddLabel(buffer, i);
  }

  {
    cRequestPacket vrp;
    vrp.init(VNSI_GETSETUP);
    vrp.add_String(CONFNAME_TIMESHIFTBUFFERFILESIZE);
    auto resp = ReadResult(&vrp);
    if (!resp)
    {
      XBMC->Log(LOG_ERROR, "%s - failed to get timeshift buffer (file) size", __FUNCTION__);
      return false;
    }
    int mode = resp->extract_U32();
    m_spinTimeshiftBufferFile->SetValue(mode);
  }

  // channel filters
  m_ratioIsRadio = GUI->Control_getRadioButton(m_window, CONTROL_RADIO_ISRADIO);

  return true;
}

bool cVNSIAdmin::OnAction(int actionId)
{
  if (m_window->GetFocusId() != CONTROL_OSD_BUTTON && m_bIsOsdControl)
  {
    m_bIsOsdControl = false;
    m_window->SetControlLabel(CONTROL_OSD_BUTTON, XBMC->GetLocalizedString(30103));
    m_window->MarkDirtyRegion();
  }
  else if (m_window->GetFocusId() == CONTROL_OSD_BUTTON)
  {
    if (actionId == ACTION_SHOW_INFO)
    {
      m_window->SetFocusId(CONTROL_MENU);
      return true;
    }
    else if(IsVdrAction(actionId))
    {
      // send all actions to vdr
      cRequestPacket vrp;
      vrp.init(VNSI_OSD_HITKEY);
      vrp.add_U32(actionId);
      cVNSISession::TransmitMessage(&vrp);
      return true;
    }
  }

  if (actionId == ADDON_ACTION_CLOSE_DIALOG ||
      actionId == ADDON_ACTION_PREVIOUS_MENU ||
      actionId == ACTION_NAV_BACK)
  {
    m_window->Close();
    return true;
  }

  return false;
}

bool cVNSIAdmin::IsVdrAction(int action)
{
  if (action == ACTION_MOVE_LEFT ||
      action == ACTION_MOVE_RIGHT ||
      action == ACTION_MOVE_UP ||
      action == ACTION_MOVE_DOWN ||
      action == ACTION_SELECT_ITEM ||
      action == ACTION_PREVIOUS_MENU ||
      action == REMOTE_0 ||
      action == REMOTE_1 ||
      action == REMOTE_2 ||
      action == REMOTE_3 ||
      action == REMOTE_4 ||
      action == REMOTE_5 ||
      action == REMOTE_6 ||
      action == REMOTE_7 ||
      action == REMOTE_8 ||
      action == REMOTE_9 ||
      action == ACTION_NAV_BACK ||
      action == ACTION_TELETEXT_RED ||
      action == ACTION_TELETEXT_GREEN ||
      action == ACTION_TELETEXT_YELLOW ||
      action == ACTION_TELETEXT_BLUE)
    return true;
  else
    return false;
}

bool cVNSIAdmin::Create(int x, int y, int w, int h, void* device)
{
  if (m_osdRender)
  {
    m_osdRender->SetControlSize(w,h);
    m_osdRender->SetDevice(device);
  }
  return true;
}

void cVNSIAdmin::Render()
{
  const CLockObject lock(m_osdMutex);
  if (m_osdRender)
  {
    m_osdRender->Render();
    m_osdRender->FreeResources();
  }
  m_bIsOsdDirty = false;
}

void cVNSIAdmin::Stop()
{
  const CLockObject lock(m_osdMutex);
  if (m_osdRender)
  {
    delete m_osdRender;
    m_osdRender = NULL;
  }
}

bool cVNSIAdmin::Dirty()
{
  return m_bIsOsdDirty;
}

bool cVNSIAdmin::OnInitCB(GUIHANDLE cbhdl)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  try {
    return osd->OnInit();
  } catch (std::exception e) {
    XBMC->Log(LOG_ERROR, "%s - %s", __FUNCTION__, e.what());
    return false;
  }
}

bool cVNSIAdmin::OnClickCB(GUIHANDLE cbhdl, int controlId)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  try {
    return osd->OnClick(controlId);
  } catch (std::exception e) {
    XBMC->Log(LOG_ERROR, "%s - %s", __FUNCTION__, e.what());
    return false;
  }
}

bool cVNSIAdmin::OnFocusCB(GUIHANDLE cbhdl, int controlId)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  return osd->OnFocus(controlId);
}

bool cVNSIAdmin::OnActionCB(GUIHANDLE cbhdl, int actionId)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  try {
    return osd->OnAction(actionId);
  } catch (std::exception e) {
    XBMC->Log(LOG_ERROR, "%s - %s", __FUNCTION__, e.what());
    return false;
  }
}

bool cVNSIAdmin::CreateCB(GUIHANDLE cbhdl, int x, int y, int w, int h, void *device)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  return osd->Create(x, y, w, h, device);
}

void cVNSIAdmin::RenderCB(GUIHANDLE cbhdl)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  osd->Render();
}

void cVNSIAdmin::StopCB(GUIHANDLE cbhdl)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  osd->Stop();
}

bool cVNSIAdmin::DirtyCB(GUIHANDLE cbhdl)
{
  cVNSIAdmin* osd = static_cast<cVNSIAdmin*>(cbhdl);
  return osd->Dirty();
}

bool cVNSIAdmin::OnResponsePacket(cResponsePacket* resp)
{
  if (resp->getChannelID() == VNSI_CHANNEL_OSD)
  {
    uint32_t wnd, color, x0, y0, x1, y1, len;
    uint8_t *data;
    resp->getOSDData(wnd, color, x0, y0, x1, y1);
    if (wnd >= MAX_TEXTURES)
    {
      XBMC->Log(LOG_ERROR, "cVNSIAdmin::OnResponsePacket - invalid wndId: %s", wnd);
      return true;
    }
    if (resp->getOpCodeID() == VNSI_OSD_OPEN)
    {
      data = resp->getUserData();
      len = resp->getUserDataLength();
      const CLockObject lock(m_osdMutex);
      if (m_osdRender)
        m_osdRender->AddTexture(wnd, color, x0, y0, x1, y1, data[0]);
    }
    else if (resp->getOpCodeID() == VNSI_OSD_SETPALETTE)
    {
      data = resp->getUserData();
      len = resp->getUserDataLength();
      const CLockObject lock(m_osdMutex);
      if (m_osdRender)
        m_osdRender->SetPalette(wnd, x0, (uint32_t*)data);
    }
    else if (resp->getOpCodeID() == VNSI_OSD_SETBLOCK)
    {
      data = resp->getUserData();
      len = resp->getUserDataLength();
      const CLockObject lock(m_osdMutex);
      if (m_osdRender)
      {
        m_osdRender->SetBlock(wnd, x0, y0, x1, y1, color, data, len);
        m_bIsOsdDirty = true;
      }
    }
    else if (resp->getOpCodeID() == VNSI_OSD_CLEAR)
    {
      const CLockObject lock(m_osdMutex);
      if (m_osdRender)
        m_osdRender->Clear(wnd);
      m_bIsOsdDirty = true;
    }
    else if (resp->getOpCodeID() == VNSI_OSD_CLOSE)
    {
      {
        const CLockObject lock(m_osdMutex);
        if (m_osdRender)
          m_osdRender->DisposeTexture(wnd);
        m_bIsOsdDirty = true;
      }
    }
    else if (resp->getOpCodeID() == VNSI_OSD_MOVEWINDOW)
    {
    }
    else
      return false;
  }
  else
    return false;

  return true;
}

bool cVNSIAdmin::ConnectOSD()
{
  cRequestPacket vrp;
  vrp.init(VNSI_OSD_CONNECT);

  auto vresp = ReadResult(&vrp);
  if (vresp == NULL || vresp->noResponse())
  {
    return false;
  }
  uint32_t osdWidth = vresp->extract_U32();
  uint32_t osdHeight = vresp->extract_U32();
  if (m_osdRender)
    m_osdRender->SetOSDSize(osdWidth, osdHeight);

  return true;
}

bool cVNSIAdmin::ReadChannelList(bool radio)
{
  cRequestPacket vrp;
  vrp.init(VNSI_CHANNELS_GETCHANNELS);
  vrp.add_U32(radio);
  vrp.add_U8(0); // apply no filter

  auto vresp = ReadResult(&vrp);
  if (!vresp)
  {
    XBMC->Log(LOG_ERROR, "%s - Can't get response packed", __FUNCTION__);
    return false;
  }

  m_channels.m_channels.clear();
  m_channels.m_channelsMap.clear();
  while (vresp->getRemainingLength() >= 3 * 4 + 3)
  {
    CChannel channel;
    channel.m_blacklist = false;

    channel.m_number      = vresp->extract_U32();
    char *strChannelName  = vresp->extract_String();
    channel.m_name = strChannelName;
    char *strProviderName = vresp->extract_String();
    channel.m_provider    = strProviderName;
    channel.m_id          = vresp->extract_U32();
                            vresp->extract_U32(); // first caid
    char *strCaids        = vresp->extract_String();
    channel.SetCaids(strCaids);
    if (m_protocol >= 6)
    {
      std::string ref = vresp->extract_String();
    }
    channel.m_radio       = radio;

    m_channels.m_channels.push_back(channel);
    m_channels.m_channelsMap[channel.m_id] = m_channels.m_channels.size() - 1;
  }

  return true;
}

bool cVNSIAdmin::ReadChannelWhitelist(bool radio)
{
  cRequestPacket vrp;
  vrp.init(VNSI_CHANNELS_GETWHITELIST);
  vrp.add_U8(radio);

  auto vresp = ReadResult(&vrp);
  if (!vresp)
  {
    XBMC->Log(LOG_ERROR, "%s - Can't get response packed", __FUNCTION__);
    return false;
  }

  m_channels.m_providerWhitelist.clear();
  CProvider provider;
  while (vresp->getRemainingLength() >= 1 + 4)
  {
    char *strProviderName = vresp->extract_String();
    provider.m_name = strProviderName;
    provider.m_caid = vresp->extract_U32();
    m_channels.m_providerWhitelist.push_back(provider);
  }

  return true;
}

bool cVNSIAdmin::SaveChannelWhitelist(bool radio)
{
  m_channels.ExtractProviderWhitelist();

  cRequestPacket vrp;
  vrp.init(VNSI_CHANNELS_SETWHITELIST);
  vrp.add_U8(radio);

  for (const auto &provider : m_channels.m_providerWhitelist)
  {
    vrp.add_String(provider.m_name.c_str());
    vrp.add_S32(provider.m_caid);
  }

  auto vresp = ReadResult(&vrp);
  if (!vresp)
  {
    XBMC->Log(LOG_ERROR, "%s - Can't get response packed", __FUNCTION__);
    return false;
  }

  return true;
}

bool cVNSIAdmin::ReadChannelBlacklist(bool radio)
{
  cRequestPacket vrp;
  vrp.init(VNSI_CHANNELS_GETBLACKLIST);
  vrp.add_U8(radio);

  auto vresp = ReadResult(&vrp);
  if (!vresp)
  {
    XBMC->Log(LOG_ERROR, "%s - Can't get response packed", __FUNCTION__);
    return false;
  }

  m_channels.m_channelBlacklist.clear();
  while (vresp->getRemainingLength() >= 4)
  {
    int id = vresp->extract_U32();
    m_channels.m_channelBlacklist.push_back(id);
  }

  return true;
}

bool cVNSIAdmin::SaveChannelBlacklist(bool radio)
{
  m_channels.ExtractChannelBlacklist();

  cRequestPacket vrp;
  vrp.init(VNSI_CHANNELS_SETBLACKLIST);
  vrp.add_U8(radio);

  for (auto b : m_channels.m_channelBlacklist)
  {
    vrp.add_S32(b);
  }

  auto vresp = ReadResult(&vrp);
  if (!vresp)
  {
    XBMC->Log(LOG_ERROR, "%s - Can't get response packed", __FUNCTION__);
    return false;
  }

  return true;
}

void cVNSIAdmin::ClearListItems()
{
  m_window->ClearList();
  for (auto *i : m_listItems)
  {
    GUI->ListItem_destroy(i);
  }
  m_listItems.clear();
  m_listItemsMap.clear();
  m_listItemsChannelsMap.clear();
}

void cVNSIAdmin::LoadListItemsProviders()
{
  ClearListItems();

  int count = 0;
  for (const auto &provider : m_channels.m_providers)
  {
    std::string tmp;
    if(!provider.m_name.empty())
      tmp = provider.m_name;
    else
      tmp = XBMC->GetLocalizedString(30114);
    if (provider.m_caid == 0)
    {
      tmp += " - FTA";
    }
    else
    {
      tmp += " - CAID: ";
      char buf[16];
      sprintf(buf, "%04x", provider.m_caid);
      tmp += buf;
    }

    CAddonListItem *item = GUI->ListItem_create(tmp.c_str(), NULL, NULL, NULL, NULL);
    m_window->AddItem(item, count);
    GUIHANDLE hdl = m_window->GetListItem(count);
    m_listItems.push_back(item);
    m_listItemsMap[hdl] = count;

    if (provider.m_whitelist)
      item->SetProperty("IsWhitelist", "true");
    else
      item->SetProperty("IsWhitelist", "false");

    count++;
  }
}

void cVNSIAdmin::LoadListItemsChannels()
{
  ClearListItems();

  int count = 0;
  std::string tmp;
  for(unsigned int i=0; i<m_channels.m_channels.size(); i++)
  {
    if(!m_channels.IsWhitelist(m_channels.m_channels[i]))
      continue;

    tmp = m_channels.m_channels[i].m_name;
    tmp += " (";
    if(!m_channels.m_channels[i].m_provider.empty())
      tmp += m_channels.m_channels[i].m_provider;
    else
      tmp += XBMC->GetLocalizedString(30114);
    tmp += ")";
    CAddonListItem *item = GUI->ListItem_create(tmp.c_str(), NULL, NULL, NULL, NULL);
    m_window->AddItem(item, count);
    GUIHANDLE hdl = m_window->GetListItem(count);
    m_listItems.push_back(item);
    m_listItemsMap[hdl] = count;
    m_listItemsChannelsMap[hdl] = i;

    if (m_channels.m_channels[i].m_blacklist)
      item->SetProperty("IsBlacklist", "true");
    else
      item->SetProperty("IsBlacklist", "false");

    count++;
  }
}
