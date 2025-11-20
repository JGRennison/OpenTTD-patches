/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strgen.cpp Tool to create computer readable (stand-alone) translation files. */

#include "../stdafx.h"
#include "../core/endian_func.hpp"
#include "../core/mem_func.hpp"
#include "../error_func.h"
#include "../fileio_type.h"
#include "../string_func.h"
#include "../strings_type.h"
#include "../misc/getoptdata.h"
#include "../table/control_codes.h"

#include "strgen.h"

#include <exception>

#if !defined(_WIN32) || defined(__CYGWIN__)
#include <unistd.h>
#include <sys/stat.h>
#endif

#if defined(_WIN32) || defined(__WATCOMC__)
#include <direct.h>
#endif /* _WIN32 || __WATCOMC__ */

#include "../table/strgen_tables.h"

#include "../safeguards.h"


#ifdef _MSC_VER
# define LINE_NUM_FMT(s) "{} ({}): warning: {} (" s ")\n"
#else
# define LINE_NUM_FMT(s) "{}:{}: " s ": {}\n"
#endif

void StrgenWarningI(const std::string &msg)
{
	if (_strgen.translation) {
		fmt::print(stderr, LINE_NUM_FMT("info"), _strgen.file, _strgen.cur_line, msg);
	} else {
		fmt::print(stderr, LINE_NUM_FMT("warning"), _strgen.file, _strgen.cur_line, msg);
	}
	_strgen.warnings++;
}

void StrgenErrorI(const std::string &msg)
{
	fmt::print(stderr, LINE_NUM_FMT("error"), _strgen.file, _strgen.cur_line, msg);
	_strgen.errors++;
}

[[noreturn]] void StrgenFatalI(const std::string &msg)
{
	fmt::print(stderr, LINE_NUM_FMT("FATAL"), _strgen.file, _strgen.cur_line, msg);
#ifdef _MSC_VER
	fmt::print(stderr, LINE_NUM_FMT("warning"), _strgen.file, _strgen.cur_line, "language is not compiled");
#endif
	throw std::exception();
}

[[noreturn]] void FatalErrorI(const std::string &msg)
{
	fmt::print(stderr, LINE_NUM_FMT("FATAL"), _strgen.file, _strgen.cur_line, msg);
#ifdef _MSC_VER
	fmt::print(stderr, LINE_NUM_FMT("warning"), _strgen.file, _strgen.cur_line, "language is not compiled");
#endif
	exit(2);
}

/**
 * Simplified FileHandle::Open which ignores OTTD2FS. Required as strgen does not include all of the fileio system.
 * @param filename UTF-8 encoded filename to open.
 * @param mode Mode to open file.
 * @return FileHandle, or std::nullopt on failure.
 */
std::optional<FileHandle> FileHandle::Open(const char *filename, const char *mode)
{
	auto f = fopen(filename, mode);
	if (f == nullptr) return std::nullopt;
	return FileHandle(f);
}

/** A reader that simply reads using fopen. */
struct FileStringReader : StringReader {
	std::optional<FileHandle> fh;  ///< The file we are reading.
	std::optional<FileHandle> fh2; ///< The file we are reading.
	std::string file2;

	/**
	 * Create the reader.
	 * @param data        The data to fill during reading.
	 * @param file        The file we are reading.
	 * @param master      Are we reading the master file?
	 * @param translation Are we reading a translation?
	 */
	FileStringReader(StringData &data, const char *file, const char *file2, bool master, bool translation) :
			StringReader(data, file, master, translation)
	{
		this->fh = FileHandle::Open(file, "rb");
		if (!this->fh.has_value()) FatalError("Could not open {}", file);

		if (file2 != nullptr) {
			this->file2.assign(file2);
			this->fh2 = FileHandle::Open(file2, "rb");
			if (!this->fh2.has_value()) FatalError("Could not open {}", file2);
		}
	}

	FileStringReader(StringData &data, const char *file, bool master, bool translation) :
			FileStringReader(data, file, nullptr, master, translation) {}

