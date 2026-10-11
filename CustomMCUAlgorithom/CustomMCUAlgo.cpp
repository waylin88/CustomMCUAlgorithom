#include"CustomMCUAlgo.h"
#include<cstdlib>
#include<cstdio>
#include<cstring>
#include<ctime>
#include<fstream>
#include<iostream>
#include<string>
#include<vector>

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

// 获取 5620GEN.json 的完整路径。
// 由于上位机(如 Xgpro.exe 所在目录)与 DLL 所在目录可能不同，这里按优先级依次检索，
// 命中第一个存在的文件：
//   1) DLL 同目录  2) 上位机/进程 EXE 目录  3) 当前工作目录
static std::string GetCfgPath() {
	std::string dllDir, exeDir;

	// --- 候选1：DLL 同目录 ---
	char dllBuf[MAX_PATH];
	HMODULE hDll = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&GetCfgPath, &hDll)) {
		if (GetModuleFileNameA(hDll, dllBuf, (DWORD)sizeof(dllBuf)) > 0) {
			std::string d = dllBuf;
			size_t slash = d.find_last_of('\\');
			if (slash != std::string::npos) { d[slash + 1] = '\0'; dllDir = d; }
		}
	}
	if (!dllDir.empty()) {
		std::string cfg = dllDir + MAC_CFG_FILE;
		std::ifstream t(cfg.c_str());
		if (t.good()) return cfg;
	}

	// --- 候选2：当前进程(上位机/Xgpro) EXE 目录 ---
	char exeBuf[MAX_PATH];
	if (GetModuleFileNameA(nullptr, exeBuf, (DWORD)sizeof(exeBuf)) > 0) {
		std::string e = exeBuf;
		size_t slash = e.find_last_of('\\');
		if (slash != std::string::npos) { e[slash + 1] = '\0'; exeDir = e; }
	}
	if (!exeDir.empty()) {
		std::string cfg = exeDir + MAC_CFG_FILE;
		std::ifstream t(cfg.c_str());
		if (t.good()) return cfg;
	}

	// --- 候选3：当前工作目录 ---
	{
		std::ifstream t(MAC_CFG_FILE);
		if (t.good()) return std::string(MAC_CFG_FILE);
	}

	// 兜底：返回 DLL 同目录路径（写入时用，若都不存在则新建在 DLL 同目录）
	return dllDir.empty() ? std::string(MAC_CFG_FILE) : dllDir + MAC_CFG_FILE;
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

// 读取 5620GEN.json 中记录的 firmware 路径；失败返回空串
static std::string ReadFirmwarePath() {
	std::string path = GetCfgPath();
	std::ifstream ifs(path);
	if (!ifs.is_open()) return "";
	std::string content((std::istreambuf_iterator<char>(ifs)),
		std::istreambuf_iterator<char>());
	ifs.close();
	return GetJsonValue(content, "firmware");
}

// 将 firmware 路径指定的源固件文件整体加载到 pCode 缓冲区。
// 成功返回 true；失败返回 false（调用方回退使用上位机传入的数据）。
static bool LoadFirmware(unsigned char* pCode, size_t maxSize, size_t& loadedSize) {
	std::string fw = ReadFirmwarePath();
	if (fw.empty()) return false;

	std::ifstream f(fw, std::ios::binary);
	if (!f.is_open()) {
		std::cerr << "Cannot open firmware: " << fw << std::endl;
		return false;
	}
	// 获取文件大小
	f.seekg(0, std::ios::end);
	std::streamoff size = f.tellg();
	f.seekg(0, std::ios::beg);
	if (size <= 0) {
		f.close();
		return false;
	}
	// 若文件超过缓冲区上限则截断读取最大可容纳部分
	size_t toRead = (size_t)size;
	if (toRead > maxSize) toRead = maxSize;
	f.read((char*)pCode, (std::streamsize)toRead);
	loadedSize = (size_t)f.gcount();
	f.close();
	return loadedSize > 0;
}

// 将新 MAC 写回 5620GEN.json（保留 firmware/mac_offset/mac_prefix/log_file，仅更新 last_mac）
static void WriteLastMac(const unsigned char* mac) {
	std::string path = GetCfgPath();
	char macStr[32];
	MacToHexString(mac, macStr);

	// 读取原文件内容，若存在则保留其它字段
	std::string firmware, off, pre, logf;
	std::ifstream ifs(path);
	if (ifs.is_open()) {
		std::string content((std::istreambuf_iterator<char>(ifs)),
			std::istreambuf_iterator<char>());
		ifs.close();
		firmware = GetJsonValue(content, "firmware");
		off = GetJsonValue(content, "mac_offset");
		pre = GetJsonValue(content, "mac_prefix");
		logf = GetJsonValue(content, "log_file");
	}
	if (off.empty()) off = "0x7DF100";
	if (pre.empty()) pre = "78:44:FD";

	std::ofstream ofs(path);
	if (ofs.is_open()) {
		ofs << "{\n"
			<< "  \"firmware\": \"" << firmware << "\",\n"
			<< "  \"mac_offset\": \"" << off << "\",\n"
			<< "  \"mac_prefix\": \"" << pre << "\",\n"
			<< "  \"last_mac\": \"" << macStr << "\",\n"
			<< "  \"log_file\": \"" << logf << "\"\n"
			<< "}\n";
		ofs.close();
	}
	else {
		std::cerr << "Cannot write " << path << std::endl;
	}
}

