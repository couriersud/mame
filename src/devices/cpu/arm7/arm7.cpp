// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff,R. Belmont,Ryan Holtz
/*****************************************************************************
 *
 *   arm7.c
 *   Portable CPU Emulator for 32-bit ARM v3/4/5/6
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *   Thumb, DSP, and MMU support and many bugfixes by R. Belmont and Ryan Holtz.
 *
 *  This work is based on:
 *  #1) 'Atmel Corporation ARM7TDMI (Thumb) Datasheet - January 1999'
 *  #2) Arm 2/3/6 emulator By Bryan McPhail (bmcphail@tendril.co.uk) and Phil Stroffolino (MAME CORE 0.76)
 *
 *****************************************************************************/

/******************************************************************************
 *  Notes:

    ** This is a plain vanilla implementation of an ARM7 cpu which incorporates my ARM7 core.
       It can be used as is, or used to demonstrate how to utilize the arm7 core to create a cpu
       that uses the core, since there are numerous different mcu packages that incorporate an arm7 core.

       See the notes in the arm7core.inc file itself regarding issues/limitations of the arm7 core.
    **

TODO:
- Cleanups
- Fix and finish the DRC code, or remove it entirely

*****************************************************************************/
#include "emu.h"
#include "debugger.h"
#include "arm7.h"
#include "arm7core.h"   //include arm7 core
#include "arm7help.h"
#include "arm7fe.h"

/* size of the execution code cache */
#define CACHE_SIZE                      (32 * 1024 * 1024)

DEFINE_DEVICE_TYPE(ARM7,     arm7_cpu_device,     "arm7_le",  "ARM7 (little)")
DEFINE_DEVICE_TYPE(ARM7_BE,  arm7_be_cpu_device,  "arm7_be",  "ARM7 (big)")
DEFINE_DEVICE_TYPE(ARM7500,  arm7500_cpu_device,  "arm7500",  "ARM7500")
DEFINE_DEVICE_TYPE(ARM9,     arm9_cpu_device,     "arm9",     "ARM9")
DEFINE_DEVICE_TYPE(ARM920T,  arm920t_cpu_device,  "arm920t",  "ARM920T")
DEFINE_DEVICE_TYPE(ARM946ES, arm946es_cpu_device, "arm946es", "ARM946ES")
DEFINE_DEVICE_TYPE(PXA255,   pxa255_cpu_device,   "pxa255",   "Intel XScale PXA255")
DEFINE_DEVICE_TYPE(SA1110,   sa1110_cpu_device,   "sa1110",   "Intel StrongARM SA-1110")
DEFINE_DEVICE_TYPE(IGS036,   igs036_cpu_device,   "igs036",   "IGS036")

int arm7_cpu_device::s_fault_table[512];
uint32_t arm7_cpu_device::s_add_nvc_flags[8];
uint32_t arm7_cpu_device::s_sub_nvc_flags[8];

arm7_cpu_device::arm7_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm7_cpu_device(mconfig, ARM7, tag, owner, clock, 4, ARCHFLAG_T, ENDIANNESS_LITTLE)
{
}

arm7_cpu_device::arm7_cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, uint8_t archRev, uint8_t archFlags, endianness_t endianness)
	: cpu_device(mconfig, type, tag, owner, clock)
	, m_program_config("program", endianness, 32, 32, 0)
	, m_core(nullptr)
	, m_mode_changed(false)
	, m_program(nullptr)
	, m_direct(nullptr)
	, m_endian(endianness)
	, m_archRev(archRev)
	, m_archFlags(archFlags)
	, m_vectorbase(0)
	, m_prefetch_enabled(false)
	, m_enable_drc(false)
	, m_pc(0)
	, m_cache(CACHE_SIZE + sizeof(arm7_cpu_device))
	, m_drcuml(nullptr)
	, m_drcfe(nullptr)
	, m_drcoptions(0)
	, m_cache_dirty(0)
{
	uint32_t arch = ARM9_COPRO_ID_ARCH_V4;
	if (m_archFlags & ARCHFLAG_T)
		arch = ARM9_COPRO_ID_ARCH_V4T;

	m_copro_id = ARM9_COPRO_ID_MFR_ARM | arch | ARM9_COPRO_ID_PART_GENERICARM7;
}


arm7_be_cpu_device::arm7_be_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm7_cpu_device(mconfig, ARM7_BE, tag, owner, clock, 4, ARCHFLAG_T, ENDIANNESS_BIG)
{
}


arm7500_cpu_device::arm7500_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm7_cpu_device(mconfig, ARM7500, tag, owner, clock, 4, ARCHFLAG_MODE26, ENDIANNESS_LITTLE)
{
	m_copro_id = ARM9_COPRO_ID_MFR_ARM
			   | ARM9_COPRO_ID_ARCH_V4
			   | ARM9_COPRO_ID_PART_ARM710;
}


arm9_cpu_device::arm9_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm9_cpu_device(mconfig, ARM9, tag, owner, clock, 5, ARCHFLAG_T | ARCHFLAG_E, ENDIANNESS_LITTLE)
{
}


arm9_cpu_device::arm9_cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, uint8_t archRev, uint8_t archFlags, endianness_t endianness)
	: arm7_cpu_device(mconfig, type, tag, owner, clock, archRev, archFlags, endianness)
{
	uint32_t arch = ARM9_COPRO_ID_ARCH_V4;
	switch (archRev)
	{
		case 4:
			if (archFlags & ARCHFLAG_T)
				arch = ARM9_COPRO_ID_ARCH_V4T;
			break;
		case 5:
			arch = ARM9_COPRO_ID_ARCH_V5;
			if (archFlags & ARCHFLAG_T)
			{
				arch = ARM9_COPRO_ID_ARCH_V5T;
				if (archFlags & ARCHFLAG_E)
				{
					arch = ARM9_COPRO_ID_ARCH_V5TE;
				}
			}
			break;
		default: break;
	}

	m_copro_id = ARM9_COPRO_ID_MFR_ARM | arch | (0x900 << 4);
}


arm920t_cpu_device::arm920t_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm9_cpu_device(mconfig, ARM920T, tag, owner, clock, 4, ARCHFLAG_T, ENDIANNESS_LITTLE)
{
	m_copro_id = ARM9_COPRO_ID_MFR_ARM
			   | ARM9_COPRO_ID_SPEC_REV1
			   | ARM9_COPRO_ID_ARCH_V4T
			   | ARM9_COPRO_ID_PART_ARM920
			   | 0; // Stepping
}




arm946es_cpu_device::arm946es_cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: arm9_cpu_device(mconfig, type, tag, owner, clock, 5, ARCHFLAG_T | ARCHFLAG_E, ENDIANNESS_LITTLE),
	cp15_control(0x78)
{
	m_copro_id = ARM9_COPRO_ID_MFR_ARM
		| ARM9_COPRO_ID_ARCH_V5TE
		| ARM9_COPRO_ID_PART_ARM946
		| ARM9_COPRO_ID_STEP_ARM946_A0;

	memset(ITCM, 0, 0x8000);
	memset(DTCM, 0, 0x4000);

	cp15_itcm_base = 0xffffffff;
	cp15_itcm_size = 0;
	cp15_itcm_end = 0;
	cp15_dtcm_base = 0xffffffff;
	cp15_dtcm_size = 0;
	cp15_dtcm_end = 0;
	cp15_itcm_reg = cp15_dtcm_reg = 0;
}

arm946es_cpu_device::arm946es_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm946es_cpu_device(mconfig, ARM946ES, tag, owner, clock)
{
}

// unknown configuration, but uses MPU not MMU, so closer to ARM946ES
igs036_cpu_device::igs036_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm946es_cpu_device(mconfig, IGS036, tag, owner, clock)
{
}

pxa255_cpu_device::pxa255_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm7_cpu_device(mconfig, PXA255, tag, owner, clock, 5, ARCHFLAG_T | ARCHFLAG_E | ARCHFLAG_XSCALE, ENDIANNESS_LITTLE)
{
	m_copro_id = ARM9_COPRO_ID_MFR_INTEL
			   | ARM9_COPRO_ID_ARCH_V5TE
			   | ARM9_COPRO_ID_PXA255_CORE_GEN_XSCALE
			   | (3 << ARM9_COPRO_ID_PXA255_CORE_REV_SHIFT)
			   | ARM9_COPRO_ID_STEP_PXA255_A0;
}

sa1110_cpu_device::sa1110_cpu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: arm7_cpu_device(mconfig, SA1110, tag, owner, clock, 4, ARCHFLAG_SA, ENDIANNESS_LITTLE)
	// has StrongARM, no Thumb, no Enhanced DSP
{
	m_copro_id = ARM9_COPRO_ID_MFR_INTEL
			   | ARM9_COPRO_ID_ARCH_V4
			   | ARM9_COPRO_ID_PART_SA1110
			   | ARM9_COPRO_ID_STEP_SA1110_A0;
}

device_memory_interface::space_config_vector arm7_cpu_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config)
	};
}

void arm7_cpu_device::update_reg_ptr()
{
	m_core->m_reg_group = sRegisterTable[GET_MODE];
}

void arm7_cpu_device::set_cpsr(uint32_t val)
{
	m_core->m_r[eCPSR] = val | 0x10;
	const uint32_t mode = m_core->m_r[eCPSR] & MODE_FLAG;
	if (mode != m_core->m_mode)
	{
		m_core->m_mode = mode;
		update_reg_ptr();
	}
}

//inline void arm7_cpu_device::set_cpsr_nomode(uint32_t val)
//{
//	m_core->m_r[eCPSR] = val;
//}

void arm7500_cpu_device::set_cpsr(uint32_t val)
{
	if ((val & 0x10) != (m_core->m_r[eCPSR] & 0x10))
	{
		if (val & 0x10)
		{
			// 26 -> 32
			val = (val & 0x0FFFFF3F) | (m_core->m_r[eR15] & 0xF0000000) /* N Z C V */ | ((m_core->m_r[eR15] & 0x0C000000) >> (26 - 6)) /* I F */;
			m_core->m_r[eR15] = m_core->m_r[eR15] & 0x03FFFFFC;
		}
		else
		{
			// 32 -> 26
			m_core->m_r[eR15] = (m_core->m_r[eR15] & 0x03FFFFFC) /* PC */ | (val & 0xF0000000) /* N Z C V */ | ((val & 0x000000C0) << (26 - 6)) /* I F */ | (val & 0x00000003) /* M1 M0 */;
		}
	}
	else
	{
		if (!(val & 0x10))
		{
			// mirror bits in pc
			m_core->m_r[eR15] = (m_core->m_r[eR15] & 0x03FFFFFF) | (val & 0xF0000000) /* N Z C V */ | ((val & 0x000000C0) << (26 - 6)) /* I F */;
		}
	}
	m_core->m_r[eCPSR] = val;
	const uint32_t mode = m_core->m_r[eCPSR] & MODE_FLAG;
	if (mode != m_core->m_mode)
	{
		m_core->m_mode = mode;
		update_reg_ptr();
	}
}


/**************************************************************************
 * ARM TLB IMPLEMENTATION
 **************************************************************************/

// COARSE, desc_level1, vaddr
uint32_t arm7_cpu_device::arm7_tlb_get_second_level_descriptor(uint32_t granularity, uint32_t first_desc, uint32_t vaddr)
{
	uint32_t desc_lvl2 = vaddr;

	switch( granularity )
	{
		case TLB_COARSE:
			desc_lvl2 = (first_desc & COPRO_TLB_CFLD_ADDR_MASK) | ((vaddr & COPRO_TLB_VADDR_CSLTI_MASK) >> COPRO_TLB_VADDR_CSLTI_MASK_SHIFT);
			break;
		case TLB_FINE:
			desc_lvl2 = (first_desc & COPRO_TLB_FPTB_ADDR_MASK) | ((vaddr & COPRO_TLB_VADDR_FSLTI_MASK) >> COPRO_TLB_VADDR_FSLTI_MASK_SHIFT);
			break;
		default:
			// We shouldn't be here
			LOG( ( "ARM7: Attempting to get second-level TLB descriptor of invalid granularity (%d)\n", granularity ) );
			break;
	}

	return m_program->read_dword(desc_lvl2);
}

