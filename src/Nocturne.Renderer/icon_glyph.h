#pragma once
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <cstdint>
#include <string>
#include <vector>
#include "icon_mask.h"

// 壁纸估计失真时的字形回退。
//
// 单元格里出现陡峭高对比边缘（或纹理）时，行模型无法重建那条边，逐格判定会整体超出预算；
// 此时不清空（整格变暗）也不整块保护（亮框），而是换用确定性的字形来源：
//   * 图标：Shell 的系统 image list 里同一张字形，按图标方块缩放；
//   * 标签：用桌面图标字体画出文字字形，按 ink 中心对齐到标签矩形；
//     这条路径也不可用时，退回“只在标签矩形内按整行中位数重判”。
namespace icon_glyph {
// 把一张字形 bitmap 的 alpha 写入遮罩。target 为目标屏幕矩形。
inline bool WriteGlyph(std::vector<unsigned char>& bits,int width,int height,
    IImageList* images,int image,const RECT& target) {
    if(!images||image<0) return false;
    HICON glyph=nullptr;
    if(FAILED(images->GetIcon(image,ILD_TRANSPARENT,&glyph))||!glyph) return false;
    bool written=false;
    ICONINFO info{};
    if(GetIconInfo(glyph,&info)) {
        BITMAP bitmap{};
        if(info.hbmColor&&GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap)&&
            bitmap.bmWidth>0&&bitmap.bmHeight>0&&bitmap.bmWidth<=1024&&bitmap.bmHeight<=1024) {
            BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth=bitmap.bmWidth; bi.bmiHeader.biHeight=-bitmap.bmHeight;
            bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
            std::vector<unsigned char> alpha(static_cast<size_t>(bitmap.bmWidth)*bitmap.bmHeight*4);
            HDC screen=GetDC(nullptr);
            const bool read=screen&&GetDIBits(screen,info.hbmColor,0,static_cast<UINT>(bitmap.bmHeight),
                alpha.data(),&bi,DIB_RGB_COLORS)==bitmap.bmHeight;
            if(screen) ReleaseDC(nullptr,screen);
            if(read) {
                icon_mask::ScaleGlyph(bits,width,height,alpha.data(),bitmap.bmWidth,bitmap.bmHeight,target);
                written=true;
            }
        }
        if(info.hbmColor) DeleteObject(info.hbmColor);
        if(info.hbmMask) DeleteObject(info.hbmMask);
    }
    DestroyIcon(glyph);
    return written;
}

