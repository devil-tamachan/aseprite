//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.
//

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/file/file.h"
#include "app/file/file_format.h"
#include "app/file/format_options.h"
#include "app/console.h"
#include "app/context.h"
#include "app/document.h"
#include "base/cfile.h"
#include "base/file_handle.h"
#include "doc/doc.h"
#include "app/find_widget.h"
#include "app/load_widget.h"
#include "ui/window.h"
#include "zlib.h"

namespace app {

using namespace base;
using namespace ui;

class AdwFormat : public FileFormat {

  // Data for BMP files
  class AdwOptions : public FormatOptions
  {
  public:
    bool bAnim;
  };

  const char* onGetName() const override { return "adw"; }
  const char* onGetExtensions() const override { return "adw"; }
  int onGetFlags() const override {
    return
      FILE_SUPPORT_LOAD |
     // FILE_SUPPORT_SAVE |
      FILE_SUPPORT_RGBA |
      FILE_SUPPORT_LAYERS |
      FILE_SUPPORT_FRAMES |
      //FILE_SUPPORT_GRAY |
      //FILE_SUPPORT_INDEXED |
      FILE_SUPPORT_FRAME_TAGS |
      FILE_SUPPORT_GET_FORMAT_OPTIONS_LOAD;// |
//      FILE_SUPPORT_SEQUENCES;
  }
  bool _loadADW_ver2(FileOp *fop, FILE* fp);