int arm7_cpu_device::decode_fault(int mode, int ap, int access_control, int system, int rom, int write)
{
	switch (access_control & 3)
	{
		case 0 : // "No access - Any access generates a domain fault"
			return FAULT_DOMAIN;

		case 1 : // "Client - Accesses are checked against the access permission bits in the section or page descriptor"
			switch (ap & 3)
			{
				case 0:
					if (system)
					{
						if (rom) // "Reserved" -> assume same behaviour as S=0/R=0 case
						{
							return FAULT_PERMISSION;
						}
						else // "Only Supervisor read permitted"
						{
							if (mode == eARM7_MODE_USER || write != 0)
							{
								return FAULT_PERMISSION;
							}
						}
					}
					else
					{
						if (rom) // "Any write generates a permission fault"
						{
							if (write != 0)
							{
								return FAULT_PERMISSION;
							}
						}
						else // "Any access generates a permission fault"
						{
							return FAULT_PERMISSION;
						}
					}
					return FAULT_NONE;
				case 1:
					if (mode == eARM7_MODE_USER)
					{
						return FAULT_PERMISSION;
					}
					break;
				case 2:
					if (mode == eARM7_MODE_USER && write != 0)
					{
						return FAULT_PERMISSION;
					}
					break;
				case 3:
					return FAULT_NONE;
			}
			return FAULT_NONE;

		case 2 : // "Reserved - Reserved. Currently behaves like the no access mode"
			return FAULT_DOMAIN;

		case 3 : // "Manager - Accesses are not checked against the access permission bits so a permission fault cannot be generated"
			return FAULT_NONE;
	}
	return FAULT_NONE;
}

int arm7_cpu_device::detect_read_fault(int desc_lvl1, int ap)
{
	const uint32_t index = m_core->m_mode | ap | m_core->m_decoded_access_control[(desc_lvl1 >> 5) & 0xf];
	return s_fault_table[index];
}

// Bits:
// 8: ARM7_TLB_WRITE
// 7..6: decoded access control
// 5..4: ap
// 3..0: Mode
int arm7_cpu_device::detect_fault(int desc_lvl1, int ap, int flags)
{
#if 1
	const uint32_t index = m_core->m_mode | ap | m_core->m_decoded_access_control[(desc_lvl1 >> 5) & 0xf] | (flags & ARM7_TLB_WRITE);
	return s_fault_table[index];
#else
	switch (m_core->m_decoded_access_control[(desc_lvl1 >> 5) & 0xf] >> 6)
	{
		case 0 : // "No access - Any access generates a domain fault"
			return FAULT_DOMAIN;

		case 1 : // "Client - Accesses are checked against the access permission bits in the section or page descriptor"
			switch (ap & 3)
			{
				case 0:
					if (m_core->m_control & COPRO_CTRL_SYSTEM)
					{
						if (m_core->m_control & COPRO_CTRL_ROM) // "Reserved" -> assume same behaviour as S=0/R=0 case
						{
							return FAULT_PERMISSION;
						}
						else // "Only Supervisor read permitted"
						{
							if (m_core->m_mode == eARM7_MODE_USER || (flags & ARM7_TLB_WRITE))
							{
								return FAULT_PERMISSION;
							}
						}
					}
					else
					{
						if (m_core->m_control & COPRO_CTRL_ROM) // "Any write generates a permission fault"
						{
							if (flags & ARM7_TLB_WRITE)
							{
								return FAULT_PERMISSION;
							}
						}
						else // "Any access generates a permission fault"
						{
							return FAULT_PERMISSION;
						}
					}
					return FAULT_NONE;
				case 1:
					if (m_core->m_mode == eARM7_MODE_USER)
					{
						return FAULT_PERMISSION;
					}
					break;
				case 2:
					if ((m_core->m_mode == eARM7_MODE_USER) && (flags & ARM7_TLB_WRITE))
					{
						return FAULT_PERMISSION;
					}
					break;
				case 3:
					return FAULT_NONE;
			}
			return FAULT_NONE;

		case 2 : // "Reserved - Reserved. Currently behaves like the no access mode"
			return FAULT_DOMAIN;

		case 3 : // "Manager - Accesses are not checked against the access permission bits so a permission fault cannot be generated"
			return FAULT_NONE;
	}
	return FAULT_NONE;
#endif
}

bool arm7_cpu_device::arm7_tlb_translate_check(offs_t &addr)
{
	if (addr < 0x2000000)
	{
		addr += m_core->m_pid_offset;
	}

	const uint32_t desc_lvl1 = m_tlb_base[addr >> COPRO_TLB_VADDR_FLTI_MASK_SHIFT];
	const uint32_t lvl1_type = desc_lvl1 & 3;

	if (lvl1_type == COPRO_TLB_SECTION_TABLE)
	{
		// Entry is a section
		//if (s_fault_table[m_core->m_decoded_access_control[(desc_lvl1 >> 5) & 0xf] | ((desc_lvl1 >> 6) & 0x30) | m_core->m_mode] == FAULT_NONE)
		if (detect_read_fault(desc_lvl1, (desc_lvl1 >> 6) & 0x30) == FAULT_NONE)
		{
			addr = (desc_lvl1 & COPRO_TLB_SECTION_PAGE_MASK) | (addr & ~COPRO_TLB_SECTION_PAGE_MASK);
			return true;
		}
		return false;
	}
	else if (lvl1_type == COPRO_TLB_UNMAPPED)
	{
		return false;
	}
	else
	{
		// Entry is the physical address of a coarse second-level table
		const uint8_t permission = (m_core->m_domainAccessControl >> ((desc_lvl1 >> 4) & 0x1e)) & 3;
		const uint32_t desc_lvl2 = arm7_tlb_get_second_level_descriptor( (desc_lvl1 & 3) == COPRO_TLB_COARSE_TABLE ? TLB_COARSE : TLB_FINE, desc_lvl1, addr );
		if ((permission != 1) && (permission != 3))
		{
			uint8_t domain = (desc_lvl1 >> 5) & 0xF;
			fatalerror("ARM7: Not Yet Implemented: Coarse Table, Section Domain fault on virtual address, vaddr = %08x, domain = %08x, PC = %08x\n", addr, domain, m_core->m_r[eR15]);
		}

		switch (desc_lvl2 & 3)
		{
			case COPRO_TLB_UNMAPPED:
				return false;
			case COPRO_TLB_LARGE_PAGE:
				// Large page descriptor
				addr = (desc_lvl2 & COPRO_TLB_LARGE_PAGE_MASK) | (addr & ~COPRO_TLB_LARGE_PAGE_MASK);
				return true;
			case COPRO_TLB_SMALL_PAGE:
			{
				// Small page descriptor
				uint8_t ap = ((((desc_lvl2 >> 4) & 0xFF) >> (((addr >> 10) & 3) << 1)) & 3) << 4;
				if (detect_read_fault(desc_lvl1, ap) == FAULT_NONE)
				{
					addr = (desc_lvl2 & COPRO_TLB_SMALL_PAGE_MASK) | (addr & ~COPRO_TLB_SMALL_PAGE_MASK);
					return true;
				}
				return false;
			}
			case COPRO_TLB_TINY_PAGE:
				// Tiny page descriptor
				if ((desc_lvl1 & 3) == 1)
				{
					LOG( ( "ARM7: It would appear that we're looking up a tiny page from a coarse TLB lookup.  This is bad. vaddr = %08x\n", addr ) );
				}
				addr = (desc_lvl2 & COPRO_TLB_TINY_PAGE_MASK) | (addr & ~COPRO_TLB_TINY_PAGE_MASK);
				return true;
		}
	}
#if 0
	switch (desc_lvl1 & 3)
	{
		case COPRO_TLB_UNMAPPED:
			return false;

		case COPRO_TLB_SECTION_TABLE:
		{
			// Entry is a section
			int fault = detect_read_fault(desc_lvl1, (desc_lvl1 >> 6) & 0x30);
			addr = (desc_lvl1 & COPRO_TLB_SECTION_PAGE_MASK) | (addr & ~COPRO_TLB_SECTION_PAGE_MASK);
			return (fault == FAULT_NONE)
		}

		case COPRO_TLB_COARSE_TABLE:
		case COPRO_TLB_FINE_TABLE:
		{
			// Entry is the physical address of a coarse second-level table
			const uint8_t permission = (m_core->m_domainAccessControl >> ((desc_lvl1 >> 4) & 0x1e)) & 3;
			const uint32_t desc_lvl2 = arm7_tlb_get_second_level_descriptor( (desc_lvl1 & 3) == COPRO_TLB_COARSE_TABLE ? TLB_COARSE : TLB_FINE, desc_lvl1, addr );
			if ((permission != 1) && (permission != 3))
			{
				uint8_t domain = (desc_lvl1 >> 5) & 0xF;
				fatalerror("ARM7: Not Yet Implemented: Coarse Table, Section Domain fault on virtual address, vaddr = %08x, domain = %08x, PC = %08x\n", addr, domain, m_core->m_r[eR15]);
			}

			switch (desc_lvl2 & 3)
			{
				case COPRO_TLB_UNMAPPED:
					return false;
				case COPRO_TLB_LARGE_PAGE:
					// Large page descriptor
					addr = (desc_lvl2 & COPRO_TLB_LARGE_PAGE_MASK) | (addr & ~COPRO_TLB_LARGE_PAGE_MASK);
					return true;
				case COPRO_TLB_SMALL_PAGE:
				{
					// Small page descriptor
					uint8_t ap = ((((desc_lvl2 >> 4) & 0xFF) >> (((addr >> 10) & 3) << 1)) & 3) << 4;
					int fault = detect_read_fault(desc_lvl1, ap);
					addr = (desc_lvl2 & COPRO_TLB_SMALL_PAGE_MASK) | (addr & ~COPRO_TLB_SMALL_PAGE_MASK);
					return fault == FAULT_NONE;
				}
				case COPRO_TLB_TINY_PAGE:
					// Tiny page descriptor
					if ((desc_lvl1 & 3) == 1)
					{
						LOG( ( "ARM7: It would appear that we're looking up a tiny page from a coarse TLB lookup.  This is bad. vaddr = %08x\n", addr ) );
					}
					addr = (desc_lvl2 & COPRO_TLB_TINY_PAGE_MASK) | (addr & ~COPRO_TLB_TINY_PAGE_MASK);
					return true;
			}
		}
	}
#endif
	return true;
}

