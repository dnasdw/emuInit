#include <sdw.h>
#include <elfio/elfio.hpp>
#include <unicorn/unicorn.h>

using namespace ELFIO;

enum EMachine
{
	kMachineARM = EM_ARM,
	kMachineAARCH64 = EM_res183/* EM_AARCH64 ARM AARCH64 */,
};

int UMain(int argc, UChar* argv[])
{
	if (argc != 3)
	{
		return 1;
	}
	FILE* fp = UFopen(argv[1], USTR("rb"), false);
	if (fp == nullptr)
	{
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	u32 uElfSize = ftell(fp);
	if (uElfSize == 0)
	{
		fclose(fp);
		return 1;
	}
	fseek(fp, 0, SEEK_SET);
	vector<u8> vElf(uElfSize);
	fread(&*vElf.begin(), 1, vElf.size(), fp);
	fclose(fp);
	elfio elfFile;
	ifstream input;
	input.open(argv[1], ios::in | ios::binary);
	if (!input)
	{
		return 1;
	}
	if (!elfFile.load(input))
	{
		input.close();
		return 1;
	}
	input.close();
	u8 uClass = elfFile.get_class();
	if (uClass != ELFCLASS32 && uClass != ELFCLASS64)
	{
		return 1;
	}
	u8 uEncoding = elfFile.get_encoding();
	if (uEncoding != ELFDATA2LSB)
	{
		// support little endian only
		return 1;
	}
	u16 uType = elfFile.get_type();
	if (uType != ET_DYN)
	{
		// support shared object file only
		return 1;
	}
	u16 uMachine = elfFile.get_machine();
	switch (uMachine)
	{
	case kMachineARM:
		if (uClass != ELFCLASS32)
		{
			return 1;
		}
		break;
	case kMachineAARCH64:
		if (uClass != ELFCLASS64)
		{
			return 1;
		}
		break;
	default:
		return 1;
	}
	n32 nTextIndex = -1;
	n32 nRodataIndex = -1;
	n32 nDataIndex = -1;
	n32 nBssIndex = -1;
	n32 nInitArrayIndex = -1;
	n32 nRelaDynIndex = -1;
	u64 uTextAddressMin = 0;
	u64 uTextAddressMax = 0;
	u64 uBasicAddressMin = UINT32_MAX;
	u64 uBasicAddressMax = 0;
	n32 nSectionSize = elfFile.sections.size();
	for (n32 i = 0; i < nSectionSize; i++)
	{
		const section* pSection = elfFile.sections[i];
		if (pSection == nullptr)
		{
			return 1;
		}
		u64 uAddress = pSection->get_address();
		u64 uSize = pSection->get_size();
		string sName = pSection->get_name();
		if (sName == ".text")
		{
			nTextIndex = i;
			uTextAddressMin = uAddress;
			uTextAddressMax = uAddress + uSize;
			if (uAddress < uBasicAddressMin)
			{
				uBasicAddressMin = uAddress;
			}
			if (uAddress + uSize > uBasicAddressMax)
			{
				uBasicAddressMax = uAddress + uSize;
			}
		}
		else if (sName == ".rodata")
		{
			nRodataIndex = i;
			if (uAddress < uBasicAddressMin)
			{
				uBasicAddressMin = uAddress;
			}
			if (uAddress + uSize > uBasicAddressMax)
			{
				uBasicAddressMax = uAddress + uSize;
			}
		}
		else if (sName == ".data")
		{
			nDataIndex = i;
			if (uAddress < uBasicAddressMin)
			{
				uBasicAddressMin = uAddress;
			}
			if (uAddress + uSize > uBasicAddressMax)
			{
				uBasicAddressMax = uAddress + uSize;
			}
		}
		else if (sName == ".bss")
		{
			nBssIndex = i;
			if (uAddress < uBasicAddressMin)
			{
				uBasicAddressMin = uAddress;
			}
			if (uAddress + uSize > uBasicAddressMax)
			{
				uBasicAddressMax = uAddress + uSize;
			}
		}
		else if (sName == ".init_array")
		{
			nInitArrayIndex = i;
		}
		else if (sName == ".rela.dyn")
		{
			nRelaDynIndex = i;
		}
	}
	if (nTextIndex == -1 || nDataIndex == -1 || nInitArrayIndex == -1)
	{
		// support .text and .data and .init_array only
		fp = UFopen(argv[2], USTR("wb"), false);
		if (fp == nullptr)
		{
			return 1;
		}
		fwrite(&*vElf.begin(), 1, vElf.size(), fp);
		fclose(fp);
		return 0;
	}
	section* pTextSection = elfFile.sections[nTextIndex];
	section* pRodataSection = nullptr;
	if (nRodataIndex != -1)
	{
		pRodataSection = elfFile.sections[nRodataIndex];
	}
	section* pDataSection = elfFile.sections[nDataIndex];
	section* pBssSection = nullptr;
	if (nBssIndex != -1)
	{
		pBssSection = elfFile.sections[nBssIndex];
	}
	section* pInitArraySection = elfFile.sections[nInitArrayIndex];
	section* pRelaDynSection = nullptr;
	if (nRelaDynIndex != -1)
	{
		pRelaDynSection = elfFile.sections[nRelaDynIndex];
	}
	if (pTextSection->get_size() == 0 || pDataSection->get_size() == 0 || pInitArraySection->get_size() == 0)
	{
		fp = UFopen(argv[2], USTR("wb"), false);
		if (fp == nullptr)
		{
			return 1;
		}
		fwrite(&*vElf.begin(), 1, vElf.size(), fp);
		fclose(fp);
		return 0;
	}
	u64 uMemoryAddress4K = uBasicAddressMin / 4096 * 4096;
	u32 uMemorySize4K = static_cast<u32>(Align(uBasicAddressMax - uMemoryAddress4K, 4096));
	if (uMemorySize4K == 0)
	{
		return 1;
	}
	string sMemory(uMemorySize4K, 0);
	memcpy(&*sMemory.begin() + static_cast<u32>(pTextSection->get_address() - uMemoryAddress4K), pTextSection->get_data(), static_cast<u32>(pTextSection->get_size()));
	if (pRodataSection != nullptr)
	{
		memcpy(&*sMemory.begin() + static_cast<u32>(pRodataSection->get_address() - uMemoryAddress4K), pRodataSection->get_data(), static_cast<u32>(pRodataSection->get_size()));
	}
	memcpy(&*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), pDataSection->get_data(), static_cast<u32>(pDataSection->get_size()));
	map<n32, n32> mInitArrayRelaDynIndex;
	if (pRelaDynSection != nullptr)
	{
		relocation_section_accessor relaDynSection(elfFile, pRelaDynSection);
		u64 uInitArrayAddress = pInitArraySection->get_address();
		u64 uInitArraySize = pInitArraySection->get_size();
		string sInitArrayData(pInitArraySection->get_data(), static_cast<u32>(uInitArraySize));
		n32 nEnteyCount = static_cast<n32>(relaDynSection.get_entries_num());
		for (n32 i = 0; i < nEnteyCount; i++)
		{
			Elf64_Addr uOffset = 0;
			Elf_Word uSymbol = 0;
			Elf_Word uType = 0;
			Elf_Sxword nAddend = 0;
			if (!relaDynSection.get_entry(i, uOffset, uSymbol, uType, nAddend))
			{
				return 1;
			}
			if (uMachine == kMachineARM && uSymbol == 0 && uType == 23/* R_ARM_RELATIVE Adjust by program base. */ && uOffset >= uInitArrayAddress && uOffset + 4 <= uInitArrayAddress + uInitArraySize && !sInitArrayData.empty())
			{
				mInitArrayRelaDynIndex.insert(make_pair(static_cast<n32>((uOffset - uInitArrayAddress) / 4), i));
				memcpy(&*sInitArrayData.begin() + static_cast<u32>(uOffset - uInitArrayAddress), &nAddend, 4);
			}
			else if (uMachine == kMachineAARCH64 && uSymbol == 0 && uType == 1027/* R_AARCH64_RELATIVE Adjust by program base. */ && uOffset >= uInitArrayAddress && uOffset + 8 <= uInitArrayAddress + uInitArraySize && !sInitArrayData.empty())
			{
				mInitArrayRelaDynIndex.insert(make_pair(static_cast<n32>((uOffset - uInitArrayAddress) / 8), i));
				memcpy(&*sInitArrayData.begin() + static_cast<u32>(uOffset - uInitArrayAddress), &nAddend, 8);
			}
		}
		pInitArraySection->set_data(sInitArrayData);
	}
	vector<u8> vData(sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K + pDataSection->get_size()));
	vector<u8> vBss;
	if (pBssSection != nullptr && pBssSection->get_size() != 0)
	{
		vBss.assign(sMemory.begin() + static_cast<u32>(pBssSection->get_address() - uMemoryAddress4K), sMemory.begin() + static_cast<u32>(pBssSection->get_address() - uMemoryAddress4K + pBssSection->get_size()));
	}
	set<n32> sInvalidIndex;
	array_section_accessor initArraySection(elfFile, pInitArraySection);
	n32 nEntryCount = static_cast<n32>(initArraySection.get_entries_num());
	for (n32 i = 0; i < nEntryCount; i++)
	{
		u64 uAddress = 0;
		if (!initArraySection.get_entry(i, uAddress))
		{
			return 1;
		}
		printf(".init_array[%d]: %8llX\n", i, uAddress);
		if (uAddress == 0)
		{
			continue;
		}
		if (uAddress < uTextAddressMin || uAddress >= uTextAddressMax)
		{
			return 1;
		}
		uc_engine* pUc = nullptr;
		uc_err eErr = UC_ERR_OK;
		if (uMachine == kMachineARM)
		{
			if (uAddress % 2 == 0)
			{
				eErr = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &pUc);
			}
			else
			{
				eErr = uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &pUc);
			}
		}
		else if (uMachine == kMachineAARCH64)
		{
			eErr = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &pUc);
		}
		if (eErr != UC_ERR_OK)
		{
			printf("Failed on uc_open() with error returned: %u (%s)\n", eErr, uc_strerror(eErr));
			return 1;
		}
		eErr = uc_mem_map_ptr(pUc, uMemoryAddress4K, uMemorySize4K, UC_PROT_ALL, &*sMemory.begin());
		eErr = uc_mem_map(pUc, 0x60000000, 0x200000, UC_PROT_READ | UC_PROT_WRITE);
		n32 nSPRegId = -1;
		n32 nLRRegId = -1;
		n32 nPCRegId = -1;
		if (uMachine == kMachineARM)
		{
			nSPRegId = UC_ARM_REG_SP;
			nLRRegId = UC_ARM_REG_LR;
			nPCRegId = UC_ARM_REG_PC;
		}
		else if (uMachine == kMachineAARCH64)
		{
			nSPRegId = UC_ARM64_REG_SP;
			nLRRegId = UC_ARM64_REG_LR;
			nPCRegId = UC_ARM64_REG_PC;
		}
		u64 uSP = 0x60100000;
		eErr = uc_reg_write(pUc, nSPRegId, &uSP);
		u64 uLR = 0x68000000;
		eErr = uc_reg_write(pUc, nLRRegId, &uLR);
		u64 uPC = 0x00000000;
		eErr = uc_reg_write(pUc, nPCRegId, &uPC);
		eErr = uc_emu_start(pUc, uAddress, uTextAddressMax - uAddress, 10000000, 0);
		if (eErr == UC_ERR_OK)
		{
			// timeout
			memcpy(&*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), &*vData.begin(), vData.size());
			if (!vBss.empty())
			{
				memcpy(&*sMemory.begin() + static_cast<u32>(pBssSection->get_address() - uMemoryAddress4K), &*vBss.begin(), vBss.size());
			}
		}
		else if (eErr == UC_ERR_FETCH_UNMAPPED)
		{
			eErr = uc_reg_read(pUc, nPCRegId, &uPC);
			if (uPC != uLR)
			{
				if (uPC < uTextAddressMin || uPC >= uTextAddressMax)
				{
					memcpy(&*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), &*vData.begin(), vData.size());
					if (!vBss.empty())
					{
						memcpy(&*sMemory.begin() + static_cast<u32>(pBssSection->get_address() - uMemoryAddress4K), &*vBss.begin(), vBss.size());
					}
				}
				else
				{
					eErr = uc_emu_stop(pUc);
					eErr = uc_close(pUc);
					return 1;
				}
			}
			else if (memcmp(&*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), &*vData.begin(), vData.size()) != 0 && (vBss.empty() || memcmp(&*sMemory.begin() + static_cast<u32>(pBssSection->get_address() - uMemoryAddress4K), &*vBss.begin(), vBss.size()) == 0))
			{
				sInvalidIndex.insert(i);
				memcpy(&*vData.begin(), &*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), vData.size());
				if (!vBss.empty())
				{
					memcpy(&*vBss.begin(), &*sMemory.begin() + static_cast<u32>(pBssSection->get_address() - uMemoryAddress4K), vBss.size());
				}
			}
			else
			{
				memcpy(&*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), &*vData.begin(), vData.size());
				if (!vBss.empty())
				{
					memcpy(&*sMemory.begin() + static_cast<u32>(pBssSection->get_address() - uMemoryAddress4K), &*vBss.begin(), vBss.size());
				}
			}
		}
		else
		{
			eErr = uc_emu_stop(pUc);
			eErr = uc_close(pUc);
			return 1;
		}
		eErr = uc_emu_stop(pUc);
		eErr = uc_close(pUc);
	}
	memcpy(&*vElf.begin() + static_cast<u32>(pDataSection->get_offset()), &*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), static_cast<u32>(pDataSection->get_size()));
	if (!sInvalidIndex.empty())
	{
		string sInitArrayData(pInitArraySection->get_data(), static_cast<u32>(pInitArraySection->get_size()));
		if (uClass == ELFCLASS32)
		{
			for (set<n32>::const_iterator it = sInvalidIndex.begin(); it != sInvalidIndex.end(); ++it)
			{
				n32 nInvalidIndex = *it;
				*reinterpret_cast<n32*>(&*sInitArrayData.begin() + nInvalidIndex * 4) = -1;
			}
		}
		else if (uClass == ELFCLASS64)
		{
			for (set<n32>::const_iterator it = sInvalidIndex.begin(); it != sInvalidIndex.end(); ++it)
			{
				n32 nInvalidIndex = *it;
				*reinterpret_cast<n64*>(&*sInitArrayData.begin() + nInvalidIndex * 8) = -1;
			}
		}
		pInitArraySection->set_data(sInitArrayData);
		if (pRelaDynSection == nullptr)
		{
			memcpy(&*vElf.begin() + static_cast<u32>(pInitArraySection->get_offset()), &*sInitArrayData.begin(), sInitArrayData.size());
		}
		else
		{
			relocation_section_accessor relaDynSection(elfFile, pRelaDynSection);
			if (uClass == ELFCLASS32)
			{
				for (set<n32>::const_iterator it = sInvalidIndex.begin(); it != sInvalidIndex.end(); ++it)
				{
					n32 nInvalidIndex = *it;
					map<n32, n32>::const_iterator itRelaDyn = mInitArrayRelaDynIndex.find(nInvalidIndex);
					if (itRelaDyn == mInitArrayRelaDynIndex.end())
					{
						memcpy(&*vElf.begin() + static_cast<u32>(pInitArraySection->get_offset() + nInvalidIndex * 4), &*sInitArrayData.begin() + nInvalidIndex + 4, 4);
					}
					else
					{
						n32 nRelaDynIndex = itRelaDyn->second;
						Elf64_Addr uOffset = 0;
						Elf_Word uSymbol = 0;
						Elf_Word uType = 0;
						Elf_Sxword nAddend = 0;
						if (!relaDynSection.get_entry(nRelaDynIndex, uOffset, uSymbol, uType, nAddend))
						{
							return 1;
						}
						relaDynSection.set_entry(nRelaDynIndex, uOffset, uSymbol, uType, -1);
					}
				}
			}
			else if (uClass == ELFCLASS64)
			{
				for (set<n32>::const_iterator it = sInvalidIndex.begin(); it != sInvalidIndex.end(); ++it)
				{
					n32 nInvalidIndex = *it;
					map<n32, n32>::const_iterator itRelaDyn = mInitArrayRelaDynIndex.find(nInvalidIndex);
					if (itRelaDyn == mInitArrayRelaDynIndex.end())
					{
						memcpy(&*vElf.begin() + static_cast<u32>(pInitArraySection->get_offset() + nInvalidIndex * 8), &*sInitArrayData.begin() + nInvalidIndex + 8, 8);
					}
					else
					{
						n32 nRelaDynIndex = itRelaDyn->second;
						Elf64_Addr uOffset = 0;
						Elf_Word uSymbol = 0;
						Elf_Word uType = 0;
						Elf_Sxword nAddend = 0;
						if (!relaDynSection.get_entry(nRelaDynIndex, uOffset, uSymbol, uType, nAddend))
						{
							return 1;
						}
						relaDynSection.set_entry(nRelaDynIndex, uOffset, uSymbol, uType, -1);
					}
				}
			}
			memcpy(&*vElf.begin() + static_cast<u32>(pRelaDynSection->get_offset()), pRelaDynSection->get_data(), static_cast<u32>(pRelaDynSection->get_size()));
		}
	}
	fp = UFopen(argv[2], USTR("wb"), false);
	if (fp == nullptr)
	{
		return 1;
	}
	fwrite(&*vElf.begin(), 1, vElf.size(), fp);
	fclose(fp);
	return 0;
}
