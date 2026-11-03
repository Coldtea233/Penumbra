#define UNICODE
#define _UNICODE
#include "../installer/InstallDiscovery.h"
#include <iostream>
#include <cstdlib>

static int checks=0;
static void Check(bool result,const char* text) {
    ++checks;if(!result) {std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}
}
int main() {
    using namespace penumbra::setup;
    std::wstring path;
    for(const auto* valid:{L"C:\\Apps\\Penumbra",L"D:\\工具\\Penumbra",L"C:/Some Folder/Penumbra/",L"c:\\x",L" C:\\工具\\Penumbra "})
        Check(NormalizeDirectory(valid,path),"valid Unicode/local path");
    Check(NormalizeDirectory(L"C:/Some Folder/Penumbra/",path)&&path==L"C:\\Some Folder\\Penumbra","normalize slash and trailing separator");
    for(const auto* invalid:{L"",L"C:\\",L"C:",L"relative",L"..\\Penumbra",L"\\\\server\\share\\Penumbra",L"\\\\?\\C:\\Penumbra",
         L"1:\\Penumbra",L"C:\\..\\Penumbra",L"C:\\.\\Penumbra",L"C:\\Apps\\\\Penumbra",L"C:\\Bad.\\Penumbra",L"C:\\Bad \\Penumbra",
         L"C:\\CON",L"C:\\NUL.txt",L"C:\\PRN",L"C:\\AUX",L"C:\\COM1\\Penumbra",L"C:\\LPT9.txt",L"C:\\COM¹",
         L"C:\\bad\"path",L"C:\\bad|path",L"C:\\bad?path",L"C:\\bad*path",L"C:\\bad:path",L"C:\\bad<path",L"C:\\bad>path",L"C:\\bad\npath"})
        Check(!NormalizeDirectory(invalid,path),"reject ambiguous/unsafe path");
    for(size_t limit:{40u,100u,180u,220u}) {
        Check(NormalizeDirectory(L"C:\\"+std::wstring(limit-3,L'a'),path,limit),"exact length boundary");
        Check(!NormalizeDirectory(L"C:\\"+std::wstring(limit-2,L'a'),path,limit),"over length boundary");
    }
    std::array<unsigned,3> version{};
    for(const auto* invalid:{L"",L"0.7",L"0.7.0.0",L"0.7.-1",L"0.7.beta",L"256.0.0",L"0.256.0",L"0.7.65536",L"0.7.99999999999999999",L"0.7.0\""})
        Check(!Version(invalid,version),"reject invalid MSI versions");
    const std::wstring ordered[]={L"0.5.0",L"0.6.0",L"0.6.1",L"0.7.0",L"0.7.1",L"0.8.0",L"0.10.0",L"1.0.0",L"255.255.65535"};
    for(size_t i=0;i<std::size(ordered);++i) for(size_t j=0;j<std::size(ordered);++j)
        Check(CompareVersions(ordered[i],ordered[j])==(i<j?-1:(i>j?1:0)),"numeric upgrade/repair/downgrade matrix");
    Check(RendererCommand(L"D:\\工具\\Penumbra")==L"\"D:\\工具\\Penumbra\\app\\Nocturne.Renderer.exe\"","quoted startup target");
    Check(RendererCommand(L"D:\\工具\\Penumbra\\")==RendererCommand(L"D:\\工具\\Penumbra"),"startup ignores directory trailing separator");
    CompletionOptions defaults;
    Check(defaults.run,"completion retains default-on run option only");
    auto installed=FindInstalled();
    if(!installed.code.empty()) {
        Check(Version(installed.version,version),"read installed version");
        Check(NormalizeDirectory(installed.directory,path,259),"read installed directory");
    }
    std::cout<<"PASS: "<<checks<<" installer path/version/discovery checks; no installation or registry writes.\n";
    if(!installed.code.empty()) std::wcout<<L"Detected installed version: "<<installed.version<<L"\n";
}