bool arm7_cpu_device::arm7_tlb_translate(offs_t &addr, int flags)
{
	if (addr < 0x2000000)
	{
		addr += m_core->m_pid_offset;
	}

	const uint32_t desc_lvl1 = m_tlb_base[addr >> COPRO_TLB_VADDR_FLTI_MASK_SHIFT];
	const uint32_t lvl1_type = desc_lvl1 & 3;

	if (lvl1_type == COPRO_TLB_SECTION_TABLE)
	{
		// Entry is a section
		int fault = detect_fault(desc_lvl1, (desc_lvl1 >> 6) & 0x30, flags);
		if (fault == FAULT_NONE)
		{
			addr = ( desc_lvl1 & COPRO_TLB_SECTION_PAGE_MASK ) | ( addr & ~COPRO_TLB_SECTION_PAGE_MASK );
		}
		else
		{
			uint8_t domain = (desc_lvl1 >> 5) & 0xF;
			printf("ARM7: Section Table, Section %s fault on virtual address, vaddr = %08x, PC = %08x\n", (fault == FAULT_DOMAIN) ? "domain" : "permission", addr, m_core->m_r[eR15]);
			m_core->m_faultStatus[0] = ((fault == FAULT_DOMAIN) ? (9 << 0) : (13 << 0)) | (domain << 4); // 9 = section domain fault, 13 = section permission fault
			m_core->m_faultAddress = addr;
			m_core->m_pendingAbtD = true;
			m_core->m_pending_interrupt = true;
			printf("vaddr %08X desc_lvl1 %08X domain %d permission %d ap %d s %d r %d mode %d read %d write %d\n",
				addr, desc_lvl1, domain, (m_core->m_domainAccessControl >> ((desc_lvl1 >> 4) & 0x1e)) & 3, (desc_lvl1 >> 10) & 3, (m_core->m_control & COPRO_CTRL_SYSTEM) ? 1 : 0, (m_core->m_control & COPRO_CTRL_ROM) ? 1 : 0,
				m_core->m_mode, flags & ARM7_TLB_READ ? 1 : 0,  flags & ARM7_TLB_WRITE ? 1 : 0);
			return false;
		}
		return true;
	}
	else if (lvl1_type == COPRO_TLB_UNMAPPED)
	{
		// Unmapped, generate a translation fault
		LOG( ( "ARM7: Translation fault on unmapped virtual address, PC = %08x, vaddr = %08x\n", m_core->m_r[eR15], addr ) );
		m_core->m_faultStatus[0] = (5 << 0); // 5 = section translation fault
		m_core->m_faultAddress = addr;
		m_core->m_pendingAbtD = true;
		m_core->m_pending_interrupt = true;
		return false;
	}
	else
	{
		// Entry is the physical address of a coarse second-level table
		const uint8_t permission = (m_core->m_domainAccessControl >> ((desc_lvl1 >> 4) & 0x1e)) & 3;
		const uint32_t desc_lvl2 = arm7_tlb_get_second_level_descriptor( (desc_lvl1 & 3) == COPRO_TLB_COARSE_TABLE ? TLB_COARSE : TLB_FINE, desc_lvl1, addr );
		if ((permission != 1) && (permission != 3))
		{
			uint8_t domain = (desc_lvl1 >> 5) & 0xF;
			fatalerror("ARM7: Not Yet Implemented: Coarse Table, Section Domain fault on virtual address, vaddr = %08x, domain = %08x, PC = %08x\n", addr, domain, m_core->m_r[eR15]);
		}

		switch (desc_lvl2 & 3)
		{
			case COPRO_TLB_UNMAPPED:
			{
				// Unmapped, generate a translation fault
				uint8_t domain = (desc_lvl1 >> 5) & 0xF;
				LOG( ( "ARM7: Translation fault on unmapped virtual address, vaddr = %08x, PC %08X\n", addr, m_core->m_r[eR15] ) );
				m_core->m_faultStatus[0] = (7 << 0) | (domain << 4); // 7 = page translation fault
				m_core->m_faultAddress = addr;
				m_core->m_pendingAbtD = true;
				m_core->m_pending_interrupt = true;
				return false;
			}
			case COPRO_TLB_LARGE_PAGE:
				// Large page descriptor
				addr = (desc_lvl2 & COPRO_TLB_LARGE_PAGE_MASK) | (addr & ~COPRO_TLB_LARGE_PAGE_MASK);
				break;
			case COPRO_TLB_SMALL_PAGE:
				// Small page descriptor
				{
					uint8_t ap = ((((desc_lvl2 >> 4) & 0xFF) >> (((addr >> 10) & 3) << 1)) & 3) << 4;
					int fault = detect_fault(desc_lvl1, ap, flags);
					if (fault == FAULT_NONE)
					{
						addr = (desc_lvl2 & COPRO_TLB_SMALL_PAGE_MASK) | (addr & ~COPRO_TLB_SMALL_PAGE_MASK);
					}
					else
					{
						uint8_t domain = (desc_lvl1 >> 5) & 0xF;
						// hapyfish expects a data abort when something tries to write to a read-only memory location from user mode
						LOG( ( "ARM7: Page Table, Section %s fault on virtual address, vaddr = %08x, PC = %08x\n", (fault == FAULT_DOMAIN) ? "domain" : "permission", addr, m_core->m_r[eR15] ) );
						m_core->m_faultStatus[0] = ((fault == FAULT_DOMAIN) ? (11 << 0) : (15 << 0)) | (domain << 4); // 11 = page domain fault, 15 = page permission fault
						m_core->m_faultAddress = addr;
						m_core->m_pendingAbtD = true;
						m_core->m_pending_interrupt = true;
						LOG( ( "vaddr %08X desc_lvl2 %08X domain %d permission %d ap %d s %d r %d mode %d read %d write %d\n",
							addr, desc_lvl2, domain, permission, ap, (m_core->m_control & COPRO_CTRL_SYSTEM) ? 1 : 0, (m_core->m_control & COPRO_CTRL_ROM) ? 1 : 0,
							m_core->m_mode, flags & ARM7_TLB_READ ? 1 : 0,  flags & ARM7_TLB_WRITE ? 1 : 0) );
						return false;
					}
				}
				break;
			case COPRO_TLB_TINY_PAGE:
				// Tiny page descriptor
				if ((desc_lvl1 & 3) == 1)
				{
					LOG( ( "ARM7: It would appear that we're looking up a tiny page from a coarse TLB lookup.  This is bad. vaddr = %08x\n", addr ) );
				}
				addr = (desc_lvl2 & COPRO_TLB_TINY_PAGE_MASK) | (addr & ~COPRO_TLB_TINY_PAGE_MASK);
				break;
		}
		return true;
	}

#if 0
	switch (desc_lvl1 & 3)
	{
		case COPRO_TLB_UNMAPPED:
			// Unmapped, generate a translation fault
			LOG( ( "ARM7: Translation fault on unmapped virtual address, PC = %08x, vaddr = %08x\n", m_core->m_r[eR15], addr ) );
			m_core->m_faultStatus[0] = (5 << 0); // 5 = section translation fault
			m_core->m_faultAddress = addr;
			m_core->m_pendingAbtD = true;
			m_core->m_pending_interrupt = true;
			return false;

		case COPRO_TLB_SECTION_TABLE:
		{
			// Entry is a section
			int fault = detect_fault(desc_lvl1, (desc_lvl1 >> 6) & 0x30, flags);
			if (fault == FAULT_NONE)
			{
				addr = ( desc_lvl1 & COPRO_TLB_SECTION_PAGE_MASK ) | ( addr & ~COPRO_TLB_SECTION_PAGE_MASK );
			}
			else
			{
				uint8_t domain = (desc_lvl1 >> 5) & 0xF;
				LOG( ( "ARM7: Section Table, Section %s fault on virtual address, vaddr = %08x, PC = %08x\n", (fault == FAULT_DOMAIN) ? "domain" : "permission", addr, m_core->m_r[eR15] ) );
				m_core->m_faultStatus[0] = ((fault == FAULT_DOMAIN) ? (9 << 0) : (13 << 0)) | (domain << 4); // 9 = section domain fault, 13 = section permission fault
				m_core->m_faultAddress = addr;
				m_core->m_pendingAbtD = true;
				m_core->m_pending_interrupt = true;
				LOG( ( "vaddr %08X desc_lvl1 %08X domain %d permission %d ap %d s %d r %d mode %d read %d write %d\n",
					addr, desc_lvl1, domain, (m_core->m_domainAccessControl >> ((desc_lvl1 >> 4) & 0x1e)) & 3, (desc_lvl1 >> 10) & 3, (m_core->m_control & COPRO_CTRL_SYSTEM) ? 1 : 0, (m_core->m_control & COPRO_CTRL_ROM) ? 1 : 0,
					m_core->m_mode, flags & ARM7_TLB_READ ? 1 : 0,  flags & ARM7_TLB_WRITE ? 1 : 0) );
				return false;
			}
			return true;
		}

		case COPRO_TLB_COARSE_TABLE:
		case COPRO_TLB_FINE_TABLE:
		{
			// Entry is the physical address of a coarse second-level table
			const uint8_t permission = (m_core->m_domainAccessControl >> ((desc_lvl1 >> 4) & 0x1e)) & 3;
			const uint32_t desc_lvl2 = arm7_tlb_get_second_level_descriptor( (desc_lvl1 & 3) == COPRO_TLB_COARSE_TABLE ? TLB_COARSE : TLB_FINE, desc_lvl1, addr );
			if ((permission != 1) && (permission != 3))
			{
				uint8_t domain = (desc_lvl1 >> 5) & 0xF;
				fatalerror("ARM7: Not Yet Implemented: Coarse Table, Section Domain fault on virtual address, vaddr = %08x, domain = %08x, PC = %08x\n", addr, domain, m_core->m_r[eR15]);
			}

			switch (desc_lvl2 & 3)
			{
				case COPRO_TLB_UNMAPPED:
					// Unmapped, generate a translation fault
					uint8_t domain = (desc_lvl1 >> 5) & 0xF;
					LOG( ( "ARM7: Translation fault on unmapped virtual address, vaddr = %08x, PC %08X\n", addr, m_core->m_r[eR15] ) );
					m_core->m_faultStatus[0] = (7 << 0) | (domain << 4); // 7 = page translation fault
					m_core->m_faultAddress = addr;
					m_core->m_pendingAbtD = true;
					m_core->m_pending_interrupt = true;
					return false;
				case COPRO_TLB_LARGE_PAGE:
					// Large page descriptor
					addr = (desc_lvl2 & COPRO_TLB_LARGE_PAGE_MASK) | (addr & ~COPRO_TLB_LARGE_PAGE_MASK);
					break;
				case COPRO_TLB_SMALL_PAGE:
					// Small page descriptor
					{
						uint8_t ap = ((((desc_lvl2 >> 4) & 0xFF) >> (((addr >> 10) & 3) << 1)) & 3) << 4;
						int fault = detect_fault(desc_lvl1, ap, flags);
						if (fault == FAULT_NONE)
						{
							addr = (desc_lvl2 & COPRO_TLB_SMALL_PAGE_MASK) | (addr & ~COPRO_TLB_SMALL_PAGE_MASK);
						}
						else
						{
							uint8_t domain = (desc_lvl1 >> 5) & 0xF;
							// hapyfish expects a data abort when something tries to write to a read-only memory location from user mode
							LOG( ( "ARM7: Page Table, Section %s fault on virtual address, vaddr = %08x, PC = %08x\n", (fault == FAULT_DOMAIN) ? "domain" : "permission", addr, m_core->m_r[eR15] ) );
							m_core->m_faultStatus[0] = ((fault == FAULT_DOMAIN) ? (11 << 0) : (15 << 0)) | (domain << 4); // 11 = page domain fault, 15 = page permission fault
							m_core->m_faultAddress = addr;
							m_core->m_pendingAbtD = true;
							m_core->m_pending_interrupt = true;
							LOG( ( "vaddr %08X desc_lvl2 %08X domain %d permission %d ap %d s %d r %d mode %d read %d write %d\n",
								addr, desc_lvl2, domain, permission, ap, (m_core->m_control & COPRO_CTRL_SYSTEM) ? 1 : 0, (m_core->m_control & COPRO_CTRL_ROM) ? 1 : 0,
								m_core->m_mode, flags & ARM7_TLB_READ ? 1 : 0,  flags & ARM7_TLB_WRITE ? 1 : 0) );
							return false;
						}
					}
					break;
				case COPRO_TLB_TINY_PAGE:
					// Tiny page descriptor
					if ((desc_lvl1 & 3) == 1)
					{
						LOG( ( "ARM7: It would appear that we're looking up a tiny page from a coarse TLB lookup.  This is bad. vaddr = %08x\n", addr ) );
					}
					addr = (desc_lvl2 & COPRO_TLB_TINY_PAGE_MASK) | (addr & ~COPRO_TLB_TINY_PAGE_MASK);
					break;
			}
			return true;
		}
	}
#endif
	return true;
}


bool arm7_cpu_device::memory_translate(int spacenum, int intention, offs_t &address)
{
	/* only applies to the program address space and only does something if the MMU's enabled */
	if( spacenum == AS_PROGRAM && ( m_core->m_control & COPRO_CTRL_MMU_EN ) )
	{
		return arm7_tlb_translate(address, 0);
	}
	return true;
}


/* include the arm7 core */
#include "arm7core.hxx"

/***************************************************************************
 * CPU SPECIFIC IMPLEMENTATIONS
 **************************************************************************/

void arm7_cpu_device::postload()
{
	update_reg_ptr();
}

