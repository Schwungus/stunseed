#include <rtc/rtc.hpp>

#include "stunseed.h"

static rtc::Configuration stunseed_rtc_config;

static void stunseed_rtc_log(rtc::LogLevel level, const std::string& line) {
	stunseed_log_level log_level = stunseed_log_level::STUNSEED_LOG_INFO;

	if (level != rtc::LogLevel::Info)
		log_level = stunseed_log_level::STUNSEED_LOG_WARN;

	stunseed_log(log_level, "%s", line.c_str());
}

extern "C" {
void stunseed_glue_set_stun_server() {
	stunseed_rtc_config.iceServers.emplace_back(STUNSEED_DEFAULT_STUN);
}

void stunseed_glue_set_rtc_logger() {
	rtc::InitLogger(rtc::LogLevel::Warning, stunseed_rtc_log);
}
}
