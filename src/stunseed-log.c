#include <stdarg.h>
#include <stdio.h>

#include "stunseed.h"

static stunseed_logger stunseed_logger_fn = stunseed_default_log;

void stunseed_set_logger(stunseed_logger new_logger) {
	stunseed_logger_fn = new_logger ? new_logger : stunseed_default_log;
}

void stunseed_log(stunseed_log_level level, const char* line, ...) {
	va_list args;
	va_start(args, line);
	stunseed_log_v(level, line, args);
	va_end(args);
}

void stunseed_log_v(stunseed_log_level level, const char* line, va_list args) {
	static char buf[1024] = {0};
	vsnprintf(buf, sizeof(buf) - 1, line, args);
	stunseed_logger_fn(level, buf);
}

void stunseed_default_log(stunseed_log_level level, const char* buf) {
	static const char* const level_names[] = {
		[STUNSEED_LOG_WARN] = "WARN",
		[STUNSEED_LOG_INFO] = "INFO",
	};
	fprintf(stderr, "[%s] %s\n", level_names[level], buf);
}
