#include"CustomMCUAlgo.h"
#include<cstdlib>
#include<cstdio>
#include<cstring>
#include<ctime>
#include<fstream>
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

// ============ 独立 MAC 状态文件的读写（不与原有序列号逻辑冲突） ============
// MAC 状态文件路径：与 DLL 同目录下的 mac_state.json
// 格式：
//   { "last_mac": "" }                      初始
//   { "last_mac": "78:44:FD:01:2A:80" }     记录上一次使用的 MAC
// 作用：保证每次生成的 MAC 都在上次基础上 +8 递增，避免量产重复。

// 获取 DLL 所在目录，拼接完整路径（用于读写 mac_state.json）
static void GetDllDir(char* outBuf, size_t bufSize) {
	HMODULE hDll = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&GetDllDir, &hDll);
	GetModuleFileNameA(hDll, outBuf, (DWORD)bufSize);
	char* slash = strrchr(outBuf, '\\');
	if (slash) *(slash + 1) = '\0';
	else outBuf[0] = '\0';
}

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

// 读取 mac_state.json 中记录的上次 MAC。不存在或无效返回 false
static bool ReadLastMac(unsigned char* lastMac) {
	char dllDir[MAX_PATH];
	GetDllDir(dllDir, sizeof(dllDir));
	std::string path = std::string(dllDir) + "mac_state.json";

	std::ifstream ifs(path);
	if (!ifs.is_open()) {
		std::cerr << "Cannot open mac_state.json: " << path << std::endl;
		return false;
	}
	std::string line;
	while (std::getline(ifs, line)) {
		// 查找 "last_mac"
		size_t pos = line.find("last_mac");
		if (pos == std::string::npos) continue;
		// 查找冒号后的引号内容
		size_t q1 = line.find('"', pos + 8);
		if (q1 == std::string::npos) continue;
		size_t q2 = line.find('"', q1 + 1);
		if (q2 == std::string::npos) continue;
		std::string macStr = line.substr(q1 + 1, q2 - q1 - 1);
		return HexStringToMac(macStr.c_str(), lastMac);
	}
	return false;
}

// 将新 MAC 写回 mac_state.json
static void WriteLastMac(const unsigned char* mac) {
	char dllDir[MAX_PATH];
	GetDllDir(dllDir, sizeof(dllDir));
	std::string path = std::string(dllDir) + "mac_state.json";

	char macStr[32];
	MacToHexString(mac, macStr);

	std::ofstream ofs(path);
	if (ofs.is_open()) {
		ofs << "{\n  \"last_mac\": \"" << macStr << "\"\n}\n";
		ofs.close();
	}
	else {
		std::cerr << "Cannot write mac_state.json: " << path << std::endl;
	}
}

// 生成 MTK 主 MAC
// 前缀固定 78:44:FD；后三字节在随机基址上：
//   - 若 JSON 中没有记录，则随机生成基址（末字节末尾对齐 0）
//   - 若 JSON 中有上次 MAC，则在上次基础上整个 MAC +8 递增
// 保证每次末尾为 0 或 8，且不与上次重复。
static void GenerateMtkMac(unsigned char* mac) {
	// 前缀固定
	static const unsigned char prefix[3] = { 0x78, 0x44, 0xFD };
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
	// 主 MAC 注入地址：固件偏移 0x7DF100
	const UINT macAddr = 0x7DF100;

	// 解析当前序列号（若无效则从 0 开始）——这部分保留原有序列号逻辑，不与 MAC 冲突
	int SerialNumber = atoi(pSerial);
	if (SerialNumber < 0) SerialNumber = 0;

	// 生成 6 字节主 MAC（基于随机基址或上次值 +8 递增）
	unsigned char mac[6];
	GenerateMtkMac(mac);

	// 将 MAC 覆盖写入原始固件的 0x7DF100 起始处（覆盖插入，共 6 字节）
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