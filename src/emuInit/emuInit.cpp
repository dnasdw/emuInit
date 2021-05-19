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
		ofstream output;
		output.open(argv[2], ios::out | ios::binary | ios::trunc);
		if (!output)
		{
			return 1;
		}
		elfFile.save(output);
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
		ofstream output;
		output.open(argv[2], ios::out | ios::binary | ios::trunc);
		if (!output)
		{
			return 1;
		}
		elfFile.save(output);
		return 0;
	}
	u64 uMemoryAddress4K = uBasicAddressMin / 4096 * 4096;
	u32 uMemorySize4K = static_cast<u32>(Align(uBasicAddressMax - uMemoryAddress4K, 4096));
	if (uMemorySize4K == 0)
	{
		return 1;
	}
	string sMemory(uMemorySize4K, 0);
	memcpy(&*sMemory.begin() + (pTextSection->get_address() - uMemoryAddress4K), pTextSection->get_data(), static_cast<u32>(pTextSection->get_size()));
	if (pRodataSection != nullptr)
	{
		memcpy(&*sMemory.begin() + (pRodataSection->get_address() - uMemoryAddress4K), pRodataSection->get_data(), static_cast<u32>(pRodataSection->get_size()));
	}
	memcpy(&*sMemory.begin() + (pDataSection->get_address() - uMemoryAddress4K), pDataSection->get_data(), static_cast<u32>(pDataSection->get_size()));
	if (pRelaDynSection != nullptr)
	{
		relocation_section_accessor relaDynSection(elfFile, pRelaDynSection);
		u64 uInitArrayAddress = pInitArraySection->get_address();
		u64 uInitArraySize = pInitArraySection->get_size();
		string sInitArrayData(pInitArraySection->get_data(), static_cast<u32>(uInitArraySize));
		u64 uEnteyCount = relaDynSection.get_entries_num();
		for (u64 i = 0; i < uEnteyCount; i++)
		{
			Elf64_Addr uOffset = 0;
			Elf_Word uSymbol = 0;
			Elf_Word uType = 0;
			Elf_Sxword nAddend = 0;
			if (!relaDynSection.get_entry(i, uOffset, uSymbol, uType, nAddend))
			{
				return 1;
			}
			if (uSymbol == 0 && uType == 1027/* R_AARCH64_RELATIVE Adjust by program base. */ && uOffset >= uInitArrayAddress && uOffset + 8 <= uInitArrayAddress + uInitArraySize && !sInitArrayData.empty())
			{
				if (uClass == ELFCLASS32)
				{
					memcpy(&*sInitArrayData.begin() + (uOffset - uInitArrayAddress), &nAddend, sizeof(Elf32_Rela::r_addend));
				}
				else if (uClass == ELFCLASS64)
				{
					memcpy(&*sInitArrayData.begin() + (uOffset - uInitArrayAddress), &nAddend, sizeof(Elf64_Rela::r_addend));
				}
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
		if (uAddress >= uTextAddressMax)
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
			if (eErr != UC_ERR_OK)
			{
				printf("Failed on uc_open() with error returned: %u (%s)\n", eErr, uc_strerror(eErr));
				return 1;
			}
			eErr = uc_mem_map_ptr(pUc, uMemoryAddress4K, uMemorySize4K, UC_PROT_ALL, &*sMemory.begin());
			eErr = uc_mem_map(pUc, 0x60000000, 0x200000, UC_PROT_READ | UC_PROT_WRITE);
			u32 uSP = 0x60100000;
			eErr = uc_reg_write(pUc, UC_ARM_REG_SP, &uSP);
			u32 uLR = 0x68000000;
			eErr = uc_reg_write(pUc, UC_ARM_REG_LR, &uLR);
			u32 uPC = 0x00000000;
			eErr = uc_reg_write(pUc, UC_ARM_REG_PC, &uPC);
			eErr = uc_emu_start(pUc, uAddress, uTextAddressMax - uAddress, 10000000, 0);
			if (eErr == UC_ERR_OK)
			{
				memcpy(&*sMemory.begin() + (pDataSection->get_address() - uMemoryAddress4K), &*vData.begin(), vData.size());
				if (!vBss.empty())
				{
					memcpy(&*sMemory.begin() + (pBssSection->get_address() - uMemoryAddress4K), &*vBss.begin(), vBss.size());
				}
			}
			else if (eErr == UC_ERR_FETCH_UNMAPPED)
			{
				eErr = uc_reg_read(pUc, UC_ARM_REG_PC, &uPC);
				if (uPC != uLR)
				{
					eErr = uc_close(pUc);
					return 1;
				}
				if (memcmp(&*sMemory.begin() + (pDataSection->get_address() - uMemoryAddress4K), &*vData.begin(), vData.size()) != 0 && (vBss.empty() || memcmp(&*sMemory.begin() + (pBssSection->get_address() - uMemoryAddress4K), &*vBss.begin(), vBss.size()) == 0))
				{
					sInvalidIndex.insert(i);
					memcpy(&*vData.begin(), &*sMemory.begin() + (pDataSection->get_address() - uMemoryAddress4K), vData.size());
					if (!vBss.empty())
					{
						memcpy(&*vBss.begin(), &*sMemory.begin() + (pBssSection->get_address() - uMemoryAddress4K), vBss.size());
					}
				}
				else
				{
					memcpy(&*sMemory.begin() + (pDataSection->get_address() - uMemoryAddress4K), &*vData.begin(), vData.size());
					if (!vBss.empty())
					{
						memcpy(&*sMemory.begin() + (pBssSection->get_address() - uMemoryAddress4K), &*vBss.begin(), vBss.size());
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
		else if (uMachine == kMachineAARCH64)
		{
			eErr = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &pUc);
			if (eErr != UC_ERR_OK)
			{
				printf("Failed on uc_open() with error returned: %u (%s)\n", eErr, uc_strerror(eErr));
				return 1;
			}
			// TODO AARCH64
		}
	}
	pDataSection->set_data(&*sMemory.begin() + static_cast<u32>(pDataSection->get_address() - uMemoryAddress4K), static_cast<u32>(pDataSection->get_size()));
	if (!sInvalidIndex.empty())
	{
		string sInitArrayData(pInitArraySection->get_data(), static_cast<u32>(pInitArraySection->get_size()));
		if (uClass == ELFCLASS32)
		{
			for (set<n32>::const_iterator it = sInvalidIndex.begin(); it != sInvalidIndex.end(); ++it)
			{
				*reinterpret_cast<n32*>(&*sInitArrayData.begin() + *it * 4) = -1;
			}
		}
		else if (uClass == ELFCLASS64)
		{
			for (set<n32>::const_iterator it = sInvalidIndex.begin(); it != sInvalidIndex.end(); ++it)
			{
				*reinterpret_cast<n64*>(&*sInitArrayData.begin() + *it * 8) = -1;
			}
		}
		pInitArraySection->set_data(sInitArrayData);
		// TODO .rela.dyn section
	}
	ofstream output;
	output.open(argv[2], ios::out | ios::binary | ios::trunc);
	if (!output)
	{
		return 1;
	}
	elfFile.save(output);
	return 0;
}
