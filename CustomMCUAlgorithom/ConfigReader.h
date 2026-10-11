#pragma once

#include<string>
#include<vector>

class Config {
public:
	Config(std::size_t start_location, const std::string& data_file_):
		start_location_{ start_location }, data_file_{data_file_} {
	}

	std::size_t start_location_ = 0x00;
	std::string data_file_;
};
class ConfigReader
{
public:

	ConfigReader(const std::string& path);

	int ConfigCount() const {
		return configs_.size();
	};

	const Config& GetConfig(int index) const {
		return configs_.at(index);
	};

	auto getSupportedMcus() {
		return supported_mcus;
	}

private:

	std::vector<Config> configs_;

	std::vector<std::string> supported_mcus;

	void readConfig(const std::string& path);
};