	char *ReadLine(char *buffer, const char *last) override
	{
		char *result = fgets(buffer, ClampTo<uint16_t>(last - buffer + 1), *this->fh);
		if (result == nullptr && this->fh2.has_value()) {
			this->fh = std::move(this->fh2);
			this->fh2.reset();
			this->file = std::move(this->file2);
			_strgen.file = this->file.c_str();
			_strgen.cur_line = 1;
			return this->FileStringReader::ReadLine(buffer, last);
		}
		return result;
	}

	void HandlePragma(std::string_view str, LanguagePackHeader &lang) override;

	void ParseFile() override
	{
		this->StringReader::ParseFile();

		if (StrEmpty(_strgen.lang.name) || StrEmpty(_strgen.lang.own_name) || StrEmpty(_strgen.lang.isocode)) {
			FatalError("Language must include ##name, ##ownname and ##isocode");
		}
	}
};

void FileStringReader::HandlePragma(std::string_view str, LanguagePackHeader &lang)
{
	StringConsumer consumer(str);
	auto name = consumer.ReadUntilChar(' ', StringConsumer::SKIP_ALL_SEPARATORS);
	if (name == "id") {
		this->data.next_string_id = consumer.ReadIntegerBase<uint32_t>(0);
	} else if (name == "name") {
		strecpy(lang.name, consumer.Read(StringConsumer::npos));
	} else if (name == "ownname") {
		strecpy(lang.own_name, consumer.Read(StringConsumer::npos));
	} else if (name == "isocode") {
		strecpy(lang.isocode, consumer.Read(StringConsumer::npos));
	} else if (name == "textdir") {
		auto dir = consumer.Read(StringConsumer::npos);
		if (dir == "ltr") {
			lang.text_dir = TD_LTR;
		} else if (dir == "rtl") {
			lang.text_dir = TD_RTL;
		} else {
			FatalError("Invalid textdir {}", dir);
		}
	} else if (name == "digitsep") {
		auto sep = consumer.Read(StringConsumer::npos);
		strecpy(lang.digit_group_separator, sep == "{NBSP}" ? NBSP : sep);
	} else if (name == "digitsepcur") {
		auto sep = consumer.Read(StringConsumer::npos);
		strecpy(lang.digit_group_separator_currency, sep == "{NBSP}" ? NBSP : sep);
	} else if (name == "decimalsep") {
		auto sep = consumer.Read(StringConsumer::npos);
		strecpy(lang.digit_decimal_separator, sep == "{NBSP}" ? NBSP : sep);
	} else if (name == "winlangid") {
		auto langid = consumer.ReadIntegerBase<int32_t>(0);
		if (langid > UINT16_MAX || langid < 0) {
			FatalError("Invalid winlangid {}", langid);
		}
		lang.winlangid = static_cast<uint16_t>(langid);
	} else if (name == "grflangid") {
		auto langid = consumer.ReadIntegerBase<int32_t>(0);
		if (langid >= 0x7F || langid < 0) {
			FatalError("Invalid grflangid {}", langid);
		}
		lang.newgrflangid = static_cast<uint8_t>(langid);
	} else if (name == "gender") {
		if (this->master) FatalError("Genders are not allowed in the base translation.");
		for (;;) {
			auto s = ParseWord(consumer);

			if (!s.has_value()) break;
			if (lang.num_genders >= MAX_NUM_GENDERS) FatalError("Too many genders, max {}", MAX_NUM_GENDERS);
			s->copy(lang.genders[lang.num_genders], CASE_GENDER_LEN - 1);
			lang.num_genders++;
		}
	} else if (name == "case") {
		if (this->master) FatalError("Cases are not allowed in the base translation.");
		for (;;) {
			auto s = ParseWord(consumer);

			if (!s.has_value()) break;
			if (lang.num_cases >= MAX_NUM_CASES) FatalError("Too many cases, max {}", MAX_NUM_CASES);
			s->copy(lang.cases[lang.num_cases], CASE_GENDER_LEN - 1);
			lang.num_cases++;
		}
	} else if (name == "override") {
		if (this->translation) FatalError("Overrides are only allowed in the base translation.");
		consumer.SkipUntilCharNotIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		std::string_view mode = consumer.ReadUntilCharIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		if (mode == "on") {
			this->data.override_mode = true;
		} else if (mode == "off") {
			this->data.override_mode = false;
		} else {
			FatalError("Invalid override mode {}", mode);
		}
	} else if (name == "after") {
		if (this->translation) FatalError("Insert after is only allowed in the base translation.");
		consumer.SkipUntilCharNotIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		std::string_view target = consumer.ReadUntilCharIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		LangString *ent = this->data.Find(target);
		if (ent != nullptr) {
			this->data.insert_after = ent;
			this->data.insert_before = nullptr;
		} else {
			FatalError("Can't find string to insert after: '{}'", target);
		}
	} else if (name == "before") {
		if (this->translation) FatalError("Insert before is only allowed in the base translation.");
		consumer.SkipUntilCharNotIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		std::string_view target = consumer.ReadUntilCharIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		LangString *ent = this->data.Find(target);
		if (ent != nullptr) {
			this->data.insert_after = nullptr;
			this->data.insert_before = ent;
		} else {
			FatalError("Can't find string to insert after: '{}'", target);
		}
	} else if (name == "end-after") {
		if (this->translation) FatalError("Insert after is only allowed in the base translation.");
		this->data.insert_after = nullptr;
	} else if (name == "default-translation") {
		if (this->translation) FatalError("Default translation is only allowed in the base translation.");
		consumer.SkipUntilCharNotIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		std::string_view target = consumer.ReadUntilCharIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		LangString *ent = this->data.Find(target);
		if (ent != nullptr) {
			this->data.default_translation = ent;
		} else {
			FatalError("Can't find string to use as default translation: '{}'", target);
		}
	} else if (name == "no-translate") {
		if (this->translation) FatalError("No-translate sections are only allowed in the base translation.");
		consumer.SkipUntilCharNotIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		std::string_view mode = consumer.ReadUntilCharIn(StringConsumer::WHITESPACE_NO_NEWLINE);
		if (mode == "on") {
			this->data.no_translate_mode = true;
		} else if (mode == "off") {
			this->data.no_translate_mode = false;
		} else {
			FatalError("Invalid no-translate mode {}", mode);
		}
	} else {
		StringReader::HandlePragma(str, lang);
	}
}

