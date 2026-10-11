#pragma once

#include<windows.h>
#ifdef GETSERIAL_EXPORTS
#define GETSERIAL_API __declspec(dllexport)
#else
#define GETSERIAL_API __declspec(dllimport)
#endif
///////////////////////////////////////////////////////////////////////
//只做烧录结束及校验用，此组缓冲区地址及长度应该与临时缓冲区一致
///////////////////////////////////////////////////////////////////////
//这个待修改固件的存储器缓冲区的数量,此区域位置和长度可以加大值
#define MAX_BLOCK      10  

////////////////////////////////////////////////////////////////////////////////////////////////////
//对应缓冲区的存储器类型,可自行添加
#define CODE_MEMORY    0
#define DATA_MEMORY    1
#define USERROW_MEMORY 2
#define ID_MEMORY      3
//可自行添加
typedef struct STRUCOPERATORTag
{
	int     nCount;                //要修改的块存储器数量，如果修改了几块缓冲区,就设定这个数量
	//设定时这个数量不可大于MAX_BLOCK
	char    SerialRecord[512];     //会回读应用程序,记录在文件中的序列号信息，最长512字符。且必须是标准ASCII字符
	//如果烧录需要记录每个块的具体信息
	UINT    MemoryType[MAX_BLOCK]; //要修改的块存储器类型可CODE_MEMORY、DATA_MEMORY、USERROW_MEMORY、ID_MEMORY中的一个
	UINT    StartAddr[MAX_BLOCK];  //每块缓冲区的开始地址
	UINT    Len[MAX_BLOCK];        //每块缓冲区被修改了的长度，可自定义的

}
STRUCOPERATOR, * pSTRUCOPERATOR;
///////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////
//以上结构只是是烧录时，应用程序中用于显示使用，设定这个结构的对应值。
//每次读取内存时，会按nCount缓冲区数量来自动循环显示规定位置的数据
//若nCount=1，则只显示一个区域，显示MemoryType[0]、StartAddr[0];Len[0];设定的区域
//设定值务必正确无误，否则根据设定的值取出芯片对应位置的数据
////////////////////////////////////////////////////////////////////////////////////////

//芯片信息结构，可自行添加
#define LINECOUNTER             5  //提示信息行数

typedef struct ICINFORMATIONTag
{
	char      IcName[50];                     //芯片型号
	int       NoteCount;                     //提示信息行数
	char      MemoryType[LINECOUNTER][100];  //信息行数
}
ICINFORMATION, * pICINFORMATION;

//DLL要导出的函数

//自动IC序列号计算函数
GETSERIAL_API pSTRUCOPERATOR GetSerial(unsigned char* pCode, unsigned char* pEEdata, unsigned char* pUserRow, unsigned char* pID, char* pSerial, unsigned char* pUniqueKey, unsigned char* pConfig);
GETSERIAL_API pICINFORMATION GetCurrentIcInfo(void); //返回当前DLL烧写的算法对应的芯片型号
GETSERIAL_API int EndProgAccess(int state); //DLL结束后