/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file console_gui.cpp Handling the GUI of the in-game console. */

#include "stdafx.h"
#include "textbuf_type.h"
#include "window_gui.h"
#include "console_gui.h"
#include "console_internal.h"
#include "guitimer_func.h"
#include "window_func.h"
#include "string_func.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "gfx_layout.h"
#include "settings_type.h"
#include "console_func.h"
#include "rev.h"
#include "video/video_driver.hpp"
#include "core/ring_buffer.hpp"
#include <string>

#include "widgets/console_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static const uint ICON_HISTORY_SIZE       = 20;
static const uint ICON_RIGHT_BORDERWIDTH  = 10;
static const uint ICON_BOTTOM_BORDERWIDTH = 12;

/**
 * Container for a single line of console output
 */
struct IConsoleLine {
	std::string buffer;     ///< The data to store.
	TextColour colour;      ///< The colour of the line.
	uint16_t time;          ///< The amount of time the line is in the backlog.

	IConsoleLine() : buffer(), colour(TC_BEGIN), time(0)
	{

	}

	/**
	 * Initialize the console line.
	 * @param buffer the data to print.
	 * @param colour the colour of the line.
	 */
	IConsoleLine(std::string buffer, TextColour colour) :
			buffer(std::move(buffer)),
			colour(colour),
			time(0)
	{
	}

	~IConsoleLine()
	{
	}
};

/** The console backlog buffer. Item index 0 is the newest line. */
static ring_buffer<IConsoleLine> _iconsole_buffer;

static bool TruncateBuffer();


/* ** main console cmd buffer ** */
static Textbuf _iconsole_cmdline(ICON_CMDLN_SIZE);
static ring_buffer<std::string> _iconsole_history;
static ptrdiff_t _iconsole_historypos;
IConsoleModes _iconsole_mode;

/* *************** *
 *  end of header  *
 * *************** */

static void IConsoleClearCommand()
{
	memset(_iconsole_cmdline.buf, 0, ICON_CMDLN_SIZE);
	_iconsole_cmdline.chars = _iconsole_cmdline.bytes = 1; // only terminating zero
	_iconsole_cmdline.pixels = 0;
	_iconsole_cmdline.caretpos = 0;
	_iconsole_cmdline.caretxoffs = 0;
	SetWindowDirty(WC_CONSOLE, 0);
}

static inline void IConsoleResetHistoryPos()
{
	_iconsole_historypos = -1;
}


static const char *IConsoleHistoryAdd(const char *cmd);
static void IConsoleHistoryNavigate(int direction);
static void IConsoleTabCompletion();

static constexpr NWidgetPart _nested_console_window_widgets[] = {
	NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_BACKGROUND), SetResize(1, 1),
};

static WindowDesc _console_window_desc(__FILE__, __LINE__,
	WDP_MANUAL, nullptr, 0, 0,
	WC_CONSOLE, WC_NONE,
	0,
	_nested_console_window_widgets
);

struct IConsoleWindow : Window
{
	static size_t scroll;
	int line_height;   ///< Height of one line of text in the console.
	int line_offset;
	int cursor_width;
	GUITimer truncate_timer;

	IConsoleWindow() : Window(_console_window_desc)
	{
		_iconsole_mode = ICONSOLE_OPENED;

		this->InitNested(0);
		this->truncate_timer.SetInterval(3000);
		ResizeWindow(this, _screen.width, _screen.height / 3);
	}

	void OnInit() override
	{
		this->line_height = GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.hsep_normal;
		this->line_offset = GetStringBoundingBox("] ").width + WidgetDimensions::scaled.frametext.left;
		this->cursor_width = GetCharacterWidth(FS_NORMAL, '_');
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		_iconsole_mode = ICONSOLE_CLOSED;
		VideoDriver::GetInstance()->EditBoxLostFocus();
		this->Window::Close();
	}

