/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strgen.h Structures related to strgen. */

#ifndef STRGEN_H
#define STRGEN_H

#include "../language.h"

#include <memory>
#include <string>
#include <vector>

#include <unordered_map>
#include <array>

/** Container for the different cases of a string. */
struct Case {
	int caseidx;        ///< The index of the case.
	std::string string; ///< The translation of the case.

	Case(int caseidx, std::string string);
};

/** Information about a single string. */
struct LangString {
	std::string name;       ///< Name of the string.
	std::string english;    ///< English text.
	std::string translated; ///< Translated text.
	int index;              ///< The index in the language file.
	int line;               ///< Line of string in source-file.
	std::vector<Case> translated_cases; ///< Cases of the translation.
	std::unique_ptr<LangString> chain_before;
	std::unique_ptr<LangString> chain_after;
	bool no_translate_mode = false;
	LangString *default_translation = nullptr;

	LangString(std::string name, std::string english, size_t index, int line);
	void ReplaceDefinition(std::string english, int line);
	void FreeTranslation();
};

/** Information about the currently known strings. */
struct StringData {
	std::vector<LangString *> strings; ///< List of all known strings.
	std::unordered_map<std::string_view, LangString *> name_to_string; ///< Lookup table for the strings.
	size_t tabs;          ///< The number of 'tabs' of strings.
	size_t max_strings;   ///< The maximum number of strings.
	int next_string_id;   ///< The next string ID to allocate.

	std::vector<std::unique_ptr<LangString>> string_store;
	LangString *insert_before = nullptr;
	LangString *insert_after = nullptr;
	bool override_mode = false;
	bool no_translate_mode = false;
	LangString *default_translation = nullptr;

	StringData(size_t tabs);
	void FreeTranslation();
	LangString *Find(const std::string_view s);
	uint VersionHashStr(uint hash, const char *s) const;
	uint Version() const;
	uint CountInUse(uint tab) const;
};

/** Helper for reading strings. */
struct StringReader {
	StringData &data; ///< The data to fill during reading.
	std::string file; ///< The file we are reading.
	bool master;      ///< Are we reading the master file?
	bool translation; ///< Are we reading a translation, implies !master. However, the base translation will have this false.

	StringReader(StringData &data, std::string file, bool master, bool translation);
	virtual ~StringReader() {}
	void HandleString(char *str);

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
	virtual void HandlePragma(char *str);

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
	virtual void WriteStringID(const char *name, int stringid) = 0;

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
	 * @param length The amount of byte to write.
	 */
	virtual void Write(const byte *buffer, size_t length) = 0;

	/**
	 * Finalise writing the file.
	 */
	virtual void Finalise() = 0;

	/** Especially destroy the subclasses. */
	virtual ~LanguageWriter() = default;

	virtual void WriteLength(uint length);
	virtual void WriteLang(const StringData &data);
};

struct CmdStruct;

struct CmdPair {
	const CmdStruct *cmd;
	std::string param;
};

struct ParsedCommandStruct {
	std::vector<CmdPair> non_consuming_commands;
	std::array<const CmdStruct*, 32> consuming_commands{ nullptr }; // ordered by param #
};

const CmdStruct *TranslateCmdForCompare(const CmdStruct *a);
ParsedCommandStruct ExtractCommandString(const char *s, bool warnings);

void CDECL strgen_warning(const char *s, ...) WARN_FORMAT(1, 2);
void CDECL strgen_error(const char *s, ...) WARN_FORMAT(1, 2);
void NORETURN CDECL strgen_fatal(const char *s, ...) WARN_FORMAT(1, 2);
char *ParseWord(char **buf);

extern const char *_file;
extern int _cur_line;
extern int _errors, _warnings, _show_todo;
extern LanguagePackHeader _lang;

#endif /* STRGEN_H */
