#include "ConfigReader.h"

#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/error/en.h"
#include<iostream>


ConfigReader::ConfigReader(const std::string& path) {
    readConfig(path);
}

void ConfigReader::readConfig(const std::string& path) {
    FILE* fp = nullptr;
    // 使用 fopen_s 打开文件
    if (fopen_s(&fp, path.c_str(), "r") != 0 || fp == nullptr) {
        std::cerr << "Could not open file: " << path << std::endl;
        return;
    }

    // 创建一个读取缓冲区
    char readBuffer[65536];
    rapidjson::FileReadStream stream(fp, readBuffer, sizeof(readBuffer));

    // 定义 JSON 文档对象
    rapidjson::Document document;

    // 解析 JSON
    document.ParseStream(stream);

    // 关闭文件
    fclose(fp);

    // 判断解析是否成功
    if (document.HasParseError()) {
        std::cerr << "Error parsing JSON: " << rapidjson::GetParseError_En(document.GetParseError()) << std::endl;
        return;
    }

    // 读取 mcu 字段
    if (document.HasMember("mcu") && document["mcu"].IsString()) {
        supported_mcus.push_back(document["mcu"].GetString());
    }

    // 读取 data 数组
    if (document.HasMember("data") && document["data"].IsArray()) {
        const auto& dataArray = document["data"];
        for (const auto& item : dataArray.GetArray()) {
            std::size_t startLocation;
            std::string file;

            if (item.HasMember("startLocation") && item["startLocation"].IsString()) {
                startLocation = std::stoi(item["startLocation"].GetString(),nullptr,16);
                std::cout << "startLocaton:" << startLocation<<std::endl;
            }
            if (item.HasMember("file") && item["file"].IsString()) {
                file = item["file"].GetString();
            }

            configs_.emplace_back(startLocation, file);
        }
    }
}