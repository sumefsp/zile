/* Lisp parser

   Copyright (c) 2001, 2005, 2008, 2009 Free Software Foundation, Inc.

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "extern.h"
#include "clue.h"

/*
 * Zile Lisp functions.
 */

le leNIL, leT;

struct fentry
{
  const char *name;		/* The function name. */
  Function func;		/* The function pointer. */
  bool interactive;             /* Whether function can be used interactively. */
  const char *doc;		/* Documentation string. */
};
typedef struct fentry fentry;

static fentry fentry_table[] = {
#define X(zile_name, c_name, interactive, doc)   \
  {zile_name, F_ ## c_name, interactive, doc},
#include "tbl_funcs.h"
#undef X
};

#define fentry_table_size (sizeof (fentry_table) / sizeof (fentry_table[0]))

static fentry *
get_fentry (const char *name)
{
  size_t i;
  assert (name);
  for (i = 0; i < fentry_table_size; ++i)
    if (!strcmp (name, fentry_table[i].name))
      return &fentry_table[i];
  return NULL;
}

Function
get_function (const char *name)
{
  fentry * f = get_fentry (name);
  return f ? f->func : NULL;
}

const char *
get_function_doc (const char *name)
{
  fentry * f = get_fentry (name);
  return f ? f->doc : NULL;
}

const char *
get_function_name (Function p)
{
  size_t i;
  for (i = 0; i < fentry_table_size; ++i)
    if (fentry_table[i].func == p)
      return fentry_table[i].name;
  return NULL;
}

le
execute_with_uniarg (bool undo, int uniarg, int (*forward) (void), int (*backward) (void))
{
  int uni, ret = true;
  int (*func) (void) = forward;

  if (backward && uniarg < 0)
    {
      func = backward;
      uniarg = -uniarg;
    }
  if (undo)
    undo_save (UNDO_START_SEQUENCE, get_buffer_pt (cur_bp), 0, 0);
  for (uni = 0; ret && uni < uniarg; ++uni)
    ret = func ();
  if (undo)
    undo_save (UNDO_END_SEQUENCE, get_buffer_pt (cur_bp), 0, 0);

  return bool_to_lisp (ret);
}

le
execute_function (const char *name, int uniarg)
{
  Function func = get_function (name);
  Macro *mp;

  if (func)
    return func (uniarg, LUA_REFNIL);
  else
    {
      mp = get_macro (name);
      if (mp)
        {
          call_macro (mp);
          return leT;
        }
      return leNIL;
    }
}

DEFUN ("execute-extended-command", execute_extended_command)
/*+
Read function name, then read its arguments and call it.
+*/
{
  const char *name;
  astr msg = astr_new ();

  if (lastflag & FLAG_SET_UNIARG)
    {
      if (lastflag & FLAG_UNIARG_EMPTY)
        astr_afmt (msg, "C-u ");
      else
        astr_afmt (msg, "%d ", uniarg);
    }
  astr_cat_cstr (msg, "M-x ");

  name = minibuf_read_function_name (astr_cstr (msg));
  astr_delete (msg);
  if (name == NULL)
    return false;

  ok = execute_function (name, uniarg);
  free ((char *) name);
}
END_DEFUN

/*
 * Read a function name from the minibuffer.
 */
static History *functions_history = NULL;
const char *
minibuf_read_function_name (const char *fmt, ...)
{
  va_list ap;
  char *ms;
  Completion *cp = completion_new (false);
  size_t i;

  for (i = 0; i < fentry_table_size; ++i)
    if (fentry_table[i].interactive)
      gl_sortedlist_add (get_completion_completions (cp), completion_strcmp,
                         xstrdup (fentry_table[i].name));
  add_macros_to_list (get_completion_completions (cp), completion_strcmp);

  va_start (ap, fmt);
  ms = minibuf_vread_completion (fmt, "", cp, functions_history,
                                 "No function name given",
                                 minibuf_test_in_completions,
                                 "Undefined function name `%s'", ap);
  va_end (ap);
  free_completion (cp);

  return ms;
}

static size_t
countNodes (le branch)
{
  int count;

  for (count = 0;
       !LUA_NIL (branch);
       branch = get_lists_next (branch), count++)
    ;
  return count;
}

static int
call_zile_command (lua_State *L)
{
  le trybranch;
  const char *keyword;
  fentry * func;
  assert (lua_isstring (L, -2));
  assert (lua_istable (L, -1));
  keyword = lua_tostring (L, -2);
  trybranch = luaL_ref (L, LUA_REGISTRYINDEX);
  func = get_fentry (keyword);
  if (func)
    lua_pushvalue (L, (func->func) (1, trybranch));
  else
    lua_pushnil (L);
  luaL_unref (L, LUA_REGISTRYINDEX, trybranch);
  return 1;
}

static le
leNew (const char *text)
{
  le new;
  lua_newtable (L);
  new = luaL_ref (L, LUA_REGISTRYINDEX);

  if (text)
    {
      lua_rawgeti (L, LUA_REGISTRYINDEX, new);
      lua_pushstring (L, xstrdup (text));
      lua_setfield (L, -2, "data");
      lua_pop (L, 1);
    }

  return new;
}

void
init_lisp (void)
{
  leNIL = leNew ("nil");
  leT = leNew ("t");
}

void
lisp_loadstring (astr as)
{
  CLUE_SET (L, s, string, astr_cstr (as));
  (void) CLUE_DO (L, "leEval (lisp_read (s))");
}

bool
lisp_loadfile (const char *file)
{
  FILE *fp = fopen (file, "r");

  if (fp != NULL)
    {
      astr bs = astr_fread (fp);
      lisp_loadstring (bs);
      astr_delete (bs);
      fclose (fp);
      return true;
    }

    return false;
}

DEFUN ("load", load)
/*+
Execute a file of Lisp code named FILE.
+*/
{
  if (!LUA_NIL (arglist) && countNodes (arglist) >= 2)
    ok = bool_to_lisp (lisp_loadfile (get_lists_data (get_lists_next (arglist))));
  else
    ok = leNIL;
}
END_DEFUN

DEFUN_NONINTERACTIVE ("setq", setq)
/*+
(setq [sym val]...)

Set each sym to the value of its val.
The symbols sym are variables; they are literal (not evaluated).
The values val are expressions; they are evaluated.
+*/
{
  le newvalue = leNIL, current;
  size_t argc = countNodes (arglist);

  if (!LUA_NIL (arglist) && argc >= 2)
    {
      for (current = get_lists_next (arglist);
           !LUA_NIL (current);
           current = get_lists_next (get_lists_next (current)))
        {
          lua_rawgeti (L, LUA_REGISTRYINDEX, current);
          lua_setglobal (L, "current");
          (void) CLUE_DO (L, "newvalue = evaluateNode (current.next)");
          lua_getglobal (L, "newvalue");
          if (newvalue != leNIL)
            luaL_unref (L, LUA_REGISTRYINDEX, newvalue);
          newvalue = luaL_ref (L, LUA_REGISTRYINDEX);
          set_variable (get_lists_data (current), get_lists_data (newvalue));
          if (LUA_NIL (get_lists_next (current)))
            break; /* Cope with odd-length argument lists. */
        }
    }

  ok = newvalue;
}
END_DEFUN

void
init_eval (void)
{
  functions_history = history_new ();
  lua_register (L, "call_zile_command", call_zile_command);
}

void
free_eval (void)
{
  free_history (functions_history);
}
