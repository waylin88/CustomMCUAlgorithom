#include"CustomMCUAlgo.h"
#include<cstdlib>
#include<cstdio>
#include<cstring>
#include<ctime>
#include<fstream>
#include<iostream>
#include<string>

STRUCOPERATOR OprateResult;  //芯片烧录操作结果

BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
};


ICINFORMATION IcInformation = // 声明芯片编程相关信息
    {
        "@", // 当前烧录芯片的型号名，编号前并带冒号提示，型号要相同
        // 当上位机在烧录时，通过判断DLL自定的字符是否匹配所选的芯片
        // 若是不相同的字符则跳过

        LINECOUNTER, // 提示烧录时使用的对话框输出信息(5行)，当信息完全
        "[DEVICE]", // 只用在显示的信息行中，第5行=LINECOUNTER
        "5620GEN",
        "[Description]",
        "MTK MAC auto-inject algo",
        "2023-10-9",
};

// ============ 独立配置/状态文件 5620GEN.json 的读写 ============
// 为避免与公用 config.json 冲突，此处使用独立命名的 5620GEN.json，
// 存放于 DLL 同目录（沿用原有逻辑）。
// 内容：
//   {
//     "firmware":   "..."             指向源固件的路径（记录用途）
//     "mac_offset": "0x7DF100"        MAC 写入固件的偏移
//     "mac_prefix": "78:44:FD"        固定 MAC 前缀（OUI）
//     "last_mac":   ""                上次使用的 MAC，用于 +8 递增防重复
//   }
#define MAC_CFG_FILE "5620GEN.json"

