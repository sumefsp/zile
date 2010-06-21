/* Minibuffer facility functions

   Copyright (c) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2008, 2009, 2010 Free Software Foundation, Inc.

   This file is part of GNU Zile.

   GNU Zile is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GNU Zile is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU Zile; see the file COPYING.  If not, write to the
   Free Software Foundation, Fifth Floor, 51 Franklin Street, Boston,
   MA 02111-1301, USA.  */

#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "extern.h"

static int files_history = LUA_REFNIL;

/*--------------------------------------------------------------------------
 * Minibuffer wrapper functions.
 *--------------------------------------------------------------------------*/

void
init_minibuf (void)
{
  CLUE_DO (L, "hp = history_new ()");
  lua_getglobal (L, "hp");
  files_history = luaL_ref (L, LUA_REGISTRYINDEX);
}

int
minibuf_no_error (void)
{
  const char *s;
  CLUE_GET (L, minibuf_contents, string, s);
  return s == NULL;
}

static void
minibuf_vwrite (const char *fmt, va_list ap)
{
  char *s;
  xvasprintf (&s, fmt, ap);
  CLUE_SET (L, minibuf_contents, string, s);
  CLUE_DO (L, "minibuf_refresh ()");
}

/*
 * Write the specified string in the minibuffer.
 */
void
minibuf_write (const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  minibuf_vwrite (fmt, ap);
  va_end (ap);
}

/*
 * Write the specified error string in the minibuffer and signal an error.
 */
void
minibuf_error (const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  minibuf_vwrite (fmt, ap);
  va_end (ap);

  CLUE_DO (L, "ding ()");
}

/*
 * Read a string from the minibuffer.
 */
char *
minibuf_read (const char *fmt, const char *value)
{
  return term_minibuf_read (fmt, value ? value : "", -1, LUA_REFNIL, LUA_REFNIL);
}

/*
 * Read a non-negative number from the minibuffer.
 */
unsigned long
minibuf_read_number (const char *fmt)
{
  unsigned long n;

  do
    {
      char *ms = minibuf_read (fmt, "");
      if (ms == NULL)
        {
          n = SIZE_MAX;
          FUNCALL (keyboard_quit);
          break;
        }
      if (strlen (ms) == 0)
        n = ULONG_MAX - 1;
      else
        n = strtoul (ms, NULL, 10);
      free (ms);
      if (n == ULONG_MAX)
        minibuf_write ("Please enter a number.");
    }
  while (n == ULONG_MAX);

  return n;
}

/*
 * Read a filename from the minibuffer.
 */
char *
minibuf_read_filename (const char *fmt, const char *value,
                       const char *file)
{
  char *p = NULL;
  astr as;
  size_t pos;

  as = astr_new_cstr (value);
  if (expand_path (as))
    {
      as = compact_path (as);

      CLUE_DO (L, "cp = completion_new ()");
      CLUE_DO (L, "cp.filename = true");
      lua_getglobal (L, "cp");

      pos = astr_len (as);
      if (file)
        pos -= strlen (file);
      p = term_minibuf_read (fmt, astr_cstr (as), pos, luaL_ref (L, LUA_REGISTRYINDEX), files_history);

      if (p != NULL)
        {
          astr as = astr_new_cstr (p);
          if (expand_path (as))
            {
              lua_rawgeti (L, LUA_REGISTRYINDEX, files_history);
              lua_setglobal (L, "hp");
              CLUE_SET (L, s, string, p);
              CLUE_DO (L, "add_history_element (hp, s)");
              free (p);
              p = xstrdup (astr_cstr (as));
            }
          else
            p = NULL;
          astr_delete (as);
        }
    }
  astr_delete (as);

  return p;
}

int
minibuf_read_yn (const char *fmt)
{
  char *errmsg = "";

  for (;;) {
    size_t key;

    minibuf_write ("%s%s", errmsg, fmt);
    key = getkey ();
    switch (key)
      {
      case 'y':
        return true;
      case 'n':
        return false;
      case KBD_CTRL | 'g':
        return -1;
      default:
        errmsg = "Please answer y or n.  ";
      }
  }
}

int
minibuf_read_yesno (const char *fmt)
{
  char *ms;
  const char *errmsg = "Please answer yes or no.";
  int ret = -1;

  CLUE_DO (L, "cp = completion_new ()");
  CLUE_DO (L, "cp.completions = {'no', 'yes'}");
  lua_getglobal (L, "cp");

  ms = minibuf_vread_completion (fmt, "", luaL_ref (L, LUA_REGISTRYINDEX), LUA_REFNIL, errmsg, errmsg);

  if (ms != NULL)
    ret = !strcmp (ms, "yes");

  return ret;
}

/* FIXME: Make all callers use history */
char *
minibuf_read_completion (const char *fmt, char *value, int cp, int hp)
{
  return term_minibuf_read (fmt, value, -1, cp, hp);
}

/*
 * Read a string from the minibuffer using a completion.
 */
char *
minibuf_vread_completion (const char *fmt, char *value, int cp,
                          int hp, const char *empty_err,
                          const char *invalid_err)
{
  char *ms;

  for (;;)
    {
      ms = term_minibuf_read (fmt, value, -1, cp, hp);

      if (ms == NULL) /* Cancelled. */
        {
          FUNCALL (keyboard_quit);
          break;
        }
      else if (ms[0] == '\0')
        {
          minibuf_error (empty_err);
          free ((char *) ms);
          ms = NULL;
          break;
        }
      else
        {
          int comp;
          bool b;
          /* Complete partial words if possible. */
          lua_rawgeti (L, LUA_REGISTRYINDEX, cp);
          lua_setglobal (L, "cp");
          CLUE_SET (L, search, string, ms);
          CLUE_DO (L, "ret = completion_try (cp, search)");
          CLUE_GET (L, ret, integer, comp);
          if (comp == COMPLETION_MATCHED)
            {
              free ((char *) ms);
              ms = xstrdup (get_completion_match (cp));
            }
          else if (comp == COMPLETION_NONUNIQUE)
            {
              lua_rawgeti (L, LUA_REGISTRYINDEX, cp);
              lua_setglobal (L, "cp");
              CLUE_DO (L, "popup_completion (cp)");
            }

          CLUE_SET (L, ms, string, ms);
          lua_rawgeti (L, LUA_REGISTRYINDEX, cp);
          lua_setglobal (L, "cp");
          CLUE_DO (L, "b = minibuf_test_in_completions (ms, cp)");
          CLUE_GET (L, b, boolean, b);
          if (b)
            {
              if (hp != LUA_REFNIL)
                {
                  lua_rawgeti (L, LUA_REGISTRYINDEX, hp);
                  lua_setglobal (L, "hp");
                  CLUE_SET (L, s, string, ms);
                  CLUE_DO (L, "add_history_element (hp, s)");
                }
              minibuf_clear ();
              break;
            }
          else
            {
              minibuf_error (invalid_err, ms);
              CLUE_DO (L, "waitkey (WAITKEY_DEFAULT)");
            }
        }
    }

  return ms;
}

/*
 * Clear the minibuffer.
 */
void
minibuf_clear (void)
{
  CLUE_DO (L, "term_minibuf_write ('')");
}
