#include"CustomMCUAlgo.h"
#include<iostream>
#include<fstream>
#include<random>
#include<sstream>
#include<cstring>
#include"ConfigReader.h"

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


void generateRandonValue(int& a, int& b, int& c);
ICINFORMATION IcInformation = // 声明芯片编程相关信息
    {
        "@", // 当前烧录芯片的型号名，编号前并带冒号提示，型号要相同
        // 当上位机在烧录时，通过判断DLL自定的字符是否匹配所选的芯片
        // 若是不相同的字符则跳过

        LINECOUNTER, // 提示烧录时使用的对话框输出信息(5行)，当信息完全
        "[DEVICE]", // 只用在显示的信息行中，第5行=LINECOUNTER
        "custom algo",
        "[Description]",
        "zhaoshan custom algo",
        "2023-10-9",
};

std::vector<unsigned char> ReadHexValuesFromFile(const std::string& filename) {
	std::vector<unsigned char> intValues;
	std::ifstream file(filename, std::ios::binary); // 以二进制模式打开文件

	if (!file) {
		std::cerr << "Failed to open file: " << filename << std::endl;
		return intValues; // 返回空容器
	}

	unsigned char byte;
	while (file.read(reinterpret_cast<char*>(&byte), sizeof(byte))) {
		intValues.push_back(byte); // 将读取到的字节添加到 vector 中
	}

	file.close(); // 关闭文件
	return intValues; // 返回读取到的字节数组
}

void WriteBuffer(unsigned char* pCode, const ConfigReader& configs) {
	int modifyCount = 0;

	for (int i = 0; i < configs.ConfigCount(); ++i) {
		const auto& config = configs.GetConfig(i);
		auto data_file = config.data_file_;
		auto start_location = config.start_location_;
		// 用新代码读取待写入的十六进制数据
		auto intValues = ReadHexValuesFromFile(data_file);

		std::stringstream confirmStream;
		confirmStream << "确认将文件数据写入芯片闪存?\n\n"
			<< "文件: " << data_file << "\n"
			<< "起始地址: 0x" << std::hex << start_location << "\n"
			<< "字节数: " << std::dec << intValues.size() << "\n\n"
			<< (intValues.size() > 0 ? "前 16 字节(hex): " : "");
		for (size_t k = 0; k < intValues.size() && k < 16; ++k) {
			confirmStream << std::uppercase << std::hex
				<< static_cast<int>(intValues[k]) << " ";
		}

		int confirm = MessageBoxA(NULL, confirmStream.str().c_str(),
			"写入闪存确认", MB_YESNO | MB_ICONQUESTION);
		if (confirm != IDYES) {
			std::cout << "User cancelled: " << data_file << std::endl;
			continue;
		}

		// 经过用户确认后，再把数据写入 pCode
		for (size_t j = 0; j < intValues.size(); ++j) {		
			pCode[start_location + j] = intValues[j];					
		}
		++modifyCount;
	}
	OprateResult.nCount = modifyCount;

	std::cout << "Total modified bytes: " << modifyCount << std::endl;
}

// 获取 DLL 自身所在目录，并拼接出配置文件的完整路径
// 这样即使 T48 上位机的当前工作目录不同，也能正确找到 config.json
static void GetConfigPathFromDll(char* outBuf, size_t bufSize) {
	HMODULE hDll = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&GetConfigPathFromDll, &hDll);
	GetModuleFileNameA(hDll, outBuf, (DWORD)bufSize);
	// 去掉 DLL 文件名，保留所在目录
	char* slash = strrchr(outBuf, '\\');
	if (slash) *(slash + 1) = '\0';
	strncat_s(outBuf, bufSize, "config.json", _TRUNCATE);
}