bool CompareFiles(const char *n1, const char *n2)
{
	auto f2 = FileHandle::Open(n2, "rb");
	if (!f2.has_value()) return false;

	auto f1 = FileHandle::Open(n1, "rb");
	if (!f1.has_value()) {
		FatalError("can't open {}", n1);
	}

	size_t l1, l2;
	do {
		char b1[4096];
		char b2[4096];
		l1 = fread(b1, 1, sizeof(b1), *f1);
		l2 = fread(b2, 1, sizeof(b2), *f2);

		if (l1 != l2 || memcmp(b1, b2, l1)) {
			return false;
		}
	} while (l1 != 0);

	return true;
}

/** Base class for writing data to disk. */
struct FileWriter {
	std::optional<FileHandle> fh; ///< The file handle we're writing to.
	std::string filename;         ///< The file name we're writing to.

	/**
	 * Open a file to write to.
	 * @param filename The file to open.
	 */
	FileWriter(const char *filename)
	{
		this->filename = filename;
		this->fh = FileHandle::Open(filename, "wb");

		if (!this->fh.has_value()) {
			FatalError("Could not open {}", filename);
		}
	}

	/** Finalise the writing. */
	void Finalise()
	{
		this->fh.reset();
	}

	/** Make sure the file is closed. */
	virtual ~FileWriter()
	{
		/* If we weren't closed an exception was thrown, so remove the temporary file. */
		if (this->fh.has_value()) {
			this->fh.reset();
			unlink(this->filename.c_str());
		}
	}
};

struct HeaderFileWriter : HeaderWriter, FileWriter {
	/** The real file name we eventually want to write to. */
	std::string real_filename;
	/** The previous string ID that was printed. */
	uint prev;
	uint total_strings;