// 将 mac 渲染成 "78:44:FD:01:2A:80" 形式的字符串
static void MacToHexString(const unsigned char* mac, char* out) {
	sprintf_s(out, 32, "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// 从字符串 "xx:xx:xx:xx:xx:xx" 解析回 6 字节，成功返回 true
static bool HexStringToMac(const char* s, unsigned char* mac) {
	unsigned int v[6];
	int n = sscanf_s(s, "%02X:%02X:%02X:%02X:%02X:%02X",
		&v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
	if (n != 6) return false;
	for (int i = 0; i < 6; ++i) mac[i] = (unsigned char)v[i];
	return true;
}

// 获取 5620GEN.json 的完整路径（DLL 同目录）
static std::string GetCfgPath() {
	char dllDir[MAX_PATH];
	HMODULE hDll = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&GetCfgPath, &hDll);
	GetModuleFileNameA(hDll, dllDir, (DWORD)sizeof(dllDir));
	char* slash = strrchr(dllDir, '\\');
	if (slash) *(slash + 1) = '\0';
	else dllDir[0] = '\0';
	return std::string(dllDir) + MAC_CFG_FILE;
}

// 从 JSON 文本中提取 "key": "value" 的值（value 两侧需带引号）
static std::string GetJsonValue(const std::string& content, const std::string& key) {
	std::string token = "\"" + key + "\"";
	size_t pos = content.find(token);
	if (pos == std::string::npos) return "";
	size_t colon = content.find(':', pos);
	if (colon == std::string::npos) return "";
	size_t q1 = content.find('"', colon + 1);
	if (q1 == std::string::npos) return "";
	size_t q2 = content.find('"', q1 + 1);
	if (q2 == std::string::npos) return "";
	return content.substr(q1 + 1, q2 - q1 - 1);
}

// 读取 5620GEN.json 中记录的上次 MAC。不存在或无效返回 false
static bool ReadLastMac(unsigned char* lastMac) {
	std::string path = GetCfgPath();
	std::ifstream ifs(path);
	if (!ifs.is_open()) {
		std::cerr << "Cannot open " << path << std::endl;
		return false;
	}
	std::string content((std::istreambuf_iterator<char>(ifs)),
		std::istreambuf_iterator<char>());
	ifs.close();

	std::string macStr = GetJsonValue(content, "last_mac");
	return HexStringToMac(macStr.c_str(), lastMac);
}

// 读取 5620GEN.json 中记录的前缀（如 "78:44:FD"），成功返回 true
static bool ReadMacPrefix(unsigned char* prefix) {
	std::string path = GetCfgPath();
	std::ifstream ifs(path);
	if (!ifs.is_open()) return false;
	std::string content((std::istreambuf_iterator<char>(ifs)),
		std::istreambuf_iterator<char>());
	ifs.close();

	std::string preStr = GetJsonValue(content, "mac_prefix");
	// 前缀只取前 3 组，如 "78:44:FD"
	unsigned int v[3];
	int n = sscanf_s(preStr.c_str(), "%02X:%02X:%02X", &v[0], &v[1], &v[2]);
	if (n != 3) return false;
	prefix[0] = (unsigned char)v[0];
	prefix[1] = (unsigned char)v[1];
	prefix[2] = (unsigned char)v[2];
	return true;
}

// 读取 5620GEN.json 中记录的 MAC 写入偏移，默认 0x7DF100
static UINT ReadMacOffset() {
	std::string path = GetCfgPath();
	std::ifstream ifs(path);
	if (!ifs.is_open()) return 0x7DF100;
	std::string content((std::istreambuf_iterator<char>(ifs)),
		std::istreambuf_iterator<char>());
	ifs.close();

	std::string offStr = GetJsonValue(content, "mac_offset");
	if (offStr.empty()) return 0x7DF100;
	unsigned int off = 0;
	if (sscanf_s(offStr.c_str(), "%x", &off) != 1) return 0x7DF100;
	return (UINT)off;
}

// 将新 MAC 写回 5620GEN.json（保留 firmware/mac_offset/mac_prefix，仅更新 last_mac）
static void WriteLastMac(const unsigned char* mac) {
	std::string path = GetCfgPath();
	char macStr[32];
	MacToHexString(mac, macStr);

	// 读取原文件内容，若存在则保留其它字段
	std::string firmware, off, pre;
	std::ifstream ifs(path);
	if (ifs.is_open()) {
		std::string content((std::istreambuf_iterator<char>(ifs)),
			std::istreambuf_iterator<char>());
		ifs.close();
		firmware = GetJsonValue(content, "firmware");
		off = GetJsonValue(content, "mac_offset");
		pre = GetJsonValue(content, "mac_prefix");
	}
	if (off.empty()) off = "0x7DF100";
	if (pre.empty()) pre = "78:44:FD";

	std::ofstream ofs(path);
	if (ofs.is_open()) {
		ofs << "{\n"
			<< "  \"firmware\": \"" << firmware << "\",\n"
			<< "  \"mac_offset\": \"" << off << "\",\n"
			<< "  \"mac_prefix\": \"" << pre << "\",\n"
			<< "  \"last_mac\": \"" << macStr << "\"\n"
			<< "}\n";
		ofs.close();
	}
	else {
		std::cerr << "Cannot write " << path << std::endl;
	}
}

// 生成 MTK 主 MAC
// 前缀读取自 5620GEN.json 的 mac_prefix（默认 78:44:FD）；
// 后三字节在随机基址上：
//   - 若 JSON 中没有记录，则随机生成基址（末字节末尾对齐 0）
//   - 若 JSON 中有上次 MAC，则在上次基础上整个 MAC +8 递增
// 保证每次末尾为 0 或 8，且不与上次重复。
static void GenerateMtkMac(unsigned char* mac) {
	// 前缀从配置读取，失败则用默认 78:44:FD
	unsigned char prefix[3] = { 0x78, 0x44, 0xFD };
	if (!ReadMacPrefix(prefix)) { /* 使用默认前缀 */ }
	mac[0] = prefix[0];
	mac[1] = prefix[1];
	mac[2] = prefix[2];

	unsigned char lastMac[6];
	if (ReadLastMac(lastMac)) {
		// 在 lastMac 对应后三字节基础上 +8 递增
		unsigned int low24 = ((unsigned int)lastMac[5]) |
			((unsigned int)lastMac[4] << 8) |
			((unsigned int)lastMac[3] << 16);
		low24 += 8;                     // 每次 +8，保证末尾 0/8 交替且不重复
		// 强制末尾低3位为0（被8整除）→ 末尾必为 0 或 8
		low24 &= ~0x7u;
		mac[3] = (unsigned char)((low24 >> 16) & 0xFF);
		mac[4] = (unsigned char)((low24 >> 8) & 0xFF);
		mac[5] = (unsigned char)(low24 & 0xFF);
	}
	else {
		// 首次：随机生成基址，末字节末尾对齐 0（末尾必为 0）
		srand((unsigned)time(nullptr));
		mac[3] = (unsigned char)(rand() & 0xFF);
		mac[4] = (unsigned char)(rand() & 0xFF);
		mac[5] = (unsigned char)(rand() & 0xFF);
		mac[5] &= ~0x7u;                // 清除低3位 → 末尾为 0x?0
	}
}

GETSERIAL_API pSTRUCOPERATOR GetSerial(unsigned char* pCode, unsigned char* pEEdata,
	unsigned char* pUserRow, unsigned char* pID, char* pSerial,
	unsigned char* pUniqueKey, unsigned char* pConfig)
{
	// 主 MAC 注入地址：读取自 5620GEN.json 的 mac_offset，默认 0x7DF100
	const UINT macAddr = ReadMacOffset();

	// 解析当前序列号（若无效则从 0 开始）——这部分保留原有序列号逻辑，不与 MAC 冲突
	int SerialNumber = atoi(pSerial);
	if (SerialNumber < 0) SerialNumber = 0;

	// 生成 6 字节主 MAC（基于随机基址或上次值 +8 递增）
	unsigned char mac[6];
	GenerateMtkMac(mac);

	// 将 MAC 覆盖写入原始固件的 macAddr 起始处（覆盖插入，共 6 字节）
	for (int i = 0; i < 6; ++i) {
		pCode[macAddr + i] = mac[i];
	}

	// 将本次 MAC 写回独立状态文件，供下次 +8 递增（保证不重复）
	WriteLastMac(mac);

	// 向上位机上报本次修改的区域：CODE 区，从 macAddr 起长度 6 字节
	OprateResult.nCount = 1;
	OprateResult.MemoryType[0] = CODE_MEMORY;
	OprateResult.StartAddr[0] = macAddr;
	OprateResult.Len[0] = 6;

	// 记录序列号信息，供上位机保存追溯
	sprintf_s(OprateResult.SerialRecord, sizeof(OprateResult.SerialRecord), "%.8d", SerialNumber);

	// 序列号加 1 并写回，供下一台设备递增使用
	SerialNumber++;
	sprintf_s(pSerial, 8 + 1, "%.8d", SerialNumber);

	return &OprateResult;  //返回烧录结果
}

GETSERIAL_API pICINFORMATION GetCurrentIcInfo(void)
{
	return &IcInformation; //直接返回IC信息的指针
}

int EndProgAccess(int state)
{
	std::ofstream file("error_log.txt", std::ios::app);
	if (file.is_open())
	{
		if (state == 0)
		{
			// 正确时的处理
			file << "Success" << std::endl;
		}
		else
		{
			// 错误时的处理
			file << "Error Code: " << state << std::endl;
		}
		file.close();
	}
	else
	{
		// 打开文件失败的情况
	}

	return 0; // 需返回0
}