void arm7_cpu_device::device_start()
{
	m_core = (internal_arm_state *)m_cache.alloc_near(sizeof(internal_arm_state));
	memset(m_core, 0, sizeof(internal_arm_state));

	m_enable_drc = false;//allow_drc();

	m_core->m_prefetch_word0_shift = (m_endian == ENDIANNESS_LITTLE ? 0 : 16);
	m_core->m_prefetch_word1_shift = (m_endian == ENDIANNESS_LITTLE ? 16 : 0);

	// TODO[RH]: Default to 3-instruction prefetch for unknown ARM variants. Derived cores should set the appropriate value in their constructors.
	m_core->m_insn_prefetch_depth = 3;

	memset(m_core->m_insn_prefetch_buffer, 0, sizeof(uint32_t) * 3);
	memset(m_core->m_insn_prefetch_address, 0, sizeof(uint32_t) * 3);
	memset(m_core->m_insn_prefetch_translated, 0, sizeof(uint32_t) * 3);
	m_core->m_insn_prefetch_count = 0;
	m_core->m_insn_prefetch_index = 0;

	m_program = &space(AS_PROGRAM);
	m_direct = m_program->direct<0>();
	m_tlb_base = (uint32_t*)m_direct->read_ptr(0);

	save_item(NAME(m_core->m_insn_prefetch_depth));
	save_item(NAME(m_core->m_insn_prefetch_count));
	save_item(NAME(m_core->m_insn_prefetch_index));
	save_item(NAME(m_core->m_insn_prefetch_buffer));
	save_item(NAME(m_core->m_insn_prefetch_address));
	save_item(NAME(m_core->m_r));
	save_item(NAME(m_core->m_pendingIrq));
	save_item(NAME(m_core->m_pendingFiq));
	save_item(NAME(m_core->m_pendingAbtD));
	save_item(NAME(m_core->m_pendingAbtP));
	save_item(NAME(m_core->m_pendingUnd));
	save_item(NAME(m_core->m_pendingSwi));
	save_item(NAME(m_core->m_pending_interrupt));
	save_item(NAME(m_core->m_control));
	save_item(NAME(m_core->m_tlbBase));
	save_item(NAME(m_core->m_tlb_base_mask));
	save_item(NAME(m_core->m_faultStatus));
	save_item(NAME(m_core->m_faultAddress));
	save_item(NAME(m_core->m_fcsePID));
	save_item(NAME(m_core->m_pid_offset));
	save_item(NAME(m_core->m_domainAccessControl));
	save_item(NAME(m_core->m_decoded_access_control));
	machine().save().register_postload(save_prepost_delegate(FUNC(arm7_cpu_device::postload), this));

	m_icountptr = &m_core->m_icount;

	uint32_t umlflags = 0;
	m_drcuml = std::make_unique<drcuml_state>(*this, m_cache, umlflags, 1, 32, 1);

	// add UML symbols
	m_drcuml->symbol_add(&m_core->m_r[eR15], sizeof(uint32_t), "pc");
	char buf[4];
	for (int i = 0; i < 16; i++)
	{
		sprintf(buf, "r%d", i);
		m_drcuml->symbol_add(&m_core->m_r[i], sizeof(uint32_t), buf);
	}
	m_drcuml->symbol_add(&m_core->m_r[eCPSR], sizeof(uint32_t), "sr");
	m_drcuml->symbol_add(&m_core->m_r[eR8_FIQ], sizeof(uint32_t), "r8_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eR9_FIQ], sizeof(uint32_t), "r9_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eR10_FIQ], sizeof(uint32_t), "r10_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eR11_FIQ], sizeof(uint32_t), "r11_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eR12_FIQ], sizeof(uint32_t), "r12_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eR13_FIQ], sizeof(uint32_t), "r13_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eR14_FIQ], sizeof(uint32_t), "r14_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eSPSR_FIQ], sizeof(uint32_t), "spsr_fiq");
	m_drcuml->symbol_add(&m_core->m_r[eR13_IRQ], sizeof(uint32_t), "r13_irq");
	m_drcuml->symbol_add(&m_core->m_r[eR14_IRQ], sizeof(uint32_t), "r14_irq");
	m_drcuml->symbol_add(&m_core->m_r[eSPSR_IRQ], sizeof(uint32_t), "spsr_irq");
	m_drcuml->symbol_add(&m_core->m_r[eR13_SVC], sizeof(uint32_t), "r13_svc");
	m_drcuml->symbol_add(&m_core->m_r[eR14_SVC], sizeof(uint32_t), "r14_svc");
	m_drcuml->symbol_add(&m_core->m_r[eSPSR_SVC], sizeof(uint32_t), "spsr_svc");
	m_drcuml->symbol_add(&m_core->m_r[eR13_ABT], sizeof(uint32_t), "r13_abt");
	m_drcuml->symbol_add(&m_core->m_r[eR14_ABT], sizeof(uint32_t), "r14_abt");
	m_drcuml->symbol_add(&m_core->m_r[eSPSR_ABT], sizeof(uint32_t), "spsr_abt");
	m_drcuml->symbol_add(&m_core->m_r[eR13_UND], sizeof(uint32_t), "r13_und");
	m_drcuml->symbol_add(&m_core->m_r[eR14_UND], sizeof(uint32_t), "r14_und");
	m_drcuml->symbol_add(&m_core->m_r[eSPSR_UND], sizeof(uint32_t), "spsr_und");
	m_drcuml->symbol_add(&m_core->m_icount, sizeof(int), "icount");

	/* initialize the front-end helper */
	m_drcfe = std::make_unique<arm7_frontend>(this, COMPILE_BACKWARDS_BYTES, COMPILE_FORWARDS_BYTES, SINGLE_INSTRUCTION_MODE ? 1 : COMPILE_MAX_SEQUENCE);

	/* mark the cache dirty so it is updated on next execute */
	m_cache_dirty = true;

	state_add( ARM7_PC,    "PC", m_pc).callexport().formatstr("%08X");
	state_add(STATE_GENPC, "GENPC", m_pc).callexport().noshow();
	state_add(STATE_GENPCBASE, "CURPC", m_pc).callexport().noshow();
	/* registers shared by all operating modes */
	state_add( ARM7_R0,    "R0",   m_core->m_r[ 0]).formatstr("%08X");
	state_add( ARM7_R1,    "R1",   m_core->m_r[ 1]).formatstr("%08X");
	state_add( ARM7_R2,    "R2",   m_core->m_r[ 2]).formatstr("%08X");
	state_add( ARM7_R3,    "R3",   m_core->m_r[ 3]).formatstr("%08X");
	state_add( ARM7_R4,    "R4",   m_core->m_r[ 4]).formatstr("%08X");
	state_add( ARM7_R5,    "R5",   m_core->m_r[ 5]).formatstr("%08X");
	state_add( ARM7_R6,    "R6",   m_core->m_r[ 6]).formatstr("%08X");
	state_add( ARM7_R7,    "R7",   m_core->m_r[ 7]).formatstr("%08X");
	state_add( ARM7_R8,    "R8",   m_core->m_r[ 8]).formatstr("%08X");
	state_add( ARM7_R9,    "R9",   m_core->m_r[ 9]).formatstr("%08X");
	state_add( ARM7_R10,   "R10",  m_core->m_r[10]).formatstr("%08X");
	state_add( ARM7_R11,   "R11",  m_core->m_r[11]).formatstr("%08X");
	state_add( ARM7_R12,   "R12",  m_core->m_r[12]).formatstr("%08X");
	state_add( ARM7_R13,   "R13",  m_core->m_r[13]).formatstr("%08X");
	state_add( ARM7_R14,   "R14",  m_core->m_r[14]).formatstr("%08X");
	state_add( ARM7_R15,   "R15",  m_core->m_r[15]).formatstr("%08X");
	/* FIRQ Mode Shadowed Registers */
	state_add( ARM7_FR8,   "FR8",  m_core->m_r[eR8_FIQ]  ).formatstr("%08X");
	state_add( ARM7_FR9,   "FR9",  m_core->m_r[eR9_FIQ]  ).formatstr("%08X");
	state_add( ARM7_FR10,  "FR10", m_core->m_r[eR10_FIQ] ).formatstr("%08X");
	state_add( ARM7_FR11,  "FR11", m_core->m_r[eR11_FIQ] ).formatstr("%08X");
	state_add( ARM7_FR12,  "FR12", m_core->m_r[eR12_FIQ] ).formatstr("%08X");
	state_add( ARM7_FR13,  "FR13", m_core->m_r[eR13_FIQ] ).formatstr("%08X");
	state_add( ARM7_FR14,  "FR14", m_core->m_r[eR14_FIQ] ).formatstr("%08X");
	state_add( ARM7_FSPSR, "FR16", m_core->m_r[eSPSR_FIQ]).formatstr("%08X");
	/* IRQ Mode Shadowed Registers */
	state_add( ARM7_IR13,  "IR13", m_core->m_r[eR13_IRQ] ).formatstr("%08X");
	state_add( ARM7_IR14,  "IR14", m_core->m_r[eR14_IRQ] ).formatstr("%08X");
	state_add( ARM7_ISPSR, "IR16", m_core->m_r[eSPSR_IRQ]).formatstr("%08X");
	/* Supervisor Mode Shadowed Registers */
	state_add( ARM7_SR13,  "SR13", m_core->m_r[eR13_SVC] ).formatstr("%08X");
	state_add( ARM7_SR14,  "SR14", m_core->m_r[eR14_SVC] ).formatstr("%08X");
	state_add( ARM7_SSPSR, "SR16", m_core->m_r[eSPSR_SVC]).formatstr("%08X");
	/* Abort Mode Shadowed Registers */
	state_add( ARM7_AR13,  "AR13", m_core->m_r[eR13_ABT] ).formatstr("%08X");
	state_add( ARM7_AR14,  "AR14", m_core->m_r[eR14_ABT] ).formatstr("%08X");
	state_add( ARM7_ASPSR, "AR16", m_core->m_r[eSPSR_ABT]).formatstr("%08X");
	/* Undefined Mode Shadowed Registers */
	state_add( ARM7_UR13,  "UR13", m_core->m_r[eR13_UND] ).formatstr("%08X");
	state_add( ARM7_UR14,  "UR14", m_core->m_r[eR14_UND] ).formatstr("%08X");
	state_add( ARM7_USPSR, "UR16", m_core->m_r[eSPSR_UND]).formatstr("%08X");

	state_add(STATE_GENFLAGS, "GENFLAGS", m_core->m_r[eCPSR]).formatstr("%13s").noshow();

	update_fault_table();
	calculate_nvc_flags();
}

void arm7_cpu_device::device_stop()
{
	if (m_drcfe != nullptr)
	{
		m_drcfe = nullptr;
	}
	if (m_drcuml != nullptr)
	{
		m_drcuml = nullptr;
	}
}

void arm7_cpu_device::calculate_nvc_flags()
{
	for (uint32_t rn = 0; rn < 2; rn++)
	{
		for (uint32_t op2 = 0; op2 < 2; op2++)
		{
			for (uint32_t rd = 0; rd < 2; rd++)
			{
				s_add_nvc_flags[(rn << 2) | (op2 << 1) | rd]
					= (rd ? N_MASK : 0)
					| (((~(rn ^ op2) & (rn ^ rd)) & 1) ? V_MASK : 0)
					| ((((rn & op2) | (rn & ~rd) | (op2 & ~rd)) & 1) ? C_MASK : 0);
				s_sub_nvc_flags[(rn << 2) | (op2 << 1) | rd]
					= (rd ? N_MASK : 0)
					| ((((rn ^ op2) & (rn ^ rd)) & 1) ? V_MASK : 0)
					| ((((rn & ~op2) | (rn & ~rd) | (~op2 & ~rd)) & 1) ? C_MASK : 0);
			}
		}
	}
}

