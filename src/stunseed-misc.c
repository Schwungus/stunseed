#include <string.h>

#include "stunseed.h"

const char* stunseed_basename(const char* path) {
	const char* s = strrchr(path, '/');
	if (!s)
		s = strrchr(path, '\\');
	return s ? s + 1 : path;
}
