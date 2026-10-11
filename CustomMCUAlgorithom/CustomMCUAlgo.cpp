#include"CustomMCUAlgo.h"
#include<cstdlib>
#include<cstdio>
#include<fstream>

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

// MTK 主 MAC 生成函数
// 前缀固定为 78:44:FD，中间两字节由序列号递增，末字节对齐为 0 或 8 结尾
// 输出 mac[0..5]，共 6 字节，符合"末尾不是 0 就是 8、+8 递增"的 MTK 格式
void GenerateMtkMac(int serialNumber, unsigned char* mac) {
	// 固定 OUI 前缀 78:44:FD
	mac[0] = 0x78;
	mac[1] = 0x44;
	mac[2] = 0xFD;

	// 中间两字节(第4、5字节)由序列号递增，保证量产唯一
	// 序列号范围 0~65535 映射到高、低字节
	int serial = serialNumber & 0xFFFF;
	mac[3] = (unsigned char)((serial >> 8) & 0xFF); // 高位
	mac[4] = (unsigned char)(serial & 0xFF);        // 低位

	// 末字节对齐：保证末尾是 0 或 8（低3位必须为0，能被8整除对齐）
	// 取序列号高字节与当前中间值进一步混合，避免末字节固定不变
	mac[5] = (unsigned char)((serial >> 4) & 0xF0); // 高位对齐到 0x?0
}

GETSERIAL_API pSTRUCOPERATOR GetSerial(unsigned char* pCode, unsigned char* pEEdata,
	unsigned char* pUserRow, unsigned char* pID, char* pSerial,
	unsigned char* pUniqueKey, unsigned char* pConfig)
{
	// 主 MAC 注入地址：固件偏移 0x7DF100
	const UINT macAddr = 0x7DF100;

	// 解析当前序列号（若无效则从 0 开始）
	int SerialNumber = atoi(pSerial);
	if (SerialNumber < 0) SerialNumber = 0;

	// 生成 6 字节主 MAC
	unsigned char mac[6];
	GenerateMtkMac(SerialNumber, mac);

	// 将 MAC 覆盖写入原始固件的 0x7DF100 起始处（覆盖插入，共 6 字节）
	for (int i = 0; i < 6; ++i) {
		pCode[macAddr + i] = mac[i];
	}

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