	/**
	 * Open a file to write to.
	 * @param filename The file to open.
	 */
	HeaderFileWriter(const char *filename) : FileWriter("tmp.xxx"),
		real_filename(filename), prev(0), total_strings(0)
	{
		fprintf(*this->fh, "/* This file is automatically generated. Do not modify */\n\n");
		fprintf(*this->fh, "#ifndef TABLE_STRINGS_H\n");
		fprintf(*this->fh, "#define TABLE_STRINGS_H\n");
	}

	void WriteStringID(const std::string &name, uint stringid) override
	{
		if (stringid == 0) {
			if (name != "STR_NULL") StrgenFatal("String ID 0 is not STR_NULL");
			total_strings++;
			return;
		}

		if (prev + 1 != stringid) fprintf(*this->fh, "\n");
		fprintf(*this->fh, "static const StringID %s = 0x%X;\n", name.c_str(), stringid);
		prev = stringid;
		total_strings++;
	}

	void Finalise(const StringData &data) override
	{
		/* Find the plural form with the most amount of cases. */
		size_t max_plural_forms = 0;
		for (const auto &pf : _plural_forms) {
			max_plural_forms = std::max(max_plural_forms, pf.plural_count);
		}

		fprintf(*this->fh,
			"\n"
			"static const uint LANGUAGE_PACK_VERSION     = 0x%X;\n"
			"static const uint LANGUAGE_MAX_PLURAL       = %u;\n"
			"static const uint LANGUAGE_MAX_PLURAL_FORMS = %u;\n"
			"static const uint LANGUAGE_TOTAL_STRINGS    = %u;\n"
			"\n",
			(uint)data.Version(), (uint)std::size(_plural_forms), (uint)max_plural_forms, total_strings
		);

		fprintf(*this->fh, "#endif /* TABLE_STRINGS_H */\n");

		this->FileWriter::Finalise();

		if (CompareFiles(this->filename.c_str(), this->real_filename.c_str())) {
			/* files are equal. tmp.xxx is not needed */
			unlink(this->filename.c_str());
		} else {
			/* else rename tmp.xxx into filename */
#	if defined(_WIN32)
			unlink(this->real_filename.c_str());
#	endif
			if (rename(this->filename.c_str(), this->real_filename.c_str()) == -1) {
				FatalError("rename({}, {}) failed: {}", this->filename, this->real_filename.c_str(), StrErrorDumper().GetLast());
			}
		}
	}
};

/** Class for writing a language to disk. */
struct LanguageFileWriter : LanguageWriter, FileWriter {
	/**
	 * Open a file to write to.
	 * @param filename The file to open.
	 */
	LanguageFileWriter(const char *filename) : FileWriter(filename)
	{
	}

	void WriteHeader(const LanguagePackHeader *header) override
	{
		this->Write({reinterpret_cast<const char *>(header), sizeof(*header)});
	}

	void Finalise() override
	{
		if (fputc(0, *this->fh) == EOF) {
			FatalError("Could not write to {}", this->filename);
		}
		this->FileWriter::Finalise();
	}

	void Write(std::string_view buffer) override
	{
		if (buffer.empty()) return;
		if (fwrite(buffer.data(), sizeof(*buffer.data()), buffer.size(), *this->fh) != buffer.size()) {
			FatalError("Could not write to {}", this->filename);
		}
	}
};

/** Multi-OS mkdirectory function */
static inline void ottd_mkdir(const char *directory)
{
	/* Ignore directory creation errors; they'll surface later on, and most
	 * of the time they are 'directory already exists' errors anyhow. */
#if defined(_WIN32) || defined(__WATCOMC__)
	mkdir(directory);
#else
	mkdir(directory, 0755);
#endif
}

/**
 * Create a path consisting of an already existing path, a possible
 * path separator and the filename. The separator is only appended if the path
 * does not already end with a separator
 */
static inline char *mkpath2(char *buf, const char *last, const char *path, const char *path2, const char *file)
{
	strecpy(buf, path, last); // copy directory into buffer

	char *p = strchr(buf, '\0'); // add path separator if necessary

	if (path2 != nullptr) {
		if (p[-1] != PATHSEPCHAR && p != last) *p++ = PATHSEPCHAR;
		strecpy(p, path2, last); // concatenate filename at end of buffer
		p = strchr(buf, '\0');
	}

	if (p[-1] != PATHSEPCHAR && p != last) *p++ = PATHSEPCHAR;
	strecpy(p, file, last); // concatenate filename at end of buffer
	return buf;
}