	/**
	 * Scroll the content of the console.
	 * @param amount Number of lines to scroll back.
	 */
	void Scroll(int amount)
	{
		if (amount < 0) {
			size_t namount = static_cast<size_t>(-amount);
			IConsoleWindow::scroll = (namount > IConsoleWindow::scroll) ? 0 : IConsoleWindow::scroll - namount;
		} else {
			assert(this->height >= 0 && this->line_height > 0);
			size_t visible_lines = static_cast<size_t>(this->height / this->line_height);
			size_t max_scroll = (visible_lines > _iconsole_buffer.size()) ? 0 : _iconsole_buffer.size() + 1 - visible_lines;
			IConsoleWindow::scroll = std::min<size_t>(IConsoleWindow::scroll + amount, max_scroll);
		}
		this->SetDirty();
	}

	void OnPaint() override
	{
		const int right = this->width - WidgetDimensions::scaled.frametext.right;

		GfxFillRect(0, 0, this->width - 1, this->height - 1, PC_BLACK);
		int ypos = this->height - this->line_height - WidgetDimensions::scaled.hsep_normal;
		for (size_t line_index = IConsoleWindow::scroll; line_index < _iconsole_buffer.size(); line_index++) {
			const IConsoleLine &print = _iconsole_buffer[line_index];
			SetDParamStr(0, print.buffer);
			ypos = DrawStringMultiLine(WidgetDimensions::scaled.frametext.left, right, -this->line_height, ypos, STR_JUST_RAW_STRING, print.colour, SA_LEFT | SA_BOTTOM | SA_FORCE) - WidgetDimensions::scaled.hsep_normal;
			if (ypos < 0) break;
		}
		/* If the text is longer than the window, don't show the starting ']' */
		int delta = this->width - WidgetDimensions::scaled.frametext.right - cursor_width - this->line_offset - _iconsole_cmdline.pixels - ICON_RIGHT_BORDERWIDTH;
		if (delta > 0) {
			DrawString(WidgetDimensions::scaled.frametext.left, right, this->height - this->line_height, "]", (TextColour)CC_COMMAND, SA_LEFT | SA_FORCE);
			delta = 0;
		}

		/* If we have a marked area, draw a background highlight. */
		if (_iconsole_cmdline.marklength != 0) GfxFillRect(this->line_offset + delta + _iconsole_cmdline.markxoffs, this->height - this->line_height, this->line_offset + delta + _iconsole_cmdline.markxoffs + _iconsole_cmdline.marklength, this->height - 1, PC_DARK_RED);

		DrawString(this->line_offset + delta, right, this->height - this->line_height, _iconsole_cmdline.buf, (TextColour)CC_COMMAND, SA_LEFT | SA_FORCE);

		if (_focused_window == this && _iconsole_cmdline.caret) {
			DrawString(this->line_offset + delta + _iconsole_cmdline.caretxoffs, right, this->height - this->line_height, "_", TC_WHITE, SA_LEFT | SA_FORCE);
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (this->truncate_timer.CountElapsed(delta_ms) == 0) return;

		assert(this->height >= 0 && this->line_height > 0);
		size_t visible_lines = static_cast<size_t>(this->height / this->line_height);

		if (TruncateBuffer() && IConsoleWindow::scroll + visible_lines > _iconsole_buffer.size()) {
			size_t max_scroll = (visible_lines > _iconsole_buffer.size()) ? 0 : _iconsole_buffer.size() + 1 - visible_lines;
			IConsoleWindow::scroll = std::min<size_t>(IConsoleWindow::scroll, max_scroll);
			this->SetDirty();
		}
	}

	void OnMouseLoop() override
	{
		if (_iconsole_cmdline.HandleCaret()) this->SetDirty();
	}

	EventState OnKeyPress(char32_t key, uint16_t keycode) override
	{
		if (_focused_window != this) return ES_NOT_HANDLED;

		const int scroll_height = (this->height / this->line_height) - 1;
		switch (keycode) {
			case WKC_UP:
				IConsoleHistoryNavigate(1);
				this->SetDirty();
				break;

			case WKC_DOWN:
				IConsoleHistoryNavigate(-1);
				this->SetDirty();
				break;

			case WKC_SHIFT | WKC_PAGEDOWN:
				this->Scroll(-scroll_height);
				break;

			case WKC_SHIFT | WKC_PAGEUP:
				this->Scroll(scroll_height);
				break;

			case WKC_SHIFT | WKC_DOWN:
				this->Scroll(-1);
				break;

			case WKC_SHIFT | WKC_UP:
				this->Scroll(1);
				break;

			case WKC_BACKQUOTE:
				IConsoleSwitch();
				break;

			case WKC_RETURN: case WKC_NUM_ENTER: {
				/* We always want the ] at the left side; we always force these strings to be left
				 * aligned anyway. So enforce this in all cases by adding a left-to-right marker,
				 * otherwise it will be drawn at the wrong side with right-to-left texts. */
				IConsolePrintF(CC_COMMAND, LRM "] %s", _iconsole_cmdline.buf);
				const char *cmd = IConsoleHistoryAdd(_iconsole_cmdline.buf);
				IConsoleClearCommand();

				if (cmd != nullptr) IConsoleCmdExec(cmd);
				break;
			}

			case WKC_CTRL | WKC_RETURN:
				_iconsole_mode = (_iconsole_mode == ICONSOLE_FULL) ? ICONSOLE_OPENED : ICONSOLE_FULL;
				IConsoleResize(this);
				MarkWholeScreenDirty();
				break;

			case (WKC_CTRL | 'L'):
				IConsoleCmdExec("clear");
				break;

			case WKC_TAB:
				IConsoleTabCompletion();
				break;

			default:
				if (_iconsole_cmdline.HandleKeyPress(key, keycode) != HKPR_NOT_HANDLED) {
					IConsoleWindow::scroll = 0;
					IConsoleResetHistoryPos();
					this->SetDirty();
				} else {
					return ES_NOT_HANDLED;
				}
				break;
		}
		return ES_HANDLED;
	}

	void InsertTextString(WidgetID, const char *str, bool marked, const char *caret, const char *insert_location, const char *replacement_end) override
	{
		if (_iconsole_cmdline.InsertString(str, marked, caret, insert_location, replacement_end)) {
			IConsoleWindow::scroll = 0;
			IConsoleResetHistoryPos();
			this->SetDirty();
		}
	}

	Textbuf *GetFocusedTextbuf() const override
	{
		return &_iconsole_cmdline;
	}

	Point GetCaretPosition() const override
	{
		int delta = std::min<int>(this->width - this->line_offset - _iconsole_cmdline.pixels - ICON_RIGHT_BORDERWIDTH, 0);
		Point pt = {this->line_offset + delta + _iconsole_cmdline.caretxoffs, this->height - this->line_height};

		return pt;
	}

	Rect GetTextBoundingRect(const char *from, const char *to) const override
	{
		int delta = std::min<int>(this->width - this->line_offset - _iconsole_cmdline.pixels - ICON_RIGHT_BORDERWIDTH, 0);

		const auto p1 = GetCharPosInString(_iconsole_cmdline.buf, from, FS_NORMAL);
		const auto p2 = from != to ? GetCharPosInString(_iconsole_cmdline.buf, to, FS_NORMAL) : p1;

		Rect r = {this->line_offset + delta + p1.left, this->height - this->line_height, this->line_offset + delta + p2.right, this->height};
		return r;
	}

	ptrdiff_t GetTextCharacterAtPosition(const Point &pt) const override
	{
		int delta = std::min<int>(this->width - this->line_offset - _iconsole_cmdline.pixels - ICON_RIGHT_BORDERWIDTH, 0);

		if (!IsInsideMM(pt.y, this->height - this->line_height, this->height)) return -1;

		return GetCharAtPosition(_iconsole_cmdline.buf, pt.x - delta);
	}

	void OnMouseWheel(int wheel) override
	{
		this->Scroll(-wheel);
	}

	void OnFocus(Window *previously_focused_window) override
	{
		VideoDriver::GetInstance()->EditBoxGainedFocus();
	}

	void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		VideoDriver::GetInstance()->EditBoxLostFocus();
	}
};

