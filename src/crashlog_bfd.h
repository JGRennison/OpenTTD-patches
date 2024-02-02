/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog_bfd.h Definitions for utility functions for using libbfd as part of logging a crash */

#ifndef CRASHLOG_BFD_H
#define CRASHLOG_BFD_H

#if defined(WITH_BFD0)
#define WITH_BFD 1
#define get_bfd_section_size(abfd, section) bfd_section_size(abfd, section)
#elif defined(WITH_BFD1)
#define WITH_BFD 1
#define bfd_get_section_flags(abfd, section) bfd_section_flags(section)
#define bfd_get_section_vma(abfd, section) bfd_section_vma(section)
#define get_bfd_section_size(abfd, section) bfd_section_size(section)
#endif

#if defined(WITH_BFD)
/* this is because newer versions of libbfd insist on seeing these, even though they aren't used for anything */
#define PACKAGE 1
#define PACKAGE_VERSION 1
#include <bfd.h>
#undef PACKAGE
#undef PACKAGE_VERSION
#endif

#include <map>

#if defined(WITH_BFD)

struct sym_bfd_obj {
	bfd *abfd = nullptr;
	asymbol **syms = nullptr;
	const char *file_name = nullptr;
	long sym_count = 0;
	bool usable = false;

	~sym_bfd_obj();
};

struct sym_bfd_obj_cache {
	std::map<std::string, sym_bfd_obj> cache;
};

struct sym_info_bfd {
	bfd_vma addr;
	bfd *abfd;
	asymbol **syms;
	long sym_count;
	const char *file_name;
	const char *function_name;
	bfd_vma function_addr;
	unsigned int line;
	bool found;

	sym_info_bfd(bfd_vma addr_);
};

void lookup_addr_bfd(const char *obj_file_name, sym_bfd_obj_cache &bfdc, sym_info_bfd &info);

#endif

#endif /* CRASHLOG_BFD_H */
