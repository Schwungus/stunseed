#include <stdlib.h>
#include <string.h>

#include "stunseed.h"

const char* stunseed_basename(const char* path) {
	const char* s = strrchr(path, '/');
	if (!s)
		s = strrchr(path, '\\');
	return s ? s + 1 : path;
}

void stunseed_generate_webtorrent_id(stunseed_webtorrent_id id) {
	for (int i = 0; i < sizeof(stunseed_webtorrent_id); i++)
		id[i] = (char)('A' + (rand() % 26));
}