  bool onLoad(FileOp* fop) override;
#ifdef ENABLE_SAVE
  bool onSave(FileOp* fop) override;
#endif
  base::SharedPtr<FormatOptions> onGetFormatOptionsLoad(FileOp* fop) override;
};

FileFormat* CreateAdwFormat()
{
  return new AdwFormat;
}




void GetWORDLE(const uint8_t *pBuf,void *pVal)
{
    *((uint16_t *)pVal) = (pBuf[1] << 8) | pBuf[0];
}

bool ZlibGetDecBuf(void *pDstBuf,uint32_t dwDstSize,void *pSrcBuf,uint32_t dwSrcSize, z_stream *pz)
{
    pz->next_out  = (uint8_t *)pDstBuf;
    pz->avail_out = dwDstSize;
    pz->next_in   = (uint8_t *)pSrcBuf;
    pz->avail_in  = dwSrcSize;

    while(1)
    {
        int ret = ::inflate(pz, Z_NO_FLUSH);

        if(ret == Z_STREAM_END)
            return true;
        else if(ret != Z_OK)
            return false;
    }
}

void simpleUTF16toASCII(char *dst, uint16_t *src)
{
  int i=0;
  for(;;i++)
  {
    uint16_t wc = src[i];
    if(wc==0)break;
    if(wc>=0x20 && wc<=0x7D)dst[i]=(char)wc;
    else dst[i] = '.';
  }
  dst[i]=0;
}

void initLayer(Layer *layer, bool bAnim, uint16_t *wName, char *cName, Sprite *sprite)
{
  static_cast<LayerImage*>(layer)->setBlendMode((BlendMode)0);
  //static_cast<LayerImage*>(layer)->setOpacity(m_btAlpha);
  layer->setFlags(static_cast<LayerFlags>(bAnim ? 19 : 0x3));
  //ñºëO
  for (int j = 0; j < 25; j++)
    GetWORDLE((uint8_t *)(wName + j), wName + j);
  wName[24] = 0;
  simpleUTF16toASCII(cName, wName);
  layer->setName(cName);
  static_cast<LayerFolder*>(sprite->folder())->addLayer(layer);
}

bool AdwFormat::_loadADW_ver2(FileOp *fop, FILE* fp)
{
    uint32_t dw;
    uint16_t wSize,wWidth,wHeight,wLayerCnt,wLayerSel,wDPI,wLayerInfoSize,tx,ty;
    uint16_t wName[25];
    char     cName[25*2];
    uint32_t ti,dwTileCnt;
    gfx::Rect rc;
    int i,j;
    uint16_t wProgress=0;
    z_stream z;
    z.zalloc = Z_NULL;
    z.zfree = Z_NULL;
    z.opaque = Z_NULL;
    UniquePtr<Sprite> sprite;
    const base::SharedPtr<AdwOptions> adwOptions = fop->sequenceGetFormatOptions();
    bool bAnim = adwOptions->bAnim;
    std::vector<Layer*> sublayers;
    std::vector<Layer*> bglayers;
    std::vector<Cel*> subcels;
    std::vector<Cel*> bgcels;

    uint8_t *bufZlib = (uint8_t *)malloc(4096*2);
    if(bufZlib==NULL)goto ERR_loadADW_ver2;
    
    if(::inflateInit(&z) != Z_OK)goto ERR_loadADW_ver2;

    fseek(fp, 12, SEEK_SET);
    dw = fgetl(fp);
    fseek(fp, dw, SEEK_CUR);


    wSize = fgetw(fp);
    wWidth = fgetw(fp);
    wHeight = fgetw(fp);
    wDPI = fgetw(fp);
    wLayerCnt = fgetw(fp);
    wLayerSel = fgetw(fp);
    wLayerInfoSize = fgetw(fp);

    fseek(fp, wSize - 12, SEEK_CUR);

    sprite.reset(new Sprite(IMAGE_RGB, wWidth, wHeight, 1));

    int frameNum=0, frameNum2=0;
    int iSubLayer = 0;
    int iBGLayer = 0;
    bool bBGLinkSkip = true;
    bool bSubLinkSkip = true;

    Layer* layer = NULL;
    Layer* mainlayer = NULL;
    uint8_t m_btATypePrev = -1;

    for(i = wLayerCnt; i > 0; i--)
    {
        rc.x = fgetl(fp);
        rc.y = fgetl(fp);
        rc.w = fgetl(fp) - rc.x;
        rc.h = fgetl(fp) - rc.y;
        dwTileCnt = fgetl(fp);

        int nTileXCnt = (rc.w + 63) / 64;
        int nTileYCnt = (rc.h + 63) / 64;
        

        if(fread(wName, 50, 1, fp)!=1)goto ERR_loadADW_ver2;
        uint8_t m_btAlpha = fgetc(fp);
        uint32_t m_dwCol = fgetl(fp);
        m_dwCol = (m_dwCol & 0xFF00FF00) | ((m_dwCol & 0xFF)<<16) | ((m_dwCol & 0xFF0000)>>16);//BGRA->RGBA
        uint8_t m_btFlag = fgetc(fp);
        uint8_t m_btAType = fgetc(fp); //0-Normal,1-Sub,2-All,3-NotVisible
        uint16_t m_wAnimCnt = fgetw(fp);

        ImageRef image(Image::create(IMAGE_RGB, rc.w, rc.h));
        
        
        if(!bAnim || m_btAType==0)
        {
          if(!bAnim || !mainlayer)
          {
            layer = new LayerImage(sprite);
            if (!layer) goto ERR_loadADW_ver2;
            mainlayer = layer;

            initLayer(layer, bAnim, wName, cName, sprite);
          }
          layer = mainlayer;
          iSubLayer = 0;
          
        } else if(m_btAType==1) {
          if (iSubLayer >= sublayers.size())
          {
            layer = new LayerImage(sprite);
            if (!layer) goto ERR_loadADW_ver2;
            sublayers.push_back(layer);

            initLayer(layer, bAnim, wName, cName, sprite);
          }
          layer = sublayers[iSubLayer];
          if(!layer) goto ERR_loadADW_ver2;
          iSubLayer++;
          bSubLinkSkip = true;
          
        } else if (m_btAType == 2)  { //Type:All
          if (m_btATypePrev != 2)
          {
            //bglayers.clear();
            bgcels.clear();
            iBGLayer = 0;
          }
          if (iBGLayer >= bglayers.size())
          {
            layer = new LayerImage(sprite);
            if (!layer) goto ERR_loadADW_ver2;
            bglayers.push_back(layer);

            initLayer(layer, bAnim, wName, cName, sprite);
          }
          layer = bglayers[iBGLayer];
          if (!layer) goto ERR_loadADW_ver2;
          iBGLayer++;
          bBGLinkSkip = true;
        } else /*if(m_btAType == 3)*/ {

        }
        

        fseek(fp, wLayerInfoSize - 79, SEEK_CUR);

        for(ti = dwTileCnt; ti > 0; ti--)
        {
            tx = fgetw(fp);
            ty = fgetw(fp);
            wSize = fgetw(fp);
            int tilex = tx*64+rc.x;
            int tiley = ty*64+rc.y;

            if(tx >= nTileXCnt || ty >= nTileYCnt || wSize == 0 || wSize > 4096)
                goto ERR_loadADW_ver2;

            if(wSize == 4096)
            {
                //ñ≥à≥èk
                for(int k=0;k<64;k++)
                {
                  for(int l=0;l<64;l++)
                  {
                    unsigned char alpha = (fgetc(fp)&0xFF);
                    if(tilex>=rc.x && tilex < rc.x+rc.w && tiley>=rc.y && tiley<rc.y+rc.h)
                    {
                      put_pixel(image.get(), l+tx*64, k+ty*64, m_dwCol | (alpha<<24));
                    }
                  }
                }
            }
            else
            {
                //ZIPà≥èk

                if(fread(bufZlib, wSize, 1, fp)!=1)goto ERR_loadADW_ver2;

                if(::inflateReset(&z) != Z_OK) goto ERR_loadADW_ver2;

                uint8_t *dstBuf = bufZlib+4096;
                if(!ZlibGetDecBuf(dstBuf, 4096, bufZlib, wSize, &z))
                    goto ERR_loadADW_ver2;
                
                for(int k=0;k<64;k++)
                {
                  for(int l=0;l<64;l++)
                  {
                    if(tilex>=rc.x && tilex < rc.x+rc.w && tiley>=rc.y && tiley<rc.y+rc.h)
                    {
                      put_pixel(image.get(), l+tx*64, k+ty*64, m_dwCol | (dstBuf[0]<<24));
                      dstBuf++;
                    }
                  }
                }
            }
        }
        if (m_btAType == 3)
        {
          m_btATypePrev = m_btAType;
          if (bAnim)
          {
            for (int l = 0; l < sublayers.size(); l++)
            {
              Cel *cel = sublayers[l]->cel(frameNum);
              if (cel)static_cast<LayerImage*>(sublayers[l])->removeCel(cel);
            }
            subcels.clear();
            continue;
          }
          else layer->setFlags(static_cast<LayerFlags>(2));
        }
        base::UniquePtr<Cel> cel;
        cel.reset(new Cel(bAnim ? frameNum:0, image));
        if(!bAnim || (m_btAType==0 || m_btAType == 3))frameNum++;
        cel->setPosition(rc.x, rc.y);
        //cel->setOpacity(m_btAlpha);
        
        if(!bAnim || (m_btAType==0 || m_btAType == 3))
        {
          static_cast<LayerImage*>(layer)->addCel(cel);
        } else if(m_btAType==1) {
          static_cast<LayerImage*>(layer)->addCel(cel);
          subcels.push_back(cel);
        } else {
          static_cast<LayerImage*>(layer)->addCel(cel);
          bgcels.push_back(cel);
        }
        
        if(bAnim && (m_btAType==0 || m_btAType == 3))
        {
          base::UniquePtr<Cel> cel2;
          int startFrameNum = frameNum;
          for(int k=1;k<m_wAnimCnt;k++)
          {
            cel2.reset(Cel::createLink(cel));
            cel2->setFrame(frameNum);
            static_cast<LayerImage*>(layer)->addCel(cel2);
            cel2.release();
            frameNum++;
          }
          for(int l=0;l<subcels.size();l++)
          {
            frameNum2 = startFrameNum;
            int k=0;
            if(bSubLinkSkip)
            {
              k++;
              frameNum2++;
            }
            for(;k<m_wAnimCnt;k++)
            {
              cel2.reset(Cel::createLink(subcels[l]));
              cel2->setFrame(frameNum2-1);
              static_cast<LayerImage*>(sublayers[l])->addCel(cel2);
              cel2.release();
              frameNum2++;
            }
          }
          subcels.clear();
          for (int l = 0; l < bgcels.size(); l++)
          {
            frameNum2 = startFrameNum;
            int k=0;
            if(bBGLinkSkip)
            {
              k++;
              frameNum2++;
            }
            for (; k < m_wAnimCnt; k++)
            {
              cel2.reset(Cel::createLink(bgcels[l]));
              cel2->setFrame(frameNum2 - 1);
              static_cast<LayerImage*>(bglayers[l])->addCel(cel2);
              cel2.release();
              frameNum2++;
            }
          }
          bSubLinkSkip = false;
          bBGLinkSkip = false;
        }
        cel.release();

        m_btATypePrev = m_btAType;

        wProgress++;
        fop->setProgress(wProgress / wLayerCnt);
    }

    sprite->setTotalFrames(frame_t(bAnim ? frameNum:1));

    fop->createDocument(sprite);
    sprite.release();
    
    ::inflateEnd(&z);
    if(bufZlib!=NULL)free(bufZlib);

    return true;
    
ERR_loadADW_ver2:
    ::inflateEnd(&z);
    if(bufZlib!=NULL)free(bufZlib);
    return false;
}


bool AdwFormat::onLoad(FileOp *fop)
{
  FileHandle handle(open_file_with_exception(fop->filename(), "rb"));
  FILE* f = handle.get();
  
  unsigned char *tmp[50];
  
  if(fread(tmp, 7, 1, f)!=1 || memcmp(tmp, "AZDWDAT", 7)!=0)return false;
  int c = fgetc(f);
  
  switch(c)
  {
    /*case 0:
      return _loadADW_ver1(&file, pProgDlg);
      break;*/
    case 1:
      return _loadADW_ver2(fop, f);
      break;
    default:
      return false;
  }


  if (ferror(f)) {
    fop->setError("Error reading file.\n");
    return false;
  }

  return true;
}

#ifdef ENABLE_SAVE
bool AdwFormat::onSave(FileOp *fop)
{
  return false;
}
#endif

base::SharedPtr<FormatOptions> AdwFormat::onGetFormatOptionsLoad(FileOp* fop)
{
  base::SharedPtr<AdwOptions> adw_options;
  //if (fop->document()->getFormatOptions())
  //  adw_options = base::SharedPtr<AdwOptions>(fop->document()->getFormatOptions());

  //if (!adw_options)
    adw_options.reset(new AdwOptions);

  // Non-interactive mode
  if (!fop->context() ||
      !fop->context()->isUIAvailable())
    return adw_options;

  try {
    base::UniquePtr<Window> window(app::load_widget<Window>("adwimport_settings.xml", "adwimport_settings"));
    Widget* button_ok = app::find_widget<Widget>(window, "ok");
    Widget* check_anim = app::find_widget<Widget>(window, "check_anim");

    window->openWindowInForeground();

    if (window->closer() == button_ok) {
      adw_options->bAnim = check_anim->isSelected();
    }
    else {
      adw_options.reset(NULL);
    }

    return adw_options;
  }
  catch (std::exception& e) {
    Console::showException(e);
    return base::SharedPtr<AdwOptions>(0);
  }
}
} // namespace app