size_t IConsoleWindow::scroll = 0;

void IConsoleGUIInit()
{
	IConsoleResetHistoryPos();
	_iconsole_mode = ICONSOLE_CLOSED;

	IConsoleClearBuffer();

	IConsolePrintF(CC_WARNING, "OpenTTD Game Console Revision 7 - %s", _openttd_revision);
	IConsolePrint(CC_WHITE,  "------------------------------------");
	IConsolePrint(CC_WHITE,  "use \"help\" for more information");
	IConsolePrint(CC_WHITE,  "");
	IConsoleClearCommand();
}

void IConsoleClearBuffer()
{
	_iconsole_buffer.clear();
}

void IConsoleGUIFree()
{
	IConsoleClearBuffer();
}

/** Change the size of the in-game console window after the screen size changed, or the window state changed. */
void IConsoleResize(Window *w)
{
	switch (_iconsole_mode) {
		case ICONSOLE_OPENED:
			w->height = _screen.height / 3;
			w->width = _screen.width;
			break;
		case ICONSOLE_FULL:
			w->height = _screen.height - ICON_BOTTOM_BORDERWIDTH;
			w->width = _screen.width;
			break;
		default: return;
	}

	MarkWholeScreenDirty();
}

/** Toggle in-game console between opened and closed. */
void IConsoleSwitch()
{
	switch (_iconsole_mode) {
		case ICONSOLE_CLOSED:
			new IConsoleWindow();
			break;

		case ICONSOLE_OPENED: case ICONSOLE_FULL:
			CloseWindowById(WC_CONSOLE, 0);
			break;
	}

	MarkWholeScreenDirty();
}

