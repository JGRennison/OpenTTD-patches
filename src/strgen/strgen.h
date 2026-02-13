/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strgen.h Structures related to strgen. */

#ifndef STRGEN_H
#define STRGEN_H

#include "../core/string_consumer.hpp"
#include "../language.h"
#include "../3rdparty/robin_hood/robin_hood.h"

#include <memory>
#include <string>
#include <vector>

#include <array>

/** Container for the different cases of a string. */
struct Case {
	uint8_t caseidx;    ///< The index of the case.
	std::string string; ///< The translation of the case.

	/**
	 * Create a new case.
	 * @param caseidx The index of the case.
	 * @param string  The translation of the case.
	 */
	Case(uint8_t caseidx, std::string_view string) :
			caseidx(caseidx), string(string) {}
};

/** Information about a single string. */
struct LangString {
	std::string name;       ///< Name of the string.
	std::string english;    ///< English text.
	std::string translated; ///< Translated text.
	int index;              ///< The index in the language file.
	uint line;              ///< Line of string in source-file.
	std::vector<Case> translated_cases; ///< Cases of the translation.
	std::unique_ptr<LangString> chain_before;
	std::unique_ptr<LangString> chain_after;
	bool no_translate_mode = false;
	LangString *default_translation = nullptr;

	LangString(std::string_view name, std::string_view english, int index, uint line);
	void ReplaceDefinition(std::string_view english, uint line);
	void FreeTranslation();
};

/** Information about the currently known strings. */
struct StringData {
	std::vector<LangString *> strings; ///< List of all known strings.
	robin_hood::unordered_map<std::string_view, LangString *> name_to_string; ///< Lookup table for the strings.
	uint tabs;            ///< The number of 'tabs' of strings.
	uint max_strings;     ///< The maximum number of strings.
	int next_string_id;   ///< The next string ID to allocate.

	std::vector<std::unique_ptr<LangString>> string_store;
	LangString *insert_before = nullptr;
	LangString *insert_after = nullptr;
	bool override_mode = false;
	bool no_translate_mode = false;
	LangString *default_translation = nullptr;

	StringData(uint tabs);
	void FreeTranslation();
	LangString *Find(std::string_view s);
	uint32_t Version() const;
	uint CountInUse(uint tab) const;
};

/** Helper for reading strings. */
struct StringReader {
	StringData &data; ///< The data to fill during reading.
	std::string file; ///< The file we are reading.
	bool master;      ///< Are we reading the master file?
	bool translation; ///< Are we reading a translation, implies !master. However, the base translation will have this false.

	StringReader(StringData &data, std::string file, bool master, bool translation);
	virtual ~StringReader() = default;
	void HandleString(std::string_view str);

	/**
	 * Read a single line from the source of strings.
	 * @param buffer The buffer to read the data in to.
	 * @param last   The last element in the buffer.
	 * @return The buffer, or nullptr if at the end of the file.
	 */
	virtual char *ReadLine(char *buffer, const char *last) = 0;

	/**
	 * Handle the pragma of the file.
	 * @param str    The pragma string to parse.
	 */
	virtual void HandlePragma(std::string_view str, LanguagePackHeader &lang);

	/**
	 * Start parsing the file.
	 */
	virtual void ParseFile();

	void AssignIDs(size_t &next_id, LangString *ls);
};

/** Base class for writing the header, i.e. the STR_XXX to numeric value. */
struct HeaderWriter {
	/**
	 * Write the string ID.
	 * @param name     The name of the string.
	 * @param stringid The ID of the string.
	 */
	virtual void WriteStringID(const std::string &name, uint stringid) = 0;

	/**
	 * Finalise writing the file.
	 * @param data The data about the string.
	 */
	virtual void Finalise(const StringData &data) = 0;

	/** Especially destroy the subclasses. */
	virtual ~HeaderWriter() = default;

	void WriteHeader(const StringData &data);
};

/** Base class for all language writers. */
struct LanguageWriter {
	/**
	 * Write the header metadata. The multi-byte integers are already converted to
	 * the little endian format.
	 * @param header The header to write.
	 */
	virtual void WriteHeader(const LanguagePackHeader *header) = 0;

	/**
	 * Write a number of bytes.
	 * @param buffer The buffer to write.
	 */
	virtual void Write(std::string_view buffer) = 0;

	/**
	 * Finalise writing the file.
	 */
	virtual void Finalise() = 0;

	/** Especially destroy the subclasses. */
	virtual ~LanguageWriter() = default;

	virtual void WriteLength(size_t length);
	virtual void WriteLang(const StringData &data);
};

struct CmdStruct;

struct CmdPair {
	const CmdStruct *cmd;
	std::string param;

	auto operator<=>(const CmdPair &other) const = default;
};

struct ParsedCommandStruct {
	std::vector<CmdPair> non_consuming_commands;
	std::array<const CmdStruct*, 32> consuming_commands{ nullptr }; // ordered by param #
};

const CmdStruct *TranslateCmdForCompare(const CmdStruct *a);
ParsedCommandStruct ExtractCommandString(std::string_view s, bool warnings);

void StrgenWarningI(const std::string &msg);
void StrgenErrorI(const std::string &msg);
[[noreturn]] void StrgenFatalI(const std::string &msg);
#define StrgenWarning(format_string, ...) StrgenWarningI(fmt::format(FMT_STRING(format_string) __VA_OPT__(,) __VA_ARGS__))
#define StrgenError(format_string, ...) StrgenErrorI(fmt::format(FMT_STRING(format_string) __VA_OPT__(,) __VA_ARGS__))
#define StrgenFatal(format_string, ...) StrgenFatalI(fmt::format(FMT_STRING(format_string) __VA_OPT__(,) __VA_ARGS__))
std::optional<std::string_view> ParseWord(StringConsumer &consumer);

/** Global state shared between strgen.cpp, game_text.cpp and strgen_base.cpp */
struct StrgenState {
	std::string file = "(unknown file)"; ///< The filename of the input, so we can refer to it in errors/warnings
	uint cur_line = 0; ///< The current line we're parsing in the input file
	uint errors = 0;
	uint warnings = 0;
	bool show_warnings = false;
	bool annotate_todos = false;
	bool translation = false; ///< Is the current file actually a translation or not
	LanguagePackHeader lang; ///< Header information about a language.
};
extern StrgenState _strgen;

#endif /* STRGEN_H */
