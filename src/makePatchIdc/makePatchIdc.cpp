#include <sdw.h>

int UMain(int argc, UChar* argv[])
{
	if (argc != 4)
	{
		return 1;
	}
	FILE* fp = UFopen(argv[1], USTR("rb"), false);
	if (fp == nullptr)
	{
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	u32 uOldSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	vector<u8> vOld(uOldSize);
	if (uOldSize != 0)
	{
		fread(&*vOld.begin(), 1, uOldSize, fp);
	}
	fclose(fp);
	fp = UFopen(argv[2], USTR("rb"), false);
	if (fp == nullptr)
	{
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	u32 uNewSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	vector<u8> vNew(uNewSize);
	if (uNewSize != 0)
	{
		fread(&*vNew.begin(), 1, uNewSize, fp);
	}
	fclose(fp);
	if (uNewSize <= uOldSize && uNewSize != 0 && memcmp(&*vNew.begin(), &*vOld.begin(), uNewSize) == 0)
	{
		return 0;
	}
	fp = UFopen(argv[3], USTR("wb"), false);
	if (fp == nullptr)
	{
		return 1;
	}
	fprintf(fp, "#include <idc.idc>\r\n");
	fprintf(fp, "\r\n");
	fprintf(fp, "static main()\r\n");
	fprintf(fp, "{\r\n");
	u32 uSizeMin = min<u32>(uOldSize, uNewSize);
	if (uSizeMin != 0)
	{
		for (u32 i = 0; i < uSizeMin; i++)
		{
			if (vNew[i] != vOld[i])
			{
				fprintf(fp, "\tpatch_byte(0x%X, 0x%02X);\r\n", i, vNew[i]);
			}
		}
	}
	if (uNewSize > uSizeMin)
	{
		for (u32 i = uSizeMin; i < uNewSize; i++)
		{
			fprintf(fp, "\tpatch_byte(0x%X, 0x%02X);\r\n", i, vNew[i]);
		}
	}
	fprintf(fp, "\tmsg(\"patch over!\");\r\n");
	fprintf(fp, "}\r\n");
	fclose(fp);
	return 0;
}
