#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// 桌面图标遮罩。
//
// 旧版 Windows 的图标视图是透明的，PrintWindow 只画图标字形，黑底上非零即图标。
// 新版 Explorer 把壁纸和图标画进同一张表面，PrintWindow 得到的是合并画面，
// 于是“非黑即图标”会把整屏当成图标，旧实现只能拒绝绘制暗角。
//
// 现在的做法：向 Shell 取每个图标的单元格矩形，只在格内区分内容与壁纸。
// 每一行用左右边距的中位数估计该行壁纸颜色，与壁纸偏差超过阈值的像素归为内容。
// 遮罩永远不会越过单元格，所以最坏结果只是该图标被一并压暗，
// 不会出现整块提亮、也不会因为估计失真让暗角整片消失。
namespace icon_mask {
constexpr int SampleWidth=6;   // 每侧用于估计壁纸的取样宽度
constexpr int Threshold=24;    // 与壁纸估计的最大通道偏差
constexpr int CellBudget=75;   // 单格判定像素上限（百分比），超过即视为估计失真

struct Rgb { int red=0, green=0, blue=0; };

// 一个桌面上图标的单元格。label 为空时整格使用边距模型。
struct Cell { RECT bounds{}; RECT label{}; };

inline bool Get(const std::vector<unsigned char>& bits,size_t index) {
    return (bits[index/8]&static_cast<unsigned char>(1u<<(index%8)))!=0;
}
inline void Set(std::vector<unsigned char>& bits,size_t index) {
    bits[index/8]|=static_cast<unsigned char>(1u<<(index%8));
}
inline void Clear(std::vector<unsigned char>& bits,size_t index) {
    bits[index/8]&=static_cast<unsigned char>(~(1u<<(index%8)));
}

// 中位数对图标阴影和边缘抗锯齿不敏感，比均值稳。
inline Rgb Median(const uint32_t* pixels,int width,int begin,int end,int y) {
    int red[SampleWidth]{}, green[SampleWidth]{}, blue[SampleWidth]{};
    int count=0;
    for(int x=begin;x<end&&count<SampleWidth;++x) {
        const uint32_t pixel=pixels[static_cast<size_t>(y)*width+x];
        red[count]=static_cast<int>(pixel&0xFF);
        green[count]=static_cast<int>((pixel>>8)&0xFF);
        blue[count]=static_cast<int>((pixel>>16)&0xFF);
        ++count;
    }
    if(count<1) return {};
    std::sort(red,red+count); std::sort(green,green+count); std::sort(blue,blue+count);
    return {red[count/2],green[count/2],blue[count/2]};
}

// 标签行的文字笔画通常不足半行，整行中位数仍然是壁纸；
// 而图标行正好相反（图标常占半行以上），所以图标行必须用两侧边距。
inline Rgb RowMedian(const uint32_t* pixels,int width,int begin,int end,int y) {
    int red[256]{}, green[256]{}, blue[256]{};
    int count=0;
    for(int x=begin;x<end&&count<256;++x) {
        const uint32_t pixel=pixels[static_cast<size_t>(y)*width+x];
        red[count]=static_cast<int>(pixel&0xFF);
        green[count]=static_cast<int>((pixel>>8)&0xFF);
        blue[count]=static_cast<int>((pixel>>16)&0xFF);
        ++count;
    }
    if(count<1) return {};
    std::sort(red,red+count); std::sort(green,green+count); std::sort(blue,blue+count);
    return {red[count/2],green[count/2],blue[count/2]};
}

// 不依赖 std::min/std::max：windows.h 在部分工具链下会把它们定义成宏。
inline int Larger(int a,int b) { return a>b?a:b; }
inline int Smaller(int a,int b) { return a<b?a:b; }

inline int Deviation(uint32_t pixel,const Rgb& background) {
    const int red=std::abs(static_cast<int>(pixel&0xFF)-background.red);
    const int green=std::abs(static_cast<int>((pixel>>8)&0xFF)-background.green);
    const int blue=std::abs(static_cast<int>((pixel>>16)&0xFF)-background.blue);
    return Larger(red,Larger(green,blue));
}

// 去掉孤立像素：壁纸纹理造成的单点误判不应该在暗角上留下亮点。
// before 是全部单元格判定完成后的快照，逐个单元格的写入顺序不影响判定。
inline void DropIsolated(std::vector<unsigned char>& bits,const std::vector<unsigned char>& before,
    int width,const RECT& cell) {
    for(int y=static_cast<int>(cell.top);y<static_cast<int>(cell.bottom);++y)
        for(int x=static_cast<int>(cell.left);x<static_cast<int>(cell.right);++x) {
            const size_t index=static_cast<size_t>(y)*width+x;
            if(!Get(before,index)) continue;
            bool connected=false;
            for(int dy=-1;dy<=1&&!connected;++dy) for(int dx=-1;dx<=1;++dx) {
                if(!dx&&!dy) continue;
                if(Get(before,static_cast<size_t>(y+dy)*width+(x+dx))) { connected=true; break; }
            }
            if(!connected) Clear(bits,index);
        }
}

// pixels 为整张合并截图（左上角对齐、行优先 BGRA）；bits 覆盖整张截图。
// cells 为 Shell 报告的单元格矩形，与截图使用同一坐标系。
//
// 已知边界：壁纸在格内出现陡峭高对比边缘时，行模型无法重建那条边，
// 单格判定会整体超出预算，于是这一格回退为“不保护”。
// 该图标因此随暗角一起变暗，但不会出现边缘锐利的亮块——后者在视觉上明显更突兀。
inline void Segment(std::vector<unsigned char>& bits,int width,int height,const uint32_t* pixels,
    const std::vector<Cell>& cells) {
    bits.clear();
    if(!pixels||width<3||height<3) return;
    bits.assign((static_cast<size_t>(width)*height+7)/8,0);
    for(const Cell& reported:cells) {
        const RECT cell{Larger(1,reported.bounds.left),Larger(1,reported.bounds.top),
            Smaller(width-1,reported.bounds.right),Smaller(height-1,reported.bounds.bottom)};
        const int spanX=static_cast<int>(cell.right-cell.left), spanY=static_cast<int>(cell.bottom-cell.top);
        if(spanX<8||spanY<8) continue;
        const int labelTop=reported.label.bottom>reported.label.top&&reported.label.top>cell.top?
            static_cast<int>(reported.label.top):static_cast<int>(cell.bottom);
        size_t marked=0, area=0;
        for(int y=static_cast<int>(cell.top);y<static_cast<int>(cell.bottom);++y) {
            const bool labelRow=y>=labelTop;
            const Rgb leftEdge=labelRow?Rgb{}:Median(pixels,width,static_cast<int>(cell.left),
                Smaller(static_cast<int>(cell.left)+SampleWidth,static_cast<int>(cell.right)),y);
            const Rgb rightEdge=labelRow?Rgb{}:Median(pixels,width,
                Larger(static_cast<int>(cell.right)-SampleWidth,static_cast<int>(cell.left)),
                static_cast<int>(cell.right),y);
            const Rgb rowWide=labelRow?RowMedian(pixels,width,static_cast<int>(cell.left),
                static_cast<int>(cell.right),y):Rgb{};
            for(int x=static_cast<int>(cell.left);x<static_cast<int>(cell.right);++x) {
                Rgb background=rowWide;
                if(!labelRow) {
                    const double t=static_cast<double>(x-cell.left)/spanX;
                    background={
                        static_cast<int>(std::lround(leftEdge.red*(1-t)+rightEdge.red*t)),
                        static_cast<int>(std::lround(leftEdge.green*(1-t)+rightEdge.green*t)),
                        static_cast<int>(std::lround(leftEdge.blue*(1-t)+rightEdge.blue*t))};
                }
                const size_t index=static_cast<size_t>(y)*width+x;
                if(Deviation(pixels[index],background)>Threshold) { Set(bits,index); ++marked; }
            }
            area+=static_cast<size_t>(spanX);
        }
        // 单格判定过多说明壁纸估计失真：宁可让这个图标随暗角一起变暗，也不要整块提亮。
        if(area&&marked*100>area*CellBudget)
            for(int y=static_cast<int>(cell.top);y<static_cast<int>(cell.bottom);++y)
                for(int x=static_cast<int>(cell.left);x<static_cast<int>(cell.right);++x)
                    Clear(bits,static_cast<size_t>(y)*width+x);
    }
    // 孤立点判定需要“全部判定完成”的快照：一次拷贝，逐格检查。
    const std::vector<unsigned char> before=bits;
    for(const Cell& reported:cells) {
        const RECT cell{Larger(1,reported.bounds.left),Larger(1,reported.bounds.top),
            Smaller(width-1,reported.bounds.right),Smaller(height-1,reported.bounds.bottom)};
        if(cell.right-cell.left<8||cell.bottom-cell.top<8) continue;
        DropIsolated(bits,before,width,cell);
    }
}
}