void arm7_cpu_device::update_fault_table()
{
	for (uint8_t mode = 0; mode < 16; mode++)
	{
		for (uint8_t ap = 0; ap < 4; ap++)
		{
			for (uint8_t access_control = 0; access_control < 4; access_control++)
			{
				uint8_t system = (m_core->m_control & COPRO_CTRL_SYSTEM) ? 1 : 0;
				uint8_t rom = (m_core->m_control & COPRO_CTRL_ROM) ? 1 : 0;
				for (uint8_t write = 0; write < 2; write++)
				{
					const uint32_t index = (write << 8) | (access_control << 6) | (ap << 4) | mode;
					s_fault_table[index] = decode_fault(mode, ap, access_control, system, rom, write);
				}
			}
		}
	}
}

void arm946es_cpu_device::device_start()
{
	arm9_cpu_device::device_start();

	save_item(NAME(cp15_control));
	save_item(NAME(cp15_itcm_base));
	save_item(NAME(cp15_dtcm_base));
	save_item(NAME(cp15_itcm_size));
	save_item(NAME(cp15_dtcm_size));
	save_item(NAME(cp15_itcm_end));
	save_item(NAME(cp15_dtcm_end));
	save_item(NAME(cp15_itcm_reg));
	save_item(NAME(cp15_dtcm_reg));
	save_item(NAME(ITCM));
	save_item(NAME(DTCM));
}


void arm7_cpu_device::state_export(const device_state_entry &entry)
{
	switch (entry.index())
	{
		case STATE_GENPC:
		case STATE_GENPCBASE:
			m_pc = GET_PC;
			break;
	}
}


void arm7_cpu_device::state_string_export(const device_state_entry &entry, std::string &str) const
{
	switch (entry.index())
	{
		case STATE_GENFLAGS:
			str = string_format("%c%c%c%c%c%c%c%c %s",
				(ARM7REG(eCPSR) & N_MASK) ? 'N' : '-',
				(ARM7REG(eCPSR) & Z_MASK) ? 'Z' : '-',
				(ARM7REG(eCPSR) & C_MASK) ? 'C' : '-',
				(ARM7REG(eCPSR) & V_MASK) ? 'V' : '-',
				(ARM7REG(eCPSR) & Q_MASK) ? 'Q' : '-',
				(ARM7REG(eCPSR) & I_MASK) ? 'I' : '-',
				(ARM7REG(eCPSR) & F_MASK) ? 'F' : '-',
				(ARM7REG(eCPSR) & T_MASK) ? 'T' : '-',
				GetModeText(ARM7REG(eCPSR)));
		break;
	}
}

void arm7_cpu_device::device_reset()
{
	memset(m_core->m_r, 0, sizeof(m_core->m_r));
	m_core->m_pendingIrq = false;
	m_core->m_pendingFiq = false;
	m_core->m_pendingAbtD = false;
	m_core->m_pendingAbtP = false;
	m_core->m_pendingUnd = false;
	m_core->m_pendingSwi = false;
	m_core->m_pending_interrupt = false;
	m_core->m_control = 0;
	m_core->m_tlbBase = 0;
	m_core->m_tlb_base_mask = 0;
	m_core->m_faultStatus[0] = 0;
	m_core->m_faultStatus[1] = 0;
	m_core->m_faultAddress = 0;
	m_core->m_fcsePID = 0;
	m_core->m_pid_offset = 0;
	m_core->m_domainAccessControl = 0;
	memset(m_core->m_decoded_access_control, 0, sizeof(uint8_t) * 16);

	/* start up in SVC mode with interrupts disabled. */
	m_core->m_r[eCPSR] = I_MASK | F_MASK | 0x10;
	SwitchMode(eARM7_MODE_SVC);
	m_core->m_r[eR15] = 0 | m_vectorbase;

	m_cache_dirty = true;
}


#define UNEXECUTED() \
	{							\
		m_core->m_r[eR15] += 4;	\
		m_core->m_icount += 2;	/* Any unexecuted instruction only takes 1 cycle (page 193) */	\
	}


void arm7_cpu_device::update_insn_prefetch_mmu(uint32_t curr_pc)
{
	if (m_core->m_insn_prefetch_address[m_core->m_insn_prefetch_index] != curr_pc)
	{
		m_core->m_insn_prefetch_count = 0;
		m_core->m_insn_prefetch_index = 0;
	}

	if (m_core->m_insn_prefetch_count == m_core->m_insn_prefetch_depth)
		return;

	const uint32_t to_fetch = m_core->m_insn_prefetch_depth - m_core->m_insn_prefetch_count;
	if (to_fetch == 0)
		return;

	uint32_t index = m_core->m_insn_prefetch_depth + (m_core->m_insn_prefetch_index - to_fetch);
	if (index >= m_core->m_insn_prefetch_depth) index -= m_core->m_insn_prefetch_depth;

	uint32_t pc = curr_pc + m_core->m_insn_prefetch_count * 4;
	uint32_t i = 0;
	for (; i < to_fetch; i++)
	{
		uint32_t translated_pc = pc;
		if (!arm7_tlb_translate_check(translated_pc))
		{
			m_core->m_insn_prefetch_translated[index] = ~0;
			break;
		}
		m_core->m_insn_prefetch_buffer[index] = m_direct->read_dword(translated_pc);
		m_core->m_insn_prefetch_address[index] = pc;
		m_core->m_insn_prefetch_translated[index] = translated_pc;
		pc += 4;

		index++;
		if (index >= m_core->m_insn_prefetch_depth) index -= m_core->m_insn_prefetch_depth;
	}
	m_core->m_insn_prefetch_count += i;
}

void arm7_cpu_device::update_insn_prefetch(uint32_t curr_pc)
{
	if (m_core->m_insn_prefetch_address[m_core->m_insn_prefetch_index] != curr_pc)
	{
		m_core->m_insn_prefetch_count = 0;
		m_core->m_insn_prefetch_index = 0;
	}

	if (m_core->m_insn_prefetch_count == m_core->m_insn_prefetch_depth)
		return;

	const uint32_t to_fetch = m_core->m_insn_prefetch_depth - m_core->m_insn_prefetch_count;
	const uint32_t start_index = (m_core->m_insn_prefetch_depth + (m_core->m_insn_prefetch_index - to_fetch)) % m_core->m_insn_prefetch_depth;

	uint32_t pc = curr_pc + m_core->m_insn_prefetch_count * 4;
	uint32_t i = 0;
	for (; i < to_fetch; i++)
	{
		uint32_t index = (i + start_index) % m_core->m_insn_prefetch_depth;
		m_core->m_insn_prefetch_buffer[index] = m_direct->read_dword(pc);
		m_core->m_insn_prefetch_address[index] = pc;
		m_core->m_insn_prefetch_translated[index] = pc;
		pc += 4;
	}
	m_core->m_insn_prefetch_count += i;
}

void arm7_cpu_device::insn_fetch_thumb(uint32_t pc, bool& translated)
{
	translated = !(m_core->m_insn_prefetch_translated[m_core->m_insn_prefetch_index] & 1);
	if (pc & 2)
	{
		m_insn = (uint16_t)(m_core->m_insn_prefetch_buffer[m_core->m_insn_prefetch_index] >> m_core->m_prefetch_word1_shift);
		m_core->m_insn_prefetch_index = (m_core->m_insn_prefetch_index + 1) % m_core->m_insn_prefetch_count;
		m_core->m_insn_prefetch_count--;
		return;
	}
	m_insn = (uint16_t)(m_core->m_insn_prefetch_buffer[m_core->m_insn_prefetch_index] >> m_core->m_prefetch_word0_shift);
}

void arm7_cpu_device::insn_fetch_arm(uint32_t pc, bool& translated)
{
	//printf("ipb[%d] = %08x\n", m_core->m_insn_prefetch_index, m_core->m_insn_prefetch_buffer[m_core->m_insn_prefetch_index]);
	translated = !(m_core->m_insn_prefetch_translated[m_core->m_insn_prefetch_index] & 1);
	m_insn = m_core->m_insn_prefetch_buffer[m_core->m_insn_prefetch_index];
	m_core->m_insn_prefetch_index++;
	if (m_core->m_insn_prefetch_index >= m_core->m_insn_prefetch_count)
		m_core->m_insn_prefetch_index -= m_core->m_insn_prefetch_count;
	m_core->m_insn_prefetch_count--;
}

int arm7_cpu_device::get_insn_prefetch_index(uint32_t address)
{
	address &= ~3;
	for (uint32_t i = 0; i < m_core->m_insn_prefetch_depth; i++)
	{
		if (m_core->m_insn_prefetch_address[i] == address)
		{
			return (int)i;
		}
	}
	return -1;
}

#include "arm7ops.hxx"