/** Close the in-game console. */
void IConsoleClose()
{
	if (_iconsole_mode == ICONSOLE_OPENED) IConsoleSwitch();
}

/**
 * Add the entered line into the history so you can look it back
 * scroll, etc. Put it to the beginning as it is the latest text
 * @param cmd Text to be entered into the 'history'
 * @return the command to execute
 */
static const char *IConsoleHistoryAdd(const char *cmd)
{
	/* Strip all spaces at the begin */
	while (IsWhitespace(*cmd)) cmd++;

	/* Do not put empty command in history */
	if (StrEmpty(cmd)) return nullptr;

	/* Do not put in history if command is same as previous */
	if (_iconsole_history.empty() || _iconsole_history.front() != cmd) {
		_iconsole_history.emplace_front(cmd);
		while (_iconsole_history.size() > ICON_HISTORY_SIZE) _iconsole_history.pop_back();
	}

	/* Reset the history position */
	IConsoleResetHistoryPos();
	return _iconsole_history.front().c_str();
}

/**
 * Navigate Up/Down in the history of typed commands
 * @param direction Go further back in history (+1), go to recently typed commands (-1)
 */
static void IConsoleHistoryNavigate(int direction)
{
	if (_iconsole_history.empty()) return; // Empty history
	_iconsole_historypos = Clamp<ptrdiff_t>(_iconsole_historypos + direction, -1, _iconsole_history.size() - 1);

	if (_iconsole_historypos == -1) {
		_iconsole_cmdline.DeleteAll();
	} else {
		_iconsole_cmdline.Assign(_iconsole_history[_iconsole_historypos]);
	}
}

