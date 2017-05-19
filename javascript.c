/*	$Id$ */
/*
 * Copyright (c) 2017 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/queue.h>

#if HAVE_ERR
# include <err.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static void
gen_js_field(const struct field *f)
{

	/* TODO: blob handling. */

	if (FIELD_NOEXPORT & f->flags || FTYPE_BLOB == f->type)
		return;

	if (FIELD_NULL & f->flags) {
		printf("\t\t\tif (null === this.obj.%s) {\n"
		       "\t\t\t\t_hidecl(e, '%s-has-%s');\n"
		       "\t\t\t\t_showcl(e, '%s-no-%s');\n"
		       "\t\t\t} else {\n"
		       "\t\t\t\t_showcl(e, '%s-has-%s');\n"
		       "\t\t\t\t_hidecl(e, '%s-no-%s');\n",
		       f->name, 
		       f->parent->name, f->name,
		       f->parent->name, f->name,
		       f->parent->name, f->name,
		       f->parent->name, f->name);
		if (FTYPE_STRUCT == f->type) 
			printf("\t\t\t\tlist = e.getElementsByClassName"
				     "('%s-%s-obj');\n"
			       "\t\t\tfor (i = 0; i < list.length; i++)\n"
			       "\t\t\t\t%s.fill(list[i], this.obj.%s);\n",
			       f->parent->name, f->name, 
			       f->ref->tstrct, f->name);
		else
			printf("\t\t\t\t_replcl(e, '%s-%s-text', this.obj.%s);\n",
				f->parent->name, f->name, 
				f->name);
		puts("\t\t\t}");
	} else {
		if (FTYPE_STRUCT == f->type) 
			printf("\t\t\tlist = e.getElementsByClassName"
				     "('%s-%s-obj');\n"
			       "\t\t\tfor (i = 0; i < list.length; i++)\n"
			       "\t\t\t\t%s.fill(list[i], this.obj.%s);\n",
			       f->parent->name, f->name, 
			       f->ref->tstrct, f->name);
		else 
			printf("\t\t\t_replcl(e, '%s-%s-text', this.obj.%s);\n",
				f->parent->name, f->name, 
				f->name);
	}
}

void
gen_javascript(const struct strctq *sq)
{
	const struct strct *s;
	const struct field *f;

	puts("(function(root) {\n"
	     "\t'use strict';\n"
	     "\n"
	     "\tfunction _repl(e, text)\n"
	     "\t{\n"
	     "\t\tif (null === e)\n"
	     "\t\t\treturn;\n"
	     "\t\twhile (e.firstChild)\n"
	     "\t\t\te.removeChild(e.firstChild);\n"
	     "\t\te.appendChild(document.createTextNode(text));\n"
	     "\t}\n"
	     "\n"
	     "\tfunction _replcl(e, name, text)\n"
	     "\t{\n"
	     "\t\tvar list, i;\n"
	     "\t\tif (null === e)\n"
	     "\t\t\treturn;\n"
	     "\t\tlist = e.getElementsByClassName(name);\n"
	     "\t\tfor (i = 0; i < list.length; i++)\n"
	     "\t\t\t_repl(list[i], text);\n"
	     "\t}\n"
	     "\n"
	     "\tfunction _hide(e)\n"
	     "\t{\n"
	     "\t\tif (null === e)\n"
	     "\t\t\treturn(null);\n"
	     "\t\tif ( ! e.classList.contains('hide'))\n"
	     "\t\t\te.classList.add('hide');\n"
	     "\t\treturn(e);\n"
	     "\t}\n"
	     "\t\n"
	     "\tfunction _hidecl(e, name)\n"
	     "\t{\n"
	     "\t\tvar list, i;\n"
	     "\t\tif (null === e)\n"
	     "\t\t\treturn;\n"
	     "\t\tlist = e.getElementsByClassName(name);\n"
	     "\t\tfor (i = 0; i < list.length; i++)\n"
	     "\t\t\t_hide(list[i]);\n"
	     "\t}\n"
	     "\n"
	     "\tfunction _show(e)\n"
	     "\t{\n"
	     "\t\tif (null === e)\n"
	     "\t\t\treturn(null);\n"
	     "\t\tif (e.classList.contains('hide'))\n"
	     "\t\t\te.classList.remove('hide');\n"
	     "\t\treturn(e);\n"
	     "\t}\n"
	     "\t\n"
	     "\tfunction _showcl(e, name)\n"
	     "\t{\n"
	     "\t\tvar list, i;\n"
	     "\t\tif (null === e)\n"
	     "\t\t\treturn;\n"
	     "\t\tlist = e.getElementsByClassName(name);\n"
	     "\t\tfor (i = 0; i < list.length; i++)\n"
	     "\t\t\t_show(list[i]);\n"
	     "\t}\n"
	     "");

	TAILQ_FOREACH(s, sq, entries) {
		printf("\tfunction %s(obj)\n"
		       "\t{\n"
		       "\t\tthis.obj = obj;\n"
		       "\t\tthis.fill = function(e){\n",
		       s->name);
		TAILQ_FOREACH(f, &s->fq, entries)
			if ( ! (FIELD_NOEXPORT & f->flags) &&
			    FTYPE_STRUCT == f->type) {
				puts("\t\t\tvar list, i;");
				break;
			}
		TAILQ_FOREACH(f, &s->fq, entries)
			gen_js_field(f);
		printf("\t\t};\n"
		       "\t}\n"
		       "\n");
	}

	TAILQ_FOREACH(s, sq, entries)
		printf("\troot.%s = %s;\n", s->name, s->name);

	puts("})(this);");

}