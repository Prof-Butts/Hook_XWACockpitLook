#pragma once

#include <cstring>
#include <cstdlib>


void* hexstr_to_char_address(unsigned char* address, const char* hexstr)
{
	size_t len = strlen(hexstr);
	if (len % 2 != 0)
		return NULL;
	size_t final_len = len / 2;
	unsigned char* chrs = address;
	for (size_t i = 0, j = 0; j < final_len; i += 2, j++)
		chrs[j] = (hexstr[i] % 32 + 9) % 25 * 16 + (hexstr[i + 1] % 32 + 9) % 25;
	return 0;
}

unsigned char* hexstr_to_char(const char* hexstr)
{
	size_t len = strlen(hexstr);
	if (len % 2 != 0)
		return NULL;
	size_t final_len = len / 2;
	unsigned char* chrs = (unsigned char*)malloc((final_len + 1) * sizeof(*chrs));
	for (size_t i = 0, j = 0; j < final_len; i += 2, j++)
		chrs[j] = (hexstr[i] % 32 + 9) % 25 * 16 + (hexstr[i + 1] % 32 + 9) % 25;
	chrs[final_len] = '\0';
	return chrs;
}

void CopyByteString(char *destination, unsigned char *source, int size)
{
	int count = 0;
	for (int i = 0; count < size; i++)
	{
		destination[i] = source[i];
		count++;
	}
}