template <arm7_cpu_device::insn_mode THUMB, arm7_cpu_device::copro_mode MMU_ENABLED, arm7_cpu_device::prefetch_mode PREFETCH>
void arm7_cpu_device::execute_core()
{
	do
	{
		arm7_check_irq_state();

		uint32_t pc = R15;

		debugger_instruction_hook(this, pc);

		if (THUMB)
		{
			// "In Thumb state, bit [0] is undefined and must be ignored. Bits [31:1] contain the PC."
			offs_t raddr = pc & (~1);

			if (MMU_ENABLED)
			{
				if (PREFETCH)
				{
					update_insn_prefetch_mmu(raddr & ~3);

					bool translated = false;
					insn_fetch_thumb(raddr, translated);
					if (translated)
					{
						(this->*thumb_handler[(m_insn & 0xffc0) >> 6])(pc);
					}
					else
					{
						m_core->m_pendingAbtP = true;
						m_core->m_pending_interrupt = true;
					}
				}
				else
				{
					uint32_t dword_pc = raddr & ~3;
					if (arm7_tlb_translate_check(dword_pc))
					{
						m_insn = m_direct->read_word(dword_pc | (raddr & 2));
						(this->*thumb_handler[(m_insn & 0xffc0) >> 6])(pc);
					}
					else
					{
						m_core->m_pendingAbtP = true;
						m_core->m_pending_interrupt = true;
					}
				}
			}
			else
			{
				if (PREFETCH)
				{
					update_insn_prefetch(raddr & ~3);

					bool translated = false;
					insn_fetch_thumb(raddr, translated);
					(this->*thumb_handler[(m_insn & 0xffc0) >> 6])(pc);
				}
				else
				{
					m_insn = m_direct->read_word(raddr);
					(this->*thumb_handler[(m_insn & 0xffc0) >> 6])(pc);
				}
			}
		}
		else
		{
			/* load 32 bit instruction */

			// "In ARM state, bits [1:0] of r15 are undefined and must be ignored. Bits [31:2] contain the PC."
			offs_t raddr = pc & (~3);

			if (PREFETCH)
			{
				if (MMU_ENABLED)
				{
					update_insn_prefetch_mmu(raddr);
				}
				else
				{
					update_insn_prefetch(raddr);
				}

				bool translated = false;
				insn_fetch_arm(raddr, translated);
				if (!translated)
				{
					m_core->m_pendingAbtP = true;
					m_core->m_pending_interrupt = true;
					goto skip_arm_exec;
				}
			}
			else
			{
				if (MMU_ENABLED)
				{
					if (arm7_tlb_translate_check(raddr))
					{
						m_insn = m_direct->read_dword(raddr);
					}
					else
					{
						m_core->m_pendingAbtP = true;
						m_core->m_pending_interrupt = true;
						goto skip_arm_exec;
					}
				}
				else
				{
					m_insn = m_direct->read_dword(raddr);
				}
			}

			const uint32_t cond = m_insn >> INSN_COND_SHIFT;
			if (cond != COND_AL)
			{
				/* process condition codes for this instruction */
				switch (cond)
				{
					case COND_EQ:
						if (GET_CPSR & Z_MASK)
							{}
						else
							{ UNEXECUTED(); goto skip_arm_exec; }
						break;
					case COND_NE:
						if (GET_CPSR & Z_MASK)
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
							{}
						break;
					case COND_CS:
						if (GET_CPSR & C_MASK)
							{}
						else
							{ UNEXECUTED(); goto skip_arm_exec; }
						break;
					case COND_CC:
						if (GET_CPSR & C_MASK)
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
							{}
						break;
					case COND_MI:
						if (GET_CPSR & N_MASK)
							{}
						else
							{ UNEXECUTED(); goto skip_arm_exec; }
						break;
					case COND_PL:
						if (GET_CPSR & N_MASK)
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
							{}
						break;
					case COND_VS:
						if (GET_CPSR & V_MASK)
							{}
						else
							{ UNEXECUTED(); goto skip_arm_exec; }
						break;
					case COND_VC:
						if (GET_CPSR & V_MASK)
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
							{}
						break;
					case COND_HI:
						if (!(GET_CPSR & C_MASK) || (GET_CPSR & Z_MASK))
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
							{}
						break;
					case COND_LS:
						if (!(GET_CPSR & C_MASK) || (GET_CPSR & Z_MASK))
							{}
						else
							{ UNEXECUTED(); goto skip_arm_exec; }
						break;
					case COND_GE:
						if (((GET_CPSR & N_MASK) >> 3) ^ (GET_CPSR & V_MASK))
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
							{}
						break;
					case COND_LT:
						if (((GET_CPSR & N_MASK) >> 3) ^ (GET_CPSR & V_MASK))
							{}
						else
							{ UNEXECUTED(); goto skip_arm_exec; }
						break;
					case COND_GT:
						if ((GET_CPSR & Z_MASK) || ((GET_CPSR & N_MASK) >> 3) ^ (GET_CPSR & V_MASK))
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
							{}
						break;
					case COND_LE:
						if ((GET_CPSR & Z_MASK) || ((GET_CPSR & N_MASK) >> 3) ^ (GET_CPSR & V_MASK))
							{}
						else
							{ UNEXECUTED(); goto skip_arm_exec; }
						break;
					case COND_NV:
						if (m_archRev < 5)
							{ UNEXECUTED(); goto skip_arm_exec; }
						else
						{
							execute_arm9_insn();
							goto skip_arm_exec;
						}
						break;
					case COND_AL:
						break;
				}
			}

			const uint32_t op_offset = (m_insn & 0x0FF00000) >> 20;
			switch (op_offset)
			{
				case 0x00:
				case 0x04:
					arm7ops_0<OFFSET_DOWN, NO_FLAGS, NO_WRITEBACK>();
					break;
				case 0x02:
				case 0x06:
					arm7ops_0<OFFSET_DOWN, NO_FLAGS, WRITEBACK>();
					break;
				case 0x01:
				case 0x05:
					arm7ops_0<OFFSET_DOWN, SET_FLAGS, NO_WRITEBACK>();
					break;
				case 0x03:
				case 0x07:
					arm7ops_0<OFFSET_DOWN, SET_FLAGS, WRITEBACK>();
					break;
				case 0x08:
				case 0x0c:
					arm7ops_0<OFFSET_UP, NO_FLAGS, NO_WRITEBACK>();
					break;
				case 0x0a:
				case 0x0e:
					arm7ops_0<OFFSET_UP, NO_FLAGS, WRITEBACK>();
					break;
				case 0x09:
				case 0x0d:
					arm7ops_0<OFFSET_UP, SET_FLAGS, NO_WRITEBACK>();
					break;
				case 0x0b:
				case 0x0f:
					arm7ops_0<OFFSET_UP, SET_FLAGS, WRITEBACK>();
					break;
				case 0x10:
				case 0x14:
					arm7ops_1<OFFSET_DOWN, NO_FLAGS, NO_WRITEBACK>();
					break;
				case 0x12:
				case 0x16:
					arm7ops_1<OFFSET_DOWN, NO_FLAGS, WRITEBACK>();
					break;
				case 0x11:
				case 0x15:
					arm7ops_1<OFFSET_DOWN, SET_FLAGS, NO_WRITEBACK>();
					break;
				case 0x13:
				case 0x17:
					arm7ops_1<OFFSET_DOWN, SET_FLAGS, WRITEBACK>();
					break;
				case 0x18:
				case 0x1c:
					arm7ops_1<OFFSET_UP, NO_FLAGS, NO_WRITEBACK>();
					break;
				case 0x1a:
				case 0x1e:
					arm7ops_1<OFFSET_UP, NO_FLAGS, WRITEBACK>();
					break;
				case 0x19:
				case 0x1d:
					arm7ops_1<OFFSET_UP, SET_FLAGS, NO_WRITEBACK>();
					break;
				case 0x1b:
				case 0x1f:
					arm7ops_1<OFFSET_UP, SET_FLAGS, WRITEBACK>();
					break;
				case 0x20:
				case 0x22:
				case 0x24:
				case 0x26:
				case 0x28:
				case 0x2a:
				case 0x2c:
				case 0x2e:
					HandleALU<IMM_OP2, NO_FLAGS>();
					break;
				case 0x21:
				case 0x23:
				case 0x25:
				case 0x27:
				case 0x29:
				case 0x2b:
				case 0x2d:
				case 0x2f:
					HandleALU<IMM_OP2, SET_FLAGS>();
					break;
				case 0x30:
				case 0x32:
				case 0x34:
				case 0x36:
					arm7ops_3<OFFSET_DOWN, NO_FLAGS>();
					break;
				case 0x31:
				case 0x33:
				case 0x35:
				case 0x37:
					arm7ops_3<OFFSET_DOWN, SET_FLAGS>();
					break;
				case 0x38:
				case 0x3a:
				case 0x3c:
				case 0x3e:
					arm7ops_3<OFFSET_UP, NO_FLAGS>();
					break;
				case 0x39:
				case 0x3b:
				case 0x3d:
				case 0x3f:
					arm7ops_3<OFFSET_UP, SET_FLAGS>();
					break;
				case 0x40:
				case 0x41:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x42:
				case 0x43:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x44:
				case 0x45:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x46:
				case 0x47:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x48:
				case 0x49:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_UP, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x4a:
				case 0x4b:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_UP, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x4c:
				case 0x4d:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_UP, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x4e:
				case 0x4f:
					HandleMemSingle<REG_OP2, POST_INDEXED, OFFSET_UP, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x50:
				case 0x51:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x52:
				case 0x53:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x54:
				case 0x55:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x56:
				case 0x57:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x58:
				case 0x59:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_UP, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x5a:
				case 0x5b:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_UP, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x5c:
				case 0x5d:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_UP, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x5e:
				case 0x5f:
					HandleMemSingle<REG_OP2, PRE_INDEXED, OFFSET_UP, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x60:
				case 0x61:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x62:
				case 0x63:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x64:
				case 0x65:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x66:
				case 0x67:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_DOWN, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x68:
				case 0x69:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_UP, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x6a:
				case 0x6b:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_UP, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x6c:
				case 0x6d:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_UP, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x6e:
				case 0x6f:
					HandleMemSingle<IMM_OP2, POST_INDEXED, OFFSET_UP, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x70:
				case 0x71:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x72:
				case 0x73:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x74:
				case 0x75:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x76:
				case 0x77:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_DOWN, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x78:
				case 0x79:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_UP, SIZE_DWORD, NO_WRITEBACK>();
					break;
				case 0x7a:
				case 0x7b:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_UP, SIZE_DWORD, WRITEBACK>();
					break;
				case 0x7c:
				case 0x7d:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_UP, SIZE_BYTE, NO_WRITEBACK>();
					break;
				case 0x7e:
				case 0x7f:
					HandleMemSingle<IMM_OP2, PRE_INDEXED, OFFSET_UP, SIZE_BYTE, WRITEBACK>();
					break;
				case 0x80:
				case 0x81:
					HandleMemBlock<POST_INDEXED, OFFSET_DOWN, NO_S_BIT, NO_WRITEBACK>();
					break;
				case 0x82:
				case 0x83:
					HandleMemBlock<POST_INDEXED, OFFSET_DOWN, NO_S_BIT, WRITEBACK>();
					break;
				case 0x84:
				case 0x85:
					HandleMemBlock<POST_INDEXED, OFFSET_DOWN, S_BIT, NO_WRITEBACK>();
					break;
				case 0x86:
				case 0x87:
					HandleMemBlock<POST_INDEXED, OFFSET_DOWN, S_BIT, WRITEBACK>();
					break;
				case 0x88:
				case 0x89:
					HandleMemBlock<POST_INDEXED, OFFSET_UP, NO_S_BIT, NO_WRITEBACK>();
					break;
				case 0x8a:
				case 0x8b:
					HandleMemBlock<POST_INDEXED, OFFSET_UP, NO_S_BIT, WRITEBACK>();
					break;
				case 0x8c:
				case 0x8d:
					HandleMemBlock<POST_INDEXED, OFFSET_UP, S_BIT, NO_WRITEBACK>();
					break;
				case 0x8e:
				case 0x8f:
					HandleMemBlock<POST_INDEXED, OFFSET_UP, S_BIT, WRITEBACK>();
					break;
				case 0x90:
				case 0x91:
					HandleMemBlock<PRE_INDEXED, OFFSET_DOWN, NO_S_BIT, NO_WRITEBACK>();
					break;
				case 0x92:
				case 0x93:
					HandleMemBlock<PRE_INDEXED, OFFSET_DOWN, NO_S_BIT, WRITEBACK>();
					break;
				case 0x94:
				case 0x95:
					HandleMemBlock<PRE_INDEXED, OFFSET_DOWN, S_BIT, NO_WRITEBACK>();
					break;
				case 0x96:
				case 0x97:
					HandleMemBlock<PRE_INDEXED, OFFSET_DOWN, S_BIT, WRITEBACK>();
					break;
				case 0x98:
				case 0x99:
					HandleMemBlock<PRE_INDEXED, OFFSET_UP, NO_S_BIT, NO_WRITEBACK>();
					break;
				case 0x9a:
				case 0x9b:
					HandleMemBlock<PRE_INDEXED, OFFSET_UP, NO_S_BIT, WRITEBACK>();
					break;
				case 0x9c:
				case 0x9d:
					HandleMemBlock<PRE_INDEXED, OFFSET_UP, S_BIT, NO_WRITEBACK>();
					break;
				case 0x9e:
				case 0x9f:
					HandleMemBlock<PRE_INDEXED, OFFSET_UP, S_BIT, WRITEBACK>();
					break;
				case 0xa0:
				case 0xa1:
				case 0xa2:
				case 0xa3:
				case 0xa4:
				case 0xa5:
				case 0xa6:
				case 0xa7:
				case 0xa8:
				case 0xa9:
				case 0xaa:
				case 0xab:
				case 0xac:
				case 0xad:
				case 0xae:
				case 0xaf:
				case 0xb0:
				case 0xb1:
				case 0xb2:
				case 0xb3:
				case 0xb4:
				case 0xb5:
				case 0xb6:
				case 0xb7:
				case 0xb8:
				case 0xb9:
				case 0xba:
				case 0xbb:
				case 0xbc:
				case 0xbd:
				case 0xbe:
				case 0xbf:
					HandleBranch();
					break;
				case 0xc0:
				case 0xc1:
				case 0xc2:
				case 0xc3:
				case 0xc4:
				case 0xc5:
				case 0xc6:
				case 0xc7:
				case 0xc8:
				case 0xc9:
				case 0xca:
				case 0xcb:
				case 0xcc:
				case 0xcd:
				case 0xce:
				case 0xcf:
				case 0xd0:
				case 0xd1:
				case 0xd2:
				case 0xd3:
				case 0xd4:
				case 0xd5:
				case 0xd6:
				case 0xd7:
				case 0xd8:
				case 0xd9:
				case 0xda:
				case 0xdb:
				case 0xdc:
				case 0xdd:
				case 0xde:
				case 0xdf:
					arm7ops_cd();
					break;
				case 0xe0:
				case 0xe1:
				case 0xe2:
				case 0xe3:
				case 0xe4:
				case 0xe5:
				case 0xe6:
				case 0xe7:
				case 0xe8:
				case 0xe9:
				case 0xea:
				case 0xeb:
				case 0xec:
				case 0xed:
				case 0xee:
				case 0xef:
					arm7ops_e();
					break;
				case 0xf0:
				case 0xf1:
				case 0xf2:
				case 0xf3:
				case 0xf4:
				case 0xf5:
				case 0xf6:
				case 0xf7:
				case 0xf8:
				case 0xf9:
				case 0xfa:
				case 0xfb:
				case 0xfc:
				case 0xfd:
				case 0xfe:
				case 0xff:
					arm7ops_f();
					break;
			}
		}
skip_arm_exec:
		;
	} while (m_core->m_icount >= 0 && !m_mode_changed);

	m_mode_changed = false;
	if (m_core->m_icount >= 0)
	{
		execute_run();
	}
}

