// 图标遮罩推导的纯逻辑校验：不涉及 HWND、桌面抓取或正在运行的后台。
// 合成一杯壁纸渐变 + 若干单元格，检查内容判定、回退、位打包与耗时。
#include <windows.h>
#include "../src/Nocturne.Renderer/icon_mask.h"
#include "../src/Nocturne.Renderer/icon_glyph.h"
#include <chrono>
#include <cstdio>
#include <vector>

namespace {
constexpr int Width=640, Height=480;

// 线性渐变壁纸：行内线性、行间线性，行模型可以精确复现，任何判定都来自内容。
uint32_t Wallpaper(int x,int y) {
    const int red=30+x/8, green=60+y/10, blue=90;
    return (uint32_t(blue)<<16)|(uint32_t(green)<<8)|uint32_t(red);
}
void FillWallpaper(std::vector<uint32_t>& pixels,int width,int height) {
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) pixels[size_t(y)*width+x]=Wallpaper(x,y);
}
void FillRect(std::vector<uint32_t>& pixels,int width,int height,const RECT& rect,uint32_t color) {
    for(int y=rect.top;y<rect.bottom;++y) for(int x=rect.left;x<rect.right;++x)
        if(x>=0&&y>=0&&x<width&&y<height) pixels[size_t(y)*width+x]=color;
}
// 逐像素变暗：用来造一圈刚好低于阈值的图标阴影。
void DimRect(std::vector<uint32_t>& pixels,int width,int height,const RECT& rect,int amount) {
    for(int y=rect.top;y<rect.bottom;++y) for(int x=rect.left;x<rect.right;++x) {
        if(x<0||y<0||x>=width||y>=height) continue;
        const uint32_t pixel=pixels[size_t(y)*width+x];
        auto darker=[&](uint32_t value){const int level=static_cast<int>(value)-amount;return static_cast<uint32_t>(level>0?level:0);};
        pixels[size_t(y)*width+x]=(darker((pixel>>16)&0xFF)<<16)|(darker((pixel>>8)&0xFF)<<8)|darker(pixel&0xFF);
    }
}
constexpr uint32_t Glyph=0x00C828DC;   // 0x00BBGGRR：洋红图标本体
constexpr uint32_t Label=0x00F8F8F8;   // 0x00BBGGRR：亮色标签文字

icon_mask::Cell MakeCell(int left,int top) {
    icon_mask::Cell cell;
    cell.bounds={left,top,left+60,top+80};
    cell.label={left+18,top+50,left+42,top+56};
    return cell;
}
// 单元格内容：30×30 图标本体 + 一圈低对比阴影 + 一条居中标签文字。
void DrawItem(std::vector<uint32_t>& pixels,const icon_mask::Cell& cell) {
    const LONG left=cell.bounds.left+15, top=cell.bounds.top+10;
    DimRect(pixels,Width,Height,{left-1,top-1,left+31,top+31},12);
    FillRect(pixels,Width,Height,{left,top,left+30,top+30},Glyph);
    FillRect(pixels,Width,Height,{cell.label.left,cell.label.top,cell.label.right,cell.label.bottom},Label);
}
}