GETSERIAL_API pSTRUCOPERATOR GetSerial(unsigned char* pCode, unsigned char* pEEdata,
	unsigned char* pUserRow, unsigned char* pID, char* pSerial,
	unsigned char* pUniqueKey, unsigned char* pConfig)
{
	// 针对 DLL 所在目录拼接配置路径，保证上位机在任何工作目录下都能读到
	char configPath[MAX_PATH];
	GetConfigPathFromDll(configPath, sizeof(configPath));

	auto config_reader = ConfigReader(configPath);

	if (config_reader.ConfigCount() == 0) {
		// 弹出错误提示，方便定位"为何无法烧写/为何不弹确认框"
		std::stringstream err;
		err << "无法加载配置文件:\n" << configPath
			<< "\n\n请确认该文件存在且格式正确。";
		MessageBoxA(NULL, err.str().c_str(), "自定义算法错误", MB_OK | MB_ICONERROR);
		return &OprateResult;
	}

	WriteBuffer(pCode, config_reader);
	
	return &OprateResult;
	int i;
	int  SerialNumber;
	SerialNumber = atoi(pSerial);//将字符串转换为10进制数据
	//在对话框显示当前序列号，注意地址要符合芯片容量
	/////////////////////////////////////////////////////////////////////
	UINT Code_StartAdd = 0x4007;       //code区修改
	


	const UINT firstAdd = 0x40007;
	const UINT secondAdd = 0x4002b;
	const UINT thirdAdd = 0x40031;
	//CODE区从0x3545开始的32字节全部修改为TestValue

	int a=-1, b=-2, c=-3;
	generateRandonValue(a, b, c);
	
	pCode[firstAdd] = a;
	pCode[firstAdd + 1] = b;
	pCode[firstAdd + 2] = c;

    generateRandonValue(a, b, c);
	pCode[secondAdd] = a;
	pCode[secondAdd + 1] = b;
	pCode[secondAdd + 2] = c;

	generateRandonValue(a, b, c);
	pCode[thirdAdd] = a;
	pCode[thirdAdd + 1] = b;
	pCode[thirdAdd + 2] = c;

	OprateResult.nCount = 3;  //修改了4处

	OprateResult.MemoryType[0] = CODE_MEMORY;       //第1处修改到的存储器类型
	OprateResult.StartAddr[0] = firstAdd;     //第1处的开始地址
	OprateResult.Len[0] = 3;                //第1处的长度

	OprateResult.MemoryType[1] = CODE_MEMORY;       //第2处修改到的存储器类型
	OprateResult.StartAddr[1] = secondAdd;     //第2处的开始地址
	OprateResult.Len[0] = 3;                //第2处的长度

	OprateResult.MemoryType[2] = CODE_MEMORY;       //第3处修改到的存储器类型
	OprateResult.StartAddr[2] = thirdAdd;     //第3处的开始地址
	OprateResult.Len[0] = 3;                //第3处的长度


	std::stringstream stream;

	stream << SerialNumber;
	
	sprintf_s(OprateResult.SerialRecord, "%.8d", SerialNumber);


	SerialNumber++;//序列号加1
	//计算出新的序列号，必须为8位对应的程序可记录当前的
	sprintf_s(pSerial,8+1,"%.8d",SerialNumber);

	return &OprateResult;  //返回烧录结果
}

GETSERIAL_API pICINFORMATION GetCurrentIcInfo(void)
{

	auto config_reader = ConfigReader("./config.json");

	auto mcus = config_reader.getSupportedMcus();

	

	return &IcInformation; //直接返回IC信息的指针
}

#include <fstream>

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

void generateRandonValue(int& a, int& b, int& c) {
	if (a < 0 && b < 0 && c < 0) {
		// 随机生成三个数并确保 a、b、c 之间互不相同
		int min = 0;
		int max = 150;
		do {

			srand(static_cast<unsigned>(time(nullptr)));
			a = (rand() % (max - min + 1)) + min;; // 生成 0 到 150 之间的随机整数
			b = (rand() % (max - min + 1)) + min;;
			c = (rand() % (max - min + 1)) + min;;
		} while (a == b || a == c || b == c);
	}
	else {
		a++;
		b++;
		c++;
	}
}