void arm7_cpu_device::execute_arm9_insn()
{
	const uint32_t op_offset = (m_insn & 0xF800000) >> 23;
	switch (op_offset)
	{
		case 0x00:
		case 0x01:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0c:
		case 0x0d:
		case 0x1a:
		case 0x1b:
		case 0x1e:
		case 0x1f:
			arm9ops_undef();
			break;
		case 0x02:
		case 0x03:
			arm9ops_1();
			break;
		case 0x0a:
		case 0x0b:
		case 0x0e:
		case 0x0f:
			arm9ops_57();
			break;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			arm9ops_89();
			break;
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
			HandleBranchHBit();
			break;
		case 0x18:
		case 0x19:
			arm9ops_c();
			break;
		case 0x1c:
		case 0x1d:
			arm9ops_e();
			break;
	}
}

void arm7_cpu_device::execute_run()
{
	if (m_prefetch_enabled)
	{
		if (T_IS_SET(m_core->m_r[eCPSR]))
		{
			if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
			{
				execute_core<THUMB_MODE, MMU_ON, PREFETCH_ON>();
			}
			else
			{
				execute_core<THUMB_MODE, MMU_OFF, PREFETCH_ON>();
			}
		}
		else
		{
			if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
			{
				execute_core<ARM_MODE, MMU_ON, PREFETCH_ON>();
			}
			else
			{
				execute_core<ARM_MODE, MMU_OFF, PREFETCH_ON>();
			}
		}
	}
	else
	{
		if (T_IS_SET(m_core->m_r[eCPSR]))
		{
			if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
			{
				execute_core<THUMB_MODE, MMU_ON, PREFETCH_OFF>();
			}
			else
			{
				execute_core<THUMB_MODE, MMU_OFF, PREFETCH_OFF>();
			}
		}
		else
		{
			if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
			{
				execute_core<ARM_MODE, MMU_ON, PREFETCH_OFF>();
			}
			else
			{
				execute_core<ARM_MODE, MMU_OFF, PREFETCH_OFF>();
			}
		}
	}
}


void arm7_cpu_device::execute_set_input(int irqline, int state)
{
	switch (irqline) {
	case ARM7_IRQ_LINE: /* IRQ */
		m_core->m_pendingIrq = state ? true : false;
		break;

	case ARM7_FIRQ_LINE: /* FIRQ */
		m_core->m_pendingFiq = state ? true : false;
		break;

	case ARM7_ABORT_EXCEPTION:
		m_core->m_pendingAbtD = state ? true : false;
		break;

	case ARM7_ABORT_PREFETCH_EXCEPTION:
		m_core->m_pendingAbtP = state ? true : false;
		break;

	case ARM7_UNDEFINE_EXCEPTION:
		m_core->m_pendingUnd = state ? true : false;
		break;
	}

	update_irq_state();
	//arm7_check_irq_state();
}


util::disasm_interface *arm7_cpu_device::create_disassembler()
{
	return new arm7_disassembler(this);
}

bool arm7_cpu_device::get_t_flag() const
{
	return T_IS_SET(m_core->m_r[eCPSR]);
}


/* ARM system coprocessor support */

WRITE32_MEMBER( arm7_cpu_device::arm7_do_callback )
{
	m_core->m_pendingUnd = true;
	m_core->m_pending_interrupt = true;
}

READ32_MEMBER( arm7_cpu_device::arm7_rt_r_callback )
{
	uint32_t opcode = offset;
	uint8_t cReg = ( opcode & INSN_COPRO_CREG ) >> INSN_COPRO_CREG_SHIFT;
	uint8_t op2 =  ( opcode & INSN_COPRO_OP2 )  >> INSN_COPRO_OP2_SHIFT;
	uint8_t op3 =    opcode & INSN_COPRO_OP3;
	uint8_t cpnum = (opcode & INSN_COPRO_CPNUM) >> INSN_COPRO_CPNUM_SHIFT;
	uint32_t data = 0;

//    printf("cpnum %d cReg %d op2 %d op3 %d (%x)\n", cpnum, cReg, op2, op3, GET_REGISTER(arm, 15));

	// we only handle system copro here
	if (cpnum != 15)
	{
		if (m_archFlags & ARCHFLAG_XSCALE)
		{
			// handle XScale specific CP14
			if (cpnum == 14)
			{
				switch( cReg )
				{
					case 1: // clock counter
						data = (uint32_t)total_cycles();
						break;

					default:
						break;
				}
			}
			else
			{
				fatalerror("XScale: Unhandled coprocessor %d (archFlags %x)\n", cpnum, m_archFlags);
			}

			return data;
		}
		else
		{
			LOG( ("ARM7: Unhandled coprocessor %d (archFlags %x)\n", cpnum, m_archFlags) );
			m_core->m_pendingUnd = true;
			m_core->m_pending_interrupt = true;
			return 0;
		}
	}

	switch( cReg )
	{
		case 4:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
			// RESERVED
			LOG( ( "arm7_rt_r_callback CR%d, RESERVED\n", cReg ) );
			break;
		case 0:             // ID
			switch(op2)
			{
			case 0:
				data = m_copro_id;
				break;
			case 1: // cache type
				data = 0x0f0d2112;  // HACK: value expected by ARMWrestler (probably Nintendo DS ARM9's value)
				//data = (6 << 25) | (1 << 24) | (0x172 << 12) | (0x172 << 0); // ARM920T (S3C24xx)
				break;
			case 2: // TCM type
				data = 0;
				break;
			case 3: // TLB type
				data = 0;
				break;
			case 4: // MPU type
				data = 0;
				break;
			}
			LOG( ( "arm7_rt_r_callback, ID %02x (%02x) -> %08x (PC=%08x)\n",op2,m_archRev,data,GET_PC ) );
			break;
		case 1:             // Control
			data = COPRO_CTRL | 0x70;   // bits 4-6 always read back as "1" (bit 3 too in XScale)
			break;
		case 2:             // Translation Table Base
			data = COPRO_TLB_BASE;
			break;
		case 3:             // Domain Access Control
			LOG( ( "arm7_rt_r_callback, Domain Access Control\n" ) );
			data = COPRO_DOMAIN_ACCESS_CONTROL;
			break;
		case 5:             // Fault Status
			LOG( ( "arm7_rt_r_callback, Fault Status\n" ) );
			switch (op3)
			{
				case 0: data = COPRO_FAULT_STATUS_D; break;
				case 1: data = COPRO_FAULT_STATUS_P; break;
			}
			break;
		case 6:             // Fault Address
			LOG( ( "arm7_rt_r_callback, Fault Address\n" ) );
			data = COPRO_FAULT_ADDRESS;
			break;
		case 13:            // Read Process ID (PID)
			LOG( ( "arm7_rt_r_callback, Read PID\n" ) );
			data = COPRO_FCSE_PID;
			break;
		case 14:            // Read Breakpoint
			LOG( ( "arm7_rt_r_callback, Read Breakpoint\n" ) );
			break;
		case 15:            // Test, Clock, Idle
			LOG( ( "arm7_rt_r_callback, Test / Clock / Idle \n" ) );
			break;
	}

	return data;
}

WRITE32_MEMBER( arm7_cpu_device::arm7_rt_w_callback )
{
	uint32_t opcode = offset;
	uint8_t cReg = ( opcode & INSN_COPRO_CREG ) >> INSN_COPRO_CREG_SHIFT;
	uint8_t op2 =  ( opcode & INSN_COPRO_OP2 )  >> INSN_COPRO_OP2_SHIFT;
	uint8_t op3 =    opcode & INSN_COPRO_OP3;
	uint8_t cpnum = (opcode & INSN_COPRO_CPNUM) >> INSN_COPRO_CPNUM_SHIFT;

	// handle XScale specific CP14 - just eat writes for now
	if (cpnum != 15)
	{
		if (cpnum == 14)
		{
			LOG( ("arm7_rt_w_callback: write %x to XScale CP14 reg %d\n", data, cReg) );
			return;
		}
		else
		{
			LOG( ("ARM7: Unhandled coprocessor %d\n", cpnum) );
			m_core->m_pendingUnd = true;
			m_core->m_pending_interrupt = true;
			return;
		}
	}

	switch( cReg )
	{
		case 0:
		case 4:
		case 10:
		case 11:
		case 12:
			// RESERVED
			LOG( ( "arm7_rt_w_callback CR%d, RESERVED = %08x\n", cReg, data) );
			break;
		case 1:             // Control
		{
			LOG( ( "arm7_rt_w_callback Control = %08x (%d) (%d)\n", data, op2, op3 ) );
			LOG( ( "    MMU:%d, Address Fault:%d, Data Cache:%d, Write Buffer:%d\n",
					data & COPRO_CTRL_MMU_EN, ( data & COPRO_CTRL_ADDRFAULT_EN ) >> COPRO_CTRL_ADDRFAULT_EN_SHIFT,
					( data & COPRO_CTRL_DCACHE_EN ) >> COPRO_CTRL_DCACHE_EN_SHIFT,
					( data & COPRO_CTRL_WRITEBUF_EN ) >> COPRO_CTRL_WRITEBUF_EN_SHIFT ) );
			LOG( ( "    Endianness:%d, System:%d, ROM:%d, Instruction Cache:%d\n",
					( data & COPRO_CTRL_ENDIAN ) >> COPRO_CTRL_ENDIAN_SHIFT,
					( data & COPRO_CTRL_SYSTEM ) >> COPRO_CTRL_SYSTEM_SHIFT,
					( data & COPRO_CTRL_ROM ) >> COPRO_CTRL_ROM_SHIFT,
					( data & COPRO_CTRL_ICACHE_EN ) >> COPRO_CTRL_ICACHE_EN_SHIFT ) );
			LOG( ( "    Int Vector Adjust:%d\n", ( data & COPRO_CTRL_INTVEC_ADJUST ) >> COPRO_CTRL_INTVEC_ADJUST_SHIFT ) );

			uint32_t old_enable = COPRO_CTRL & COPRO_CTRL_MMU_EN;
			COPRO_CTRL = data & COPRO_CTRL_MASK;
			if ((COPRO_CTRL & COPRO_CTRL_MMU_EN) != old_enable)
				m_mode_changed = true;
			break;
		}
		case 2:             // Translation Table Base
			LOG( ( "arm7_rt_w_callback TLB Base = %08x (%d) (%d)\n", data, op2, op3 ) );
			COPRO_TLB_BASE = data;
			m_core->m_tlb_base_mask = data & COPRO_TLB_BASE_MASK;
			m_tlb_base = (uint32_t*)m_direct->read_ptr(m_core->m_tlb_base_mask);
			break;
		case 3:             // Domain Access Control
			LOG( ( "arm7_rt_w_callback Domain Access Control = %08x (%d) (%d)\n", data, op2, op3 ) );
			COPRO_DOMAIN_ACCESS_CONTROL = data;
			for (int i = 0; i < 32; i += 2)
			{
				m_core->m_decoded_access_control[i >> 1] = ((COPRO_DOMAIN_ACCESS_CONTROL >> i) & 3) << 6;
			}
			break;
		case 5:             // Fault Status
			LOG( ( "arm7_rt_w_callback Fault Status = %08x (%d) (%d)\n", data, op2, op3 ) );
			switch (op3)
			{
				case 0: COPRO_FAULT_STATUS_D = data; break;
				case 1: COPRO_FAULT_STATUS_P = data; break;
			}
			break;
		case 6:             // Fault Address
			LOG( ( "arm7_rt_w_callback Fault Address = %08x (%d) (%d)\n", data, op2, op3 ) );
			COPRO_FAULT_ADDRESS = data;
			break;
		case 7:             // Cache Operations
//            LOG( ( "arm7_rt_w_callback Cache Ops = %08x (%d) (%d)\n", data, op2, op3 ) );
			break;
		case 8:             // TLB Operations
			LOG( ( "arm7_rt_w_callback TLB Ops = %08x (%d) (%d)\n", data, op2, op3 ) );
			break;
		case 9:             // Read Buffer Operations
			LOG( ( "arm7_rt_w_callback Read Buffer Ops = %08x (%d) (%d)\n", data, op2, op3 ) );
			break;
		case 13:            // Write Process ID (PID)
			LOG( ( "arm7_rt_w_callback Write PID = %08x (%d) (%d)\n", data, op2, op3 ) );
			COPRO_FCSE_PID = data;
			m_core->m_pid_offset = (((COPRO_FCSE_PID >> 25) & 0x7F)) * 0x2000000;
			break;
		case 14:            // Write Breakpoint
			LOG( ( "arm7_rt_w_callback Write Breakpoint = %08x (%d) (%d)\n", data, op2, op3 ) );
			break;
		case 15:            // Test, Clock, Idle
			LOG( ( "arm7_rt_w_callback Test / Clock / Idle = %08x (%d) (%d)\n", data, op2, op3 ) );
			break;
	}
}