int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool result,const char* text){++checks;if(!result){++failures;std::printf("FAIL %s\n",text);}};

    std::vector<uint32_t> pixels(size_t(Width)*Height);
    FillWallpaper(pixels,Width,Height);
    std::vector<icon_mask::Cell> cells{MakeCell(20,20),MakeCell(120,20),MakeCell(220,20)};
    for(const auto& cell:cells) DrawItem(pixels,cell);

    std::vector<unsigned char> bits;
    icon_mask::Segment(bits,Width,Height,pixels.data(),cells);
    check(bits.size()==(size_t(Width)*Height+7)/8,"mask covers the whole capture");
    auto at=[&](int x,int y){return icon_mask::Get(bits,size_t(y)*Width+x);};
    for(const auto& cell:cells) {
        const LONG left=cell.bounds.left+15, top=cell.bounds.top+10;
        check(at(int(left)+15,int(top)+15),"icon body is protected");
        check(at(int(left),int(top)),"icon corner is protected");
        check(at(int(cell.label.left)+10,int(cell.label.top)+2),"label glyphs are protected");
        check(!at(int(cell.bounds.left)+2,int(cell.bounds.top)+2),"cell padding stays darkened");
        check(!at(int(left)-1,int(top)+15),"icon shadow stays darkened");
        check(!at(int(cell.bounds.left)+30,int(cell.bounds.top)+68),"empty cell area stays darkened");
    }

    // 没有标签矩形时整格使用边距模型：图标照样被保护，不会退化成不保护。
    std::vector<icon_mask::Cell> bare=cells;
    for(auto& cell:bare) cell.label={};
    icon_mask::Segment(bits,Width,Height,pixels.data(),bare);
    check(at(35,35),"a missing label rect keeps the icon protected");

    // 单格判定过多说明壁纸估计失真：不清空就整块提亮，所以交给字形回退处理。
    std::vector<uint32_t> blocky=pixels;
    FillWallpaper(blocky,Width,Height);
    FillRect(blocky,Width,Height,{26,22,74,98},0x00FF00FF);   // 留出两侧边距，让模型仍以壁纸为背景
    icon_mask::Cell withoutLabel{cells.front().bounds,{},-1,{}};
    std::vector<size_t> unresolved;
    icon_mask::Segment(bits,Width,Height,blocky.data(),{withoutLabel,cells.back()},&unresolved);
    int markedInCell=0;
    for(int y=22;y<98;++y) for(int x=26;x<74;++x) markedInCell+=icon_mask::Get(bits,size_t(y)*Width+x);
    check(markedInCell==0,"an over-budget cell is left to the glyph fallback");
    check(unresolved.size()==1&&unresolved.front()==0,"only the failing cell is reported");

    // 字形回退：图标方块几何、标签矩形重判、字形缩放。
    const RECT box=icon_mask::IconBox(cells.front());
    check(box.top==cells.front().bounds.top,"the icon box starts at the cell top");
    check(box.bottom-box.top==cells.front().label.top-cells.front().bounds.top,"the icon box side follows the label");
    check(std::abs((box.left+box.right)/2-(cells.front().bounds.left+cells.front().bounds.right)/2)<=1,"the icon box is centred");
    std::vector<uint32_t> labelOnly=pixels;
    FillWallpaper(labelOnly,Width,Height);
    // 只填标签矩形中间的一段，两侧留出壁纸，模拟“文字贴着矩形、矩形比文字宽一点”。
    const long labelSpan=cells.front().label.right-cells.front().label.left;
    FillRect(labelOnly,Width,Height,{cells.front().label.left+labelSpan/3,cells.front().label.top,
        cells.front().label.right-labelSpan/3,cells.front().label.bottom},Label);
    icon_mask::SegmentLabelRect(bits,Width,Height,labelOnly.data(),cells.front().label);
    check(at(int(cells.front().label.left)+10,int(cells.front().label.top)+2),"the label fallback keeps the text");
    check(!at(int(cells.front().bounds.left)+2,int(cells.front().bounds.top)+2),"the label fallback stays inside its rect");

    // 标签字形：用桌面图标字体直接画字，结果必须落在标签矩形内。
    bits.assign((size_t(Width)*Height+7)/8,0);
    const RECT labelRect{40,40,200,60};
    icon_glyph::WriteLabel(bits,Width,Height,L"Penumbra",labelRect,96);
    int inside=0,outside=0;
    for(int y=0;y<Height;++y) for(int x=0;x<Width;++x) {
        if(!icon_mask::Get(bits,size_t(y)*Width+x)) continue;
        if(x>=labelRect.left&&x<labelRect.right&&y>=labelRect.top&&y<labelRect.bottom) inside++; else outside++;
    }
    check(inside>40,"the label glyph covers the text area");
    check(outside==0,"the label glyph stays inside the label rect");

    std::vector<unsigned char> glyph(2*2*4,0);
    glyph[3]=255; glyph[7]=255;             // 左上、右上不透明，下面一行透明
    icon_mask::ScaleGlyph(bits,Width,Height,glyph.data(),2,2,{100,100,104,104});
    check(at(100,100)&&at(103,100),"the glyph is scaled to the target box");
    check(!at(100,103)&&!at(103,103),"transparent glyph rows stay darkened");

    // 壁纸纹理造成的孤立点不应在暗角上留下亮点。
    std::vector<uint32_t> speck=pixels;
    FillWallpaper(speck,Width,Height);
    speck[size_t(300)*Width+300]=0x00FFFFFF;
    icon_mask::Segment(bits,Width,Height,speck.data(),{MakeCell(280,280)});
    check(!icon_mask::Get(bits,size_t(300)*Width+300),"an isolated speck is dropped");

    // 退化输入：空单元格、越界、过小、反向矩形、缺少截图。
    std::vector<uint32_t> plain(size_t(Width)*Height);
    FillWallpaper(plain,Width,Height);
    auto countBits=[&]{size_t any=0;for(unsigned char value:bits) any+=value;return any;};
    icon_mask::Segment(bits,Width,Height,plain.data(),{});
    check(countBits()==0,"no cells produces no protection");
    icon_mask::Segment(bits,Width,Height,plain.data(),{MakeCell(-50,-50),MakeCell(630,470),MakeCell(100,100)});
    check(countBits()==0,"degenerate cells produce no protection");
    icon_mask::Segment(bits,Width,Height,nullptr,cells);
    check(countBits()==0,"a missing capture produces no protection");

    // 位打包与 IconMask::At 的约定一致（每像素 1 位、行优先、低位在前）。
    std::vector<unsigned char> packed(4,0);
    icon_mask::Set(packed,0); icon_mask::Set(packed,9); icon_mask::Set(packed,17);
    check((packed[0]&0x01)!=0&&(packed[1]&0x02)!=0&&(packed[2]&0x02)!=0,"bit packing is row-major, LSB first");
    icon_mask::Clear(packed,9);
    check(!icon_mask::Get(packed,9)&&icon_mask::Get(packed,17),"clear removes exactly one bit");

    // 全高清桌面、140 个单元格：单次重建的代价保持在可接受量级。
    std::vector<uint32_t> full(size_t(1920)*1080);
    FillWallpaper(full,1920,1080);
    std::vector<icon_mask::Cell> many;
    for(int column=0;column<7;++column) for(int row=0;row<20;++row) {
        const icon_mask::Cell cell=MakeCell(column*76,row*52);
        FillRect(full,1920,1080,{cell.bounds.left+16,cell.bounds.top+4,cell.bounds.left+59,cell.bounds.top+47},Glyph);
        FillRect(full,1920,1080,{cell.label.left,cell.label.top,cell.label.right,cell.label.bottom},Label);
        many.push_back(cell);
    }
    const auto start=std::chrono::steady_clock::now();
    icon_mask::Segment(bits,1920,1080,full.data(),many);
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    int manyMarked=0;
    for(int y=0;y<1080;++y) for(int x=0;x<1920;++x) manyMarked+=icon_mask::Get(bits,size_t(y)*1920+x);
    std::printf("{\"SegmentMs\":%.2f,\"MarkedPercent\":%.2f}\n",elapsed,100.0*manyMarked/(1920.0*1080));
    check(elapsed<400,"a full desktop with 140 cells stays far below a frame budget");
    check(manyMarked>1920*1080/40&&manyMarked<1920*1080/4,"marks stay inside a plausible icon footprint");

    std::printf("{\"IconMaskChecks\":%u,\"Failures\":%u}\n",checks,failures);
    return failures?1:0;
}
