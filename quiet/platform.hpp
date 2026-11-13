#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <powrprof.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <chrono>
#include <iomanip>
#include "policy.hpp"
#ifndef QUIET_STAGE
#define QUIET_STAGE 3
#endif
namespace quiet {
namespace fs=std::filesystem;
inline std::wstring exePath(){wchar_t p[32768];DWORD n=GetModuleFileNameW(nullptr,p,32768);return std::wstring(p,n);}
inline fs::path exeDir(){return fs::path(exePath()).parent_path();}
inline bool isAdmin(){SID_IDENTIFIER_AUTHORITY a=SECURITY_NT_AUTHORITY;PSID sid=nullptr;BOOL result=FALSE;if(AllocateAndInitializeSid(&a,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){CheckTokenMembership(nullptr,sid,&result);FreeSid(sid);}return result!=FALSE;}
inline std::wstring userSid(){HANDLE t=nullptr; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&t))return L"";DWORD n=0;GetTokenInformation(t,TokenUser,nullptr,0,&n);std::vector<char>b(n);std::wstring out;if(GetTokenInformation(t,TokenUser,b.data(),n,&n)){LPWSTR s=nullptr;if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid,&s)){out=s;LocalFree(s);}}CloseHandle(t);return out;}
inline std::string utf8(const std::wstring&s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string b(n,' ');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),b.data(),n,nullptr,nullptr);return b;}
inline std::wstring wide(const std::string&s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring b(n,L' ');if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),b.data(),n);return b;}
inline std::string jsonQuoted(const std::string&s){std::string out="\"";constexpr char hex[]="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=static_cast<char>(c);}out+='"';return out;}
inline std::wstring timestamp(){SYSTEMTIME t;GetLocalTime(&t);wchar_t b[64];swprintf_s(b,L"%04u-%02u-%02u %02u:%02u:%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return b;}
inline fs::path dataDir(bool service=false){wchar_t p[MAX_PATH];SHGetFolderPathW(nullptr,service?CSIDL_COMMON_APPDATA:CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,p);auto d=fs::path(p)/L"TPFCQuiet";if(!service)d/=QUIET_STAGE==1?L"A":QUIET_STAGE==2?L"B":L"C";std::error_code ec;fs::create_directories(d,ec);return d;}
inline std::wstring machineId(){HKEY k;std::wstring out; if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",0,KEY_READ,&k)==ERROR_SUCCESS){for(auto name:{L"SystemProductName",L"BIOSVersion"}){wchar_t b[1024]{};DWORD n=sizeof(b),type;if(RegQueryValueExW(k,name,nullptr,&type,reinterpret_cast<BYTE*>(b),&n)==ERROR_SUCCESS)out+=std::wstring(b)+L"|";}RegCloseKey(k);}return out;}
inline std::wstring profileName(Profile p){return p==Profile::Quiet?L"静音办公":p==Profile::Balanced?L"均衡":L"散热优先";}
inline std::wstring modeName(Mode m){return m==Mode::Bios?L"BIOS控制":m==Mode::Shadow?L"影子观察":m==Mode::Smart?L"Smart控制":m==Mode::Commission?L"档位验证":L"限时手动";}
inline bool originalRunning(){HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(h==INVALID_HANDLE_VALUE)return true;PROCESSENTRY32W p{};p.dwSize=sizeof(p);bool found=false;if(Process32FirstW(h,&p))do{if(p.th32ProcessID!=GetCurrentProcessId()&&!_wcsicmp(p.szExeFile,L"TPFanControl.exe")){found=true;break;}}while(Process32NextW(h,&p));CloseHandle(h);return found;}
inline std::wstring version(){return L"3.5.5-"+std::wstring(QUIET_STAGE==1?L"A":QUIET_STAGE==2?L"B":L"C");}
inline std::wstring sensorName(int i,bool=false,int=0){if(i==12)return L"CPU核心最高";if(i==13)return L"GPU / Vantage";wchar_t b[80];swprintf_s(b,L"EC测点 0x%02X",i<8?0x78+i:0xc0+i-8);return b;}
}
