/* ERRORS.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/cseries/errors.c. */

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "interface/terminal.h"

#include <stdarg.h>
#include <time.h>

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct error_suppression_globals
{
	long last_error_time;
	long error_count;
};

/* ---------- globals */

static boolean data_002dcd2c = TRUE;
static struct error_suppression_globals bss_0031df2c = { 0, 0 };
boolean find_all_fucked_up_shit = FALSE;
long fucked_up_shit_count = 0;

/* ---------- public code */

void errors_dispose(
	void)
{
	stack_walk_dispose();

	return;
}

void errors_output_to_debug_file(
	boolean output_to_debug_file)
{
	error_globals.output_to_debug_file = output_to_debug_file;

	return;
}

void errors_overflow_suppression_enable(
	boolean overflow_suppression)
{
	error_globals.overflow_suppression = overflow_suppression;

	return;
}

char *error_get(
	void)
{
	return error_globals.message_buffer;
}

/* Native builds keep the log open, flushing each line (reopening per line takes milliseconds on Windows; a host logs
thousands of lines as 100 machines join or leave). Opened once across threads; a line is one write: no interleaving. */
static void write_to_debug_file(
	char const *string,
	boolean date)
{
	static FILE *volatile debug_file = NULL;
	FILE *file = debug_file;
	char prefix[32];

	if (!file)
	{
		FILE *opened = fopen("d:\\debug.txt", "a+b");

		if (!opened)
		{
			return;
		}
		file = __sync_val_compare_and_swap(&debug_file, (FILE *)NULL, opened);
		if (file)
		{
			fclose(opened);
		}
		else
		{
			file = opened;
		}
	}

	prefix[0] = 0;
	if (date)
	{
		/* time_t, not long: the PS3's time_t is 64-bit and its game long 32-bit */
		time_t timeptr;
		struct tm *_time;

		time(&timeptr);
		_time = localtime(&timeptr);
		if (_time)
		{
			sprintf(
				prefix,
				"%02d.%02d.%02d %02d:%02d:%02d  ",
				_time->tm_mon + 1,
				_time->tm_mday,
				_time->tm_year % 100,
				_time->tm_hour,
				_time->tm_min,
				_time->tm_sec);
		}
		else
		{
			csstrcpy(prefix, "<TIME UNAVAILABLE>  ");
		}
	}
	fprintf(file, "%s%s", prefix, string);
	fflush(file);
}

void write_to_error_file(
	char *string,
	boolean date)
{
	char line[1024];
	long time_value;

	if (data_002dcd2c)
	{
		data_002dcd2c = FALSE;
		write_to_error_file("\r\n\r\n", FALSE);
		write_to_error_file("halobeta xbox 01.01.14.2342(CACHE) ----------------------------------------------\r\n", TRUE);
		sprintf(line, "reference function: %s\r\n", "_write_to_error_file");
		write_to_error_file(line, TRUE);
		sprintf(line, "reference address: %x\r\n", write_to_error_file);
		write_to_error_file(line, TRUE);
	}

	if (error_globals.output_to_debug_file)
	{
		write_to_debug_file(string, date);
	}

	return;
}

static void reset_error_state(
	void)
{
	error_globals.delayed = FALSE;
	error_globals.message_buffer_size = 0;

	return;
}

void errors_initialize(
	void)
{
	error_globals.output_to_debug_file = TRUE;
	error_globals.overflow_suppression = TRUE;
	reset_error_state();
	stack_walk_initialize();

	return;
}

void error(
	short priority,
	const char *format,
	...)
{
	char string[1024];
	char *newline;
	long new_size;

	match_assert(
		"c:\\halo\\SOURCE\\cseries\\errors.c",
		0x61,
		priority>=0 && priority<NUMBER_OF_ERROR_MESSAGE_PRIORITIES);

	if (error_globals.overflow_suppression && priority == _error_silent)
	{
		long time = system_milliseconds();

		if ((unsigned long)(time - bss_0031df2c.last_error_time) > 900)
		{
			bss_0031df2c.error_count = 0;
		}
		bss_0031df2c.last_error_time = time;
		if (bss_0031df2c.error_count == 10 && terminal_shows(_terminal_message_chatter))
		{
			terminal_printf(
				global_real_argb_white,
				"too many errors, only printing to debug.txt");
		}
		bss_0031df2c.error_count++;
		if (bss_0031df2c.error_count >= 10)
		{
			priority = _error_log;
		}
	}

	if (!error_globals.recursion_lock)
	{
		error_globals.recursion_lock = TRUE;
		if (priority == _error_delayed)
		{
			error_globals.delayed = TRUE;
		}

		if (format)
		{
			va_list argument_list;

			va_start(argument_list, format);
			vsprintf(string, format, argument_list);
			va_end(argument_list);
			csstrcat(string, "\r\n");

			/* (port: on screen as config.toml's game.console_log says: an assert that stops the game, and what a typed command
			logs (its answer), always; the rest, the game's chatter. debug.txt has it whatever that is) */
			if (priority != _error_log &&
				terminal_shows(terminal_command_running || !strncmp(string, "EXCEPTION halt", 14) ||
					!strncmp(string, "EXCEPTION assert", 16) ? _terminal_message_serious : _terminal_message_chatter))
			{
				terminal_printf(global_real_argb_white, "%s", string);
			}
			write_to_error_file(string, TRUE);

			new_size = csstrlen(string);
			if (error_globals.message_buffer_size+new_size >= ERROR_MESSAGE_BUFFER_MAXIMUM_SIZE)
			{
				long old_size;
				long copy_size;
				long prefix_size = csstrlen("[...too many errors to print...]\r\n");
				long offset = prefix_size+1024+new_size;

				if (offset < 0)
				{
					offset = 0;
				}
				else if (offset > error_globals.message_buffer_size-1)
				{
					offset = error_globals.message_buffer_size-1;
				}

				newline = strchr(error_globals.message_buffer+offset, '\n');
				if (!newline)
				{
					old_size = error_globals.message_buffer_size;
				}
				else
				{
					old_size = newline-error_globals.message_buffer+1;
				}
				copy_size = error_globals.message_buffer_size-old_size;
				match_assert(
					"c:\\halo\\SOURCE\\cseries\\errors.c",
					0xBF,
					prefix_size + copy_size + new_size < ERROR_MESSAGE_BUFFER_MAXIMUM_SIZE);
				csstrncpy(
					error_globals.message_buffer,
					"[...too many errors to print...]\r\n",
					prefix_size);
				if (copy_size > 0)
				{
					csmemmove(
						error_globals.message_buffer+prefix_size,
						newline,
						copy_size);
				}

				error_globals.message_buffer[prefix_size+copy_size] = 0;
				error_globals.message_buffer_size = (short)(prefix_size+copy_size);
			}

			if (error_globals.message_buffer_size+new_size < ERROR_MESSAGE_BUFFER_MAXIMUM_SIZE)
			{
				csstrcpy(
					error_globals.message_buffer+error_globals.message_buffer_size,
					string);
				error_globals.message_buffer_size += (short)new_size;
			}
		}

		if (priority == _error_immediate)
		{
			system_exit(-4998);
		}
		error_globals.recursion_lock = FALSE;
	}

	return;
}

boolean errors_handle(
	void)
{
	boolean delayed = error_globals.delayed;

	reset_error_state();

	return delayed;
}

void errors_clear(
	void)
{
	reset_error_state();

	return;
}

/* ---------- private code */