static void IConsoleTabCompletion()
{
	const char *input = _iconsole_cmdline.buf;

	/* Strip all spaces at the beginning */
	while (IsWhitespace(*input)) input++;

	/* Don't do tab completion for no input */
	if (StrEmpty(input)) return;

	const char *cmdptr = input;
	for (; *cmdptr != '\0'; cmdptr++) {
		switch (*cmdptr) {
		case ' ':
		case '"':
		case '\\':
			// Give up
			return;
		}
	}

	struct match_state {
		std::string prefix;
		std::string candidate_str;
		std::string common_prefix;
		uint matches = 0;
	};
	match_state match_input;
	match_state match_input_no_underscores;

	match_input.prefix = std::string(input, cmdptr - input);
	if (match_input.prefix.empty()) return;

	extern std::string RemoveUnderscores(std::string name);
	match_input_no_underscores.prefix = RemoveUnderscores(match_input.prefix);
	if (match_input_no_underscores.prefix.empty()) return;

	auto check_candidate = [&](const char *cmd_name, match_state &state) {
		if (strncmp(cmd_name, state.prefix.c_str(), state.prefix.size()) != 0) return;

		if (state.matches == 0) {
			state.common_prefix = cmd_name;
		} else {
			const char *cp = state.common_prefix.c_str();
			const char *cmdp = cmd_name;
			while (true) {
				const char *end = cmdp;
				char32_t a = Utf8Consume(cp);
				char32_t b = Utf8Consume(cmdp);
				if (a == 0 || b == 0 || a != b) {
					state.common_prefix.resize(end - cmd_name);
					break;
				}
			}
		}
		state.matches++;
		if (!state.candidate_str.empty()) state.candidate_str += ' ';
		state.candidate_str += cmd_name;
	};
	for (auto &it : IConsole::Commands()) {
		const IConsoleCmd *cmd = &it.second;
		if ((_settings_client.gui.console_show_unlisted || !cmd->unlisted) && (cmd->hook == nullptr || cmd->hook(false) != CHR_HIDE)) {
			check_candidate(it.first.c_str(), match_input_no_underscores);
			check_candidate(cmd->name.c_str(), match_input);
		}
	}
	for (auto &it : IConsole::Aliases()) {
		check_candidate(it.first.c_str(), match_input_no_underscores);
		check_candidate(it.second.name.c_str(), match_input);
	}
	match_state &best = match_input_no_underscores.matches > match_input.matches ? match_input_no_underscores : match_input;
	if (best.matches > 0) {
		_iconsole_cmdline.Assign(best.common_prefix.c_str());
		if (best.matches > 1) {
			IConsolePrint(CC_WHITE, best.candidate_str.c_str());
		}
	}
}

/**
 * Handle the printing of text entered into the console or redirected there
 * by any other means. Text can be redirected to other clients in a network game
 * as well as to a logfile. If the network server is a dedicated server, all activities
 * are also logged. All lines to print are added to a temporary buffer which can be
 * used as a history to print them onscreen
 * @param colour_code the colour of the command. Red in case of errors, etc.
 * @param str the message entered or output on the console (notice, error, etc.)
 */
void IConsoleGUIPrint(TextColour colour_code, std::string str)
{
	_iconsole_buffer.push_front(IConsoleLine(std::move(str), colour_code));
	SetWindowDirty(WC_CONSOLE, 0);
}

/**
 * Remove old lines from the backlog buffer.
 * The buffer is limited by a maximum size and a minimum age. Every time truncation runs,
 * all lines in the buffer are aged by one. When a line exceeds both the maximum position
 * and also the maximum age, it gets removed.
 * @return true if any lines were removed
 */
static bool TruncateBuffer()
{
	bool need_truncation = false;
	size_t count = 0;
	for (IConsoleLine &line : _iconsole_buffer) {
		count++;
		line.time++;
		if (line.time > _settings_client.gui.console_backlog_timeout && count > _settings_client.gui.console_backlog_length) {
			/* Any messages after this are older and need to be truncated */
			need_truncation = true;
			break;
		}
	}

	if (need_truncation) {
		_iconsole_buffer.resize(count - 1);
	}

	return need_truncation;
}


/**
 * Check whether the given TextColour is valid for console usage.
 * @param c The text colour to compare to.
 * @return true iff the TextColour is valid for console usage.
 */
bool IsValidConsoleColour(TextColour c)
{
	/* A normal text colour is used. */
	if (!(c & TC_IS_PALETTE_COLOUR)) return TC_BEGIN <= c && c < TC_END;

	/* A text colour from the palette is used; must be the company
	 * colour gradient, so it must be one of those. */
	c &= ~TC_IS_PALETTE_COLOUR;
	for (Colours i = COLOUR_BEGIN; i < COLOUR_END; i++) {
		if (GetColourGradient(i, SHADE_NORMAL) == c) return true;
	}

	return false;
}