READ32_MEMBER( arm946es_cpu_device::arm7_rt_r_callback )
{
	uint32_t opcode = offset;
	uint8_t cReg = ( opcode & INSN_COPRO_CREG ) >> INSN_COPRO_CREG_SHIFT;
	uint8_t op2 =  ( opcode & INSN_COPRO_OP2 )  >> INSN_COPRO_OP2_SHIFT;
	uint8_t op3 =    opcode & INSN_COPRO_OP3;
	uint8_t cpnum = (opcode & INSN_COPRO_CPNUM) >> INSN_COPRO_CPNUM_SHIFT;
	uint32_t data = 0;

	//printf("arm7946: read cpnum %d cReg %d op2 %d op3 %d (%x)\n", cpnum, cReg, op2, op3, opcode);
	if (cpnum == 15)
	{
		switch( cReg )
		{
			case 0:
				switch (op2)
				{
					case 0: // chip ID
						data = 0x41059461;
						break;

					case 1: // cache ID
						data = 0x0f0d2112;
						break;

					case 2: // TCM size
						data = (6 << 6) | (5 << 18);
						break;
				}
				break;

			case 1:
				return cp15_control;
				break;

			case 9:
				if (op3 == 1)
				{
					if (op2 == 0)
					{
						return cp15_dtcm_reg;
					}
					else
					{
						return cp15_itcm_reg;
					}
				}
				break;
		}
	}

	return data;
}

WRITE32_MEMBER( arm946es_cpu_device::arm7_rt_w_callback )
{
	uint32_t opcode = offset;
	uint8_t cReg = ( opcode & INSN_COPRO_CREG ) >> INSN_COPRO_CREG_SHIFT;
	uint8_t op2 =  ( opcode & INSN_COPRO_OP2 )  >> INSN_COPRO_OP2_SHIFT;
	uint8_t op3 =    opcode & INSN_COPRO_OP3;
	uint8_t cpnum = (opcode & INSN_COPRO_CPNUM) >> INSN_COPRO_CPNUM_SHIFT;

//  printf("arm7946: copro %d write %x to cReg %d op2 %d op3 %d (mask %08x)\n", cpnum, data, cReg, op2, op3, mem_mask);

	if (cpnum == 15)
	{
		switch (cReg)
		{
			case 1: // control
				cp15_control = data;
				RefreshDTCM();
				RefreshITCM();
				break;

			case 2: // Protection Unit cacheability bits
				break;

			case 3: // write bufferability bits for PU
				break;

			case 5: // protection unit region controls
				break;

			case 6: // protection unit region controls 2
				break;

			case 7: // cache commands
				break;

			case 9: // cache lockdown & TCM controls
				if (op3 == 1)
				{
					if (op2 == 0)
					{
						cp15_dtcm_reg = data;
						RefreshDTCM();
					}
					else if (op2 == 1)
					{
						cp15_itcm_reg = data;
						RefreshITCM();
					}
				}
				break;
		}
	}
}

void arm946es_cpu_device::RefreshDTCM()
{
	if (cp15_control & (1<<16))
	{
		cp15_dtcm_base = (cp15_dtcm_reg & ~0xfff);
		cp15_dtcm_size = 512 << ((cp15_dtcm_reg & 0x3f) >> 1);
		cp15_dtcm_end = cp15_dtcm_base + cp15_dtcm_size;
		//printf("DTCM enabled: base %08x size %x\n", cp15_dtcm_base, cp15_dtcm_size);
	}
	else
	{
		cp15_dtcm_base = 0xffffffff;
		cp15_dtcm_size = cp15_dtcm_end = 0;
	}
}

void arm946es_cpu_device::RefreshITCM()
{
	if (cp15_control & (1<<18))
	{
		cp15_itcm_base = 0; //(cp15_itcm_reg & ~0xfff);
		cp15_itcm_size = 512 << ((cp15_itcm_reg & 0x3f) >> 1);
		cp15_itcm_end = cp15_itcm_base + cp15_itcm_size;
		//printf("ITCM enabled: base %08x size %x\n", cp15_dtcm_base, cp15_dtcm_size);
	}
	else
	{
		cp15_itcm_base = 0xffffffff;
		cp15_itcm_size = cp15_itcm_end = 0;
	}
}

void arm946es_cpu_device::arm7_cpu_write32(uint32_t addr, uint32_t data)
{
	addr &= ~3;

	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		uint32_t *wp = (uint32_t *)&ITCM[addr&0x7fff];
		*wp = data;
		return;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		uint32_t *wp = (uint32_t *)&DTCM[addr&0x3fff];
		*wp = data;
		return;
	}

	m_program->write_dword(addr, data);
}


void arm946es_cpu_device::arm7_cpu_write16(uint32_t addr, uint16_t data)
{
	addr &= ~1;
	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		uint16_t *wp = (uint16_t *)&ITCM[addr&0x7fff];
		*wp = data;
		return;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		uint16_t *wp = (uint16_t *)&DTCM[addr&0x3fff];
		*wp = data;
		return;
	}

	m_program->write_word(addr, data);
}

void arm946es_cpu_device::arm7_cpu_write8(uint32_t addr, uint8_t data)
{
	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		ITCM[addr&0x7fff] = data;
		return;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		DTCM[addr&0x3fff] = data;
		return;
	}

	m_program->write_byte(addr, data);
}

uint32_t arm946es_cpu_device::arm7_cpu_read32(uint32_t addr)
{
	uint32_t result;

	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		if (addr & 3)
		{
			uint32_t *wp = (uint32_t *)&ITCM[(addr & ~3)&0x7fff];
			result = *wp;
			result = (result >> (8 * (addr & 3))) | (result << (32 - (8 * (addr & 3))));
		}
		else
		{
			uint32_t *wp = (uint32_t *)&ITCM[addr&0x7fff];
			result = *wp;
		}
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		if (addr & 3)
		{
			uint32_t *wp = (uint32_t *)&DTCM[(addr & ~3)&0x3fff];
			result = *wp;
			result = (result >> (8 * (addr & 3))) | (result << (32 - (8 * (addr & 3))));
		}
		else
		{
			uint32_t *wp = (uint32_t *)&DTCM[addr&0x3fff];
			result = *wp;
		}
	}
	else
	{
		if (addr & 3)
		{
			result = m_program->read_dword(addr & ~3);
			result = (result >> (8 * (addr & 3))) | (result << (32 - (8 * (addr & 3))));
		}
		else
		{
			result = m_program->read_dword(addr);
		}
	}
	return result;
}

uint32_t arm946es_cpu_device::arm7_cpu_read16(uint32_t addr)
{
	addr &= ~1;

	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		uint16_t *wp = (uint16_t *)&ITCM[addr & 0x7fff];
		return *wp;
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		uint16_t *wp = (uint16_t *)&DTCM[addr &0x3fff];
		return *wp;
	}

	return m_program->read_word(addr);
}

uint8_t arm946es_cpu_device::arm7_cpu_read8(uint32_t addr)
{
	if ((addr >= cp15_itcm_base) && (addr <= cp15_itcm_end))
	{
		return ITCM[addr & 0x7fff];
	}
	else if ((addr >= cp15_dtcm_base) && (addr <= cp15_dtcm_end))
	{
		return DTCM[addr & 0x3fff];
	}

	// Handle through normal 8 bit handler (for 32 bit cpu)
	return m_program->read_byte(addr);
}

void arm7_cpu_device::arm7_dt_r_callback(uint32_t *prn)
{
	uint8_t cpn = (m_insn >> 8) & 0xF;
	if ((m_archFlags & ARCHFLAG_XSCALE) && (cpn == 0))
	{
		LOG( ( "arm7_dt_r_callback: DSP Coprocessor 0 (CP0) not yet emulated (PC %08x)\n", GET_PC ) );
	}
	else
	{
		m_core->m_pendingUnd = true;
		m_core->m_pending_interrupt = true;
	}
}


void arm7_cpu_device::arm7_dt_w_callback(uint32_t *prn)
{
	uint8_t cpn = (m_insn >> 8) & 0xF;
	if ((m_archFlags & ARCHFLAG_XSCALE) && (cpn == 0))
	{
		LOG( ( "arm7_dt_w_callback: DSP Coprocessor 0 (CP0) not yet emulated (PC %08x)\n", GET_PC ) );
	}
	else
	{
		m_core->m_pendingUnd = true;
		m_core->m_pending_interrupt = true;
	}
}


/***************************************************************************
 * Default Memory Handlers
 ***************************************************************************/
void arm7_cpu_device::arm7_cpu_write32(uint32_t addr, uint32_t data)
{
	if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
	{
		if (!arm7_tlb_translate(addr, ARM7_TLB_ABORT_D | ARM7_TLB_WRITE))
		{
			return;
		}
	}

	addr &= ~3;
	m_program->write_dword(addr, data);
}


void arm7_cpu_device::arm7_cpu_write16(uint32_t addr, uint16_t data)
{
	if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
	{
		if (!arm7_tlb_translate(addr, ARM7_TLB_ABORT_D | ARM7_TLB_WRITE))
		{
			return;
		}
	}

	addr &= ~1;
	m_program->write_word(addr, data);
}

void arm7_cpu_device::arm7_cpu_write8(uint32_t addr, uint8_t data)
{
	if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
	{
		if (!arm7_tlb_translate(addr, ARM7_TLB_ABORT_D | ARM7_TLB_WRITE))
		{
			return;
		}
	}

	m_program->write_byte(addr, data);
}

uint32_t arm7_cpu_device::arm7_cpu_read32(uint32_t addr)
{
	uint32_t result;

	if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
	{
		if (!arm7_tlb_translate(addr, ARM7_TLB_ABORT_D | ARM7_TLB_READ))
		{
			return 0;
		}
	}

	if (addr & 3)
	{
		result = m_program->read_dword(addr & ~3);
		result = (result >> (8 * (addr & 3))) | (result << (32 - (8 * (addr & 3))));
	}
	else
	{
		result = m_program->read_dword(addr);
	}

	return result;
}

uint32_t arm7_cpu_device::arm7_cpu_read16(uint32_t addr)
{
	uint32_t result;

	if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
	{
		if (!arm7_tlb_translate(addr, ARM7_TLB_ABORT_D | ARM7_TLB_READ))
		{
			return 0;
		}
	}

	result = m_program->read_word(addr & ~1);

	if (addr & 1)
	{
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 24);
	}

	return result;
}

uint8_t arm7_cpu_device::arm7_cpu_read8(uint32_t addr)
{
	if (COPRO_CTRL & COPRO_CTRL_MMU_EN)
	{
		if (!arm7_tlb_translate(addr, ARM7_TLB_ABORT_D | ARM7_TLB_READ))
		{
			return 0;
		}
	}

	// Handle through normal 8 bit handler (for 32 bit cpu)
	return m_program->read_byte(addr);
}