// 用桌面图标字体把标签文字画成字形遮罩：文字笔画占比可达半行以上，
// 逐行中位数在长标签上会翻转成“文字为背景”，所以回退格不再靠估计。
inline void WriteLabel(std::vector<unsigned char>& bits,int width,int height,const std::wstring& text,
    const RECT& label,int dpi) {
    if(text.empty()) return;
    const int lw=label.right-label.left, lh=label.bottom-label.top;
    if(lw<4||lh<4||lw>512||lh>128) return;
    LOGFONTW font{};
    if(!SystemParametersInfoW(SPI_GETICONTITLELOGFONT,sizeof(font),&font,0)) return;
    if(dpi>0&&font.lfHeight<0) font.lfHeight=MulDiv(font.lfHeight,dpi,96);
    HFONT handle=CreateFontIndirectW(&font);
    if(!handle) return;
    const int pad=6, dw=lw+pad*2, dh=lh+pad*2;
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=dw; bi.bmiHeader.biHeight=-dh;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr), memory=CreateCompatibleDC(screen);
    void* pixels{};
    HBITMAP bitmap=screen&&memory?CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;
    if(!bitmap||!pixels) {
        if(bitmap) DeleteObject(bitmap);
        if(memory) DeleteDC(memory);
        if(screen) ReleaseDC(nullptr,screen);
        DeleteObject(handle);
        return;
    }
    HGDIOBJ previous=SelectObject(memory,bitmap);
    HGDIOBJ previousFont=SelectObject(memory,handle);
    ZeroMemory(pixels,static_cast<size_t>(dw)*dh*4);
    SetBkMode(memory,TRANSPARENT);
    SetTextColor(memory,RGB(255,255,255));
    RECT box{pad,pad,pad+lw,pad+lh};
    DrawTextW(memory,text.c_str(),static_cast<int>(text.size()),&box,
        DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|DT_END_ELLIPSIS);
    const auto* ink=static_cast<const uint32_t*>(pixels);
    int minX=dw,minY=dh,maxX=-1,maxY=-1;
    for(int y=0;y<dh;++y) for(int x=0;x<dw;++x) {
        const uint32_t pixel=ink[static_cast<size_t>(y)*dw+x];
        const int luminance=static_cast<int>(((pixel>>16&0xFF)*30+(pixel>>8&0xFF)*59+(pixel&0xFF)*11)/100);
        if(luminance>24) { if(x<minX) minX=x; if(y<minY) minY=y; if(x>maxX) maxX=x; if(y>maxY) maxY=y; }
    }
    if(maxX>=minX&&maxY>=minY) {
        // 标签矩形贴着文字排版框：把字形 ink 中心对齐过去，不缩放，避免字形变形。
        const int offsetX=(lw-(maxX-minX+1))/2-minX;
        const int offsetY=(lh-(maxY-minY+1))/2-minY;
        for(int y=0;y<lh;++y) {
            const int sy=y-offsetY;
            if(sy<0||sy>=dh) continue;
            const int ty=label.top+y;
            if(ty<1||ty>=height-1) continue;
            for(int x=0;x<lw;++x) {
                const int sx=x-offsetX;
                if(sx<0||sx>=dw) continue;
                const uint32_t pixel=ink[static_cast<size_t>(sy)*dw+sx];
                const int luminance=static_cast<int>(((pixel>>16&0xFF)*30+(pixel>>8&0xFF)*59+(pixel&0xFF)*11)/100);
                if(luminance<=24) continue;
                const int tx=label.left+x;
                if(tx<1||tx>=width-1) continue;
                icon_mask::Set(bits,static_cast<size_t>(ty)*width+tx);
            }
        }
    }
    SelectObject(memory,previousFont);
    SelectObject(memory,previous);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr,screen);
    DeleteObject(handle);
}

// pixels 为整张合并截图；cells/unresolved 来自 icon_mask::Segment。
inline void Resolve(std::vector<unsigned char>& bits,int width,int height,const uint32_t* pixels,
    const std::vector<icon_mask::Cell>& cells,const std::vector<size_t>& unresolved,int dpi=0) {
    if(unresolved.empty()) return;
    if(width<3||height<3||bits.size()<(static_cast<size_t>(width)*height+7)/8) return;
    // 常规图标尺寸用 48 像素列表，只有放大过图标时才取 256 像素那张。
    IImageList* compact=nullptr, *detailed=nullptr;
    for(size_t index:unresolved) {
        if(index>=cells.size()) continue;
        const icon_mask::Cell& cell=cells[index];
        if(cell.label.bottom>cell.label.top) {
            WriteLabel(bits,width,height,cell.text,cell.label,dpi);
            if(cell.text.empty()) icon_mask::SegmentLabelRect(bits,width,height,pixels,cell.label);
        }
        const RECT box=icon_mask::IconBox(cell);
        const long side=box.right-box.left;
        IImageList** slot=(side>56)?&detailed:&compact;
        if(!*slot) {
            if(FAILED(SHGetImageList((side>56)?SHIL_JUMBO:SHIL_EXTRALARGE,__uuidof(IImageList),reinterpret_cast<void**>(slot))))
                *slot=nullptr;
        }
        WriteGlyph(bits,width,height,*slot,cell.image,box);
    }
    if(compact) compact->Release();
    if(detailed) detailed->Release();
}
}