// 读取 5620GEN.json 中记录的日志文件路径；为空则返回默认 "error_log.txt"
static std::string ReadLogPath() {
	std::string path = GetCfgPath();
	std::ifstream ifs(path);
	if (!ifs.is_open()) return "error_log.txt";
	std::string content((std::istreambuf_iterator<char>(ifs)),
		std::istreambuf_iterator<char>());
	ifs.close();
	std::string lp = GetJsonValue(content, "log_file");
	return lp.empty() ? std::string("error_log.txt") : lp;
}

// 将路径字节串转成 Windows 宽字符。配置内容可能是 UTF-8 或本地 ANSI(GBK)，
// 这里先按 UTF-8 严格解码，失败则按本地 ANSI，从而兼容两种编码的中文路径。
static std::wstring PathToWide(const std::string& s) {
	std::wstring w;
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
	if (n > 0) {
		w.resize(n);
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), &w[0], n);
		return w;
	}
	n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
	if (n > 0) {
		w.resize(n);
		MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
	}
	return w;
}

// 宽字符打开读文件全文
static std::string ReadFileWide(const std::string& path) {
	FILE* f = _wfopen(PathToWide(path).c_str(), L"rb");
	if (!f) return std::string();
	std::string out;
	char buf[4096];
	size_t r;
	while ((r = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, r);
	fclose(f);
	return out;
}

// 确保给定完整文件路径的父目录存在（前向声明）
static bool EnsureParentDir(const std::string& filePath);

// 宽字符打开，以 UTF-8 方式追加一行（日志中文内容不会被破坏）
static bool AppendLineWide(const std::string& path, const std::string& line) {
	EnsureParentDir(path);
	FILE* f = _wfopen(PathToWide(path).c_str(), L"ab");
	if (!f) return false;
	fwrite(line.c_str(), 1, line.size(), f);
	fclose(f);
	return true;
}

// 确保给定完整文件路径的父目录存在；不存在则逐级创建。成功返回 true。
static bool EnsureParentDir(const std::string& filePath) {
	// 取最后一个 '/' 或 '\\' 之前的目录部分
	size_t pos = filePath.find_last_of("/\\");
	if (pos == std::string::npos) return true;  // 无目录，直接当前目录
	std::string dir = filePath.substr(0, pos);
	if (dir.empty()) return true;

	// 逐级创建
	std::string cur;
	for (size_t i = 0; i <= dir.size(); ++i) {
		if (i == dir.size() || dir[i] == '/' || dir[i] == '\\') {
			if (!cur.empty()) {
				CreateDirectoryW(PathToWide(cur).c_str(), nullptr);  // 已存在时返回 0，忽略
			}
			if (i < dir.size()) cur += dir[i];
		}
		else {
			cur += dir[i];
		}
	}
	return true;
}

// 追加一行烧录日志。格式：
//   序号;MAC去冒号;序列号;TP-Link_xxxx;TP-Link_xxxx_5G
// 其中 MAC 后4位取最后两字节组成，用于 wifi 名。
static void AppendLog(const unsigned char* mac) {
	std::string logPath = ReadLogPath();

	// 序号 = 已有行数 + 1（第一次为 1）
	int seq = 1;
	{
		std::string lc = ReadFileWide(logPath);
		size_t pos = 0;
		while (pos < lc.size()) {
			size_t nl = lc.find('\n', pos);
			if (nl == std::string::npos) { ++seq; break; }
			if (nl > pos) ++seq;
			pos = nl + 1;
		}
	}

	// MAC 去冒号，共12位hex
	char macHex[13];
	sprintf_s(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	// wifi 名尾 4 位：取最后两字节
	char last4[5];
	sprintf_s(last4, sizeof(last4), "%02X%02X", mac[4], mac[5]);

	// 序列号：前缀 117C792 + 6 位随机数
	srand((unsigned)time(nullptr) ^ ((unsigned)mac[5] << 8));
	int rnd = rand() % 1000000;
	char serial[32];
	sprintf_s(serial, sizeof(serial), "117C792%06d", rnd);

	// wifi 名称
	char wifi[48], wifi5g[48];
	sprintf_s(wifi, sizeof(wifi), "TP-Link_%s", last4);
	sprintf_s(wifi5g, sizeof(wifi5g), "TP-Link_%s_5G", last4);

	char line[256];
	sprintf_s(line, sizeof(line), "%d;%s;%s;%s;%s\n",
		seq, macHex, serial, wifi, wifi5g);
	// 追加一行（自动创建父目录，支持中文路径）
	if (!AppendLineWide(logPath, line)) {
		std::cerr << "Cannot open log: " << logPath << std::endl;
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

	// 从 5620GEN.json 的 firmware 路径加载源固件到 pCode 缓冲区。
	// 这里假定 pCode 缓冲区足够容纳固件文件；加载成功后会用文件内容覆盖原始数据。
	size_t loadedSize = 0;
	LoadFirmware(pCode, ~(size_t)0, loadedSize);
	(void)loadedSize;  // 仅作加载状态占位，后续若需按固件尺寸调整上报区域可在此使用

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

	// 追加一条烧录日志：序号;MAC;序列号;wifi名;wifi5g名
	AppendLog(mac);

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