/**
 * Create a path consisting of an already existing path, a possible
 * path separator and the filename. The separator is only appended if the path
 * does not already end with a separator
 */
static inline char *mkpath(char *buf, const char *last, const char *path, const char *file)
{
	return mkpath2(buf, last, path, nullptr, file);
}

#if defined(_WIN32)
/**
 * On MingW, it is common that both / as \ are accepted in the
 * params. To go with those flow, we rewrite all incoming /
 * simply to \, so internally we can safely assume \, and do
 * this for all Windows machines to keep identical behaviour,
 * no matter what your compiler was.
 */
static std::string replace_pathsep(std::string s)
{
	for (char &c : s) {
		if (c == '/') c = '\\';
	}
	return s;
}
#else
static std::string replace_pathsep(std::string s) { return s; }
#endif

/** Options of strgen. */
static const OptionData _opts[] = {
	{ .type = ODF_NO_VALUE, .id = 'C', .longname = "-export-commands" },
	{ .type = ODF_NO_VALUE, .id = 'L', .longname = "-export-plurals" },
	{ .type = ODF_NO_VALUE, .id = 'P', .longname = "-export-pragmas" },
	{ .type = ODF_NO_VALUE, .id = 't', .shortname = 't', .longname = "--todo" },
	{ .type = ODF_NO_VALUE, .id = 'w', .shortname = 'w', .longname = "--warning" },
	{ .type = ODF_NO_VALUE, .id = 'h', .shortname = 'h', .longname = "--help" },
	{ .type = ODF_NO_VALUE, .id = 'h', .shortname = '?' },
	{ .type = ODF_HAS_VALUE, .id = 's', .shortname = 's', .longname = "--source_dir" },
	{ .type = ODF_HAS_VALUE, .id = 'd', .shortname = 'd', .longname = "--dest_dir" },
};

int CDECL main(int argc, char *argv[])
{
	char pathbuf[MAX_PATH];
	char pathbuf2[MAX_PATH];
	std::string src_dir = ".";
	std::string dest_dir;

	GetOptData mgo(std::span(argv + 1, argc - 1), _opts);
	for (;;) {
		int i = mgo.GetOpt();
		if (i == -1) break;

		switch (i) {
			case 'C':
				printf("args\tflags\tcommand\treplacement\n");
				for (const auto &cs : _cmd_structs) {
					char flags;
					if (cs.proc == EmitGender) {
						flags = 'g'; // Command needs number of parameters defined by number of genders
					} else if (cs.proc == EmitPlural) {
						flags = 'p'; // Command needs number of parameters defined by plural value
					} else if (cs.flags.Test(CmdFlag::DontCount)) {
						flags = 'i'; // Command may be in the translation when it is not in base
					} else {
						flags = '0'; // Command needs no parameters
					}
					fmt::print("{}\t{:c}\t\"{}\"\t\"{}\"\n", cs.consumes, flags, cs.cmd, cs.cmd.find("STRING") != std::string::npos ? "STRING" : cs.cmd);
				}
				return 0;

			case 'L':
				printf("count\tdescription\tnames\n");
				for (const auto &pf : _plural_forms) {
					printf("%u\t\"%s\"\t%s\n", (uint)pf.plural_count, pf.description, pf.names);
				}
				return 0;

			case 'P':
				printf("name\tflags\tdefault\tdescription\n");
				for (const auto &pragma : _pragmas) {
					printf("\"%s\"\t%s\t\"%s\"\t\"%s\"\n",
							pragma[0], pragma[1], pragma[2], pragma[3]);
				}
				return 0;

			case 't':
				_strgen.annotate_todos = true;
				break;

			case 'w':
				_strgen.show_warnings = true;
				break;

			case 'h':
				puts(
					"strgen\n"
					" -t | --todo       replace any untranslated strings with '<TODO>'\n"
					" -w | --warning    print a warning for any untranslated strings\n"
					" -h | -? | --help  print this help message and exit\n"
					" -s | --source_dir search for english.txt in the specified directory\n"
					" -d | --dest_dir   put output file in the specified directory, create if needed\n"
					" -export-commands  export all commands and exit\n"
					" -export-plurals   export all plural forms and exit\n"
					" -export-pragmas   export all pragmas and exit\n"
					" Run without parameters and strgen will search for english.txt and parse it,\n"
					" creating strings.h. Passing an argument, strgen will translate that language\n"
					" file using english.txt as a reference and output <language>.lng."
				);
				return 0;

			case 's':
				src_dir = replace_pathsep(mgo.opt);
				break;

			case 'd':
				dest_dir = replace_pathsep(mgo.opt);
				break;

			case -2:
				fprintf(stderr, "Invalid arguments\n");
				return 0;
		}
	}

	if (dest_dir.empty()) dest_dir = src_dir; // if dest_dir is not specified, it equals src_dir

	try {
		/* strgen has two modes of operation. If no (free) arguments are passed
		 * strgen generates strings.h to the destination directory. If it is supplied
		 * with a (free) parameter the program will translate that language to destination
		 * directory. As input english.txt is parsed from the source directory */
		if (mgo.arguments.empty()) {
			mkpath(pathbuf, lastof(pathbuf), src_dir.c_str(), "english.txt");
			mkpath2(pathbuf2, lastof(pathbuf2), src_dir.c_str(), "extra", "english.txt");

			/* parse master file */
			StringData data(TEXT_TAB_END);
			FileStringReader master_reader(data, pathbuf, pathbuf2, true, false);
			master_reader.ParseFile();
			if (_strgen.errors != 0) return 1;

			/* write strings.h */
			ottd_mkdir(dest_dir.c_str());
			mkpath(pathbuf, lastof(pathbuf), dest_dir.c_str(), "strings.h");

			HeaderFileWriter writer(pathbuf);
			writer.WriteHeader(data);
			writer.Finalise(data);
			if (_strgen.errors != 0) return 1;
		} else {
			mkpath(pathbuf, lastof(pathbuf), src_dir.c_str(), "english.txt");
			mkpath2(pathbuf2, lastof(pathbuf2), src_dir.c_str(), "extra", "english.txt");

			StringData data(TEXT_TAB_END);
			/* parse master file and check if target file is correct */
			FileStringReader master_reader(data, pathbuf, pathbuf2, true, false);
			master_reader.ParseFile();

			for (auto &argument : mgo.arguments) {
				data.FreeTranslation();

				const std::string translation = replace_pathsep(argument);
				const char *file = strrchr(translation.c_str(), PATHSEPCHAR);
				const char *translation2 = nullptr;
				if (file != nullptr) {
					mkpath2(pathbuf2, lastof(pathbuf2), src_dir.c_str(), "extra", file + 1);
					translation2 = pathbuf2;
				}
				FileStringReader translation_reader(data, translation.c_str(), translation2, false, file == nullptr || strcmp(file + 1, "english.txt") != 0);
				translation_reader.ParseFile(); // target file
				if (_strgen.errors != 0) return 1;

				/* get the targetfile, strip any directories and append to destination path */
				mkpath(pathbuf, lastof(pathbuf), dest_dir.c_str(), (file != nullptr) ? file + 1 : translation.c_str());

				/* rename the .txt (input-extension) to .lng */
				char *r = strrchr(pathbuf, '.');
				if (r == nullptr || strcmp(r, ".txt") != 0) r = strchr(pathbuf, '\0');
				strecpy(r, ".lng", lastof(pathbuf));

				LanguageFileWriter writer(pathbuf);
				writer.WriteLang(data);
				writer.Finalise();

				/* if showing warnings, print a summary of the language */
				if (_strgen.show_warnings) {
					fmt::print("{} warnings and {} errors for {}\n", _strgen.warnings, _strgen.errors, pathbuf);
				}
			}
		}
	} catch (...) {
		return 2;
	}

	return 0;
}
