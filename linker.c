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

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

/*
 * Check that a given row identifier is valid.
 * The rules are that only one row identifier can exist on a structure
 * and that it must happen on a native type.
 */
static int
checkrowid(const struct field *f, int hasrowid)
{

	if (hasrowid) {
		warnx("%s.%s: multiple rowids on "
			"structure", f->parent->name, f->name);
		return(0);
	}

	if (FTYPE_STRUCT == f->type) {
		warnx("%s.%s: rowid on non-native field"
			"type", f->parent->name, f->name);
		return(0);
	}

	return(1);
}

/*
 * Reference rules: we can't reference from or to a struct, nor can the
 * target and source be of a different type.
 */
static int
checktargettype(const struct ref *ref)
{

	/* Our actual reference objects may not be structs. */

	if (FTYPE_STRUCT == ref->target->type ||
	    FTYPE_STRUCT == ref->source->type) {
		warnx("%s.%s: referencing a struct",
			ref->parent->parent->name,
			ref->parent->name);
		return(0);
	} 

	/* Our reference objects must have equivalent types. */

	if (ref->source->type != ref->target->type) {
		warnx("%s.%s: referencing a different type",
			ref->parent->parent->name,
			ref->parent->name);
		return(0);
	}

	if ( ! (FIELD_ROWID & ref->target->flags)) 
		warnx("%s.%s: referenced target %s.%s is not "
			"a unique field",
			ref->parent->parent->name,
			ref->parent->name,
			ref->target->parent->name,
			ref->target->name);

	return(1);
}

/*
 * When we're parsing a structure's reference, we need to create the
 * referring information to the source field, which is the actual
 * reference itself.
 * Return zero on failure, non-zero on success.
 */
static int
linkref(struct ref *ref)
{

	assert(NULL != ref->parent);
	assert(NULL != ref->source);
	assert(NULL != ref->target);

	if (FTYPE_STRUCT != ref->parent->type)
		return(1);

	/*
	 * If our source field is already a reference, make sure it
	 * points to the same thing we point to.
	 * Otherwise, it's an error.
	 */

	if (NULL != ref->source->ref &&
	    (strcasecmp(ref->tfield, ref->source->ref->tfield) ||
	     strcasecmp(ref->tstrct, ref->source->ref->tstrct))) {
		warnx("%s.%s: redeclaration of reference",
			ref->parent->parent->name,
			ref->parent->name);
		return(0);
	} else if (NULL != ref->source->ref)
		return(1);

	/* Make sure that the target is a rowid and not null. */

	if ( ! (FIELD_ROWID & ref->target->flags)) {
		warnx("%s.%s: target is not a rowid",
			ref->target->parent->name,
			ref->target->name);
		return(0);
	} else if (FIELD_NULL & ref->target->flags) {
		warnx("%s.%s: target can't be null",
			ref->target->parent->name,
			ref->target->name);
		return(0);
	}

	/* Create linkage. */

	ref->source->ref = calloc(1, sizeof(struct ref));
	if (NULL == ref->source->ref)
		err(EXIT_FAILURE, NULL);

	ref->source->ref->parent = ref->source;
	ref->source->ref->source = ref->source;
	ref->source->ref->target = ref->target;

	ref->source->ref->sfield = strdup(ref->sfield);
	ref->source->ref->tfield = strdup(ref->tfield);
	ref->source->ref->tstrct = strdup(ref->tstrct);

	if (NULL == ref->source->ref->sfield ||
	    NULL == ref->source->ref->tfield ||
	    NULL == ref->source->ref->tstrct)
		err(EXIT_FAILURE, NULL);

	return(1);
}

/*
 * Check the source field (case insensitive).
 * Return zero on failure, non-zero on success.
 * On success, this sets the "source" field for the referrent.
 */
static int
resolve_field_source(struct ref *ref, struct strct *s)
{
	struct field	*f;

	if (NULL != ref->source)
		return(1);

	assert(NULL == ref->source);
	assert(NULL == ref->target);

	TAILQ_FOREACH(f, &s->fq, entries) {
		if (strcasecmp(f->name, ref->sfield))
			continue;
		ref->source = f;
		return(1);
	}

	warnx("%s:%zu%zu: unknown reference target",
		ref->parent->pos.fname, ref->parent->pos.line, 
		ref->parent->pos.column);
	return(0);
}

/*
 * Check that the target structure and field exist (case insensitive).
 * Return zero on failure, non-zero on success.
 * On success, this sets the "target" field for the referrent.
 */
static int
resolve_field_target(struct ref *ref, struct strctq *q)
{
	struct strct	*p;
	struct field	*f;

	if (NULL != ref->target)
		return(1);

	assert(NULL != ref->source);
	assert(NULL == ref->target);

	TAILQ_FOREACH(p, q, entries) {
		if (strcasecmp(p->name, ref->tstrct))
			continue;
		TAILQ_FOREACH(f, &p->fq, entries) {
			if (strcasecmp(f->name, ref->tfield))
				continue;
			ref->target = f;
			return(1);
		}
	}
	warnx("%s:%zu%zu: unknown reference target",
		ref->parent->pos.fname, ref->parent->pos.line, 
		ref->parent->pos.column);
	return(0);
}

/*
 * Resolve an enumeration.
 * This returns zero if the resolution fails, non-zero otherwise.
 * In the success case, it sets the enumeration link.
 */
static int
resolve_field_enum(struct eref *ref, struct enmq *q)
{
	struct enm	*e;

	TAILQ_FOREACH(e, q, entries)
		if (0 == strcasecmp(e->name, ref->ename)) {
			ref->enm = e;
			return(1);
		}
	warnx("%s:%zu:%zu: unknown enum reference",
		ref->parent->pos.fname, ref->parent->pos.line, 
		ref->parent->pos.column);
	return(0);
}

/*
 * Recursively check for... recursion.
 * Returns zero if the reference is recursive, non-zero otherwise.
 */
static int
check_recursive(struct ref *ref, const struct strct *check)
{
	struct field	*f;
	struct strct	*p;

	assert(NULL != ref);

	if ((p = ref->target->parent) == check)
		return(0);

	TAILQ_FOREACH(f, &p->fq, entries)
		if (FTYPE_STRUCT == f->type)
			if ( ! check_recursive(f->ref, check))
				return(0);

	return(1);
}

/*
 * Recursively annotate our height from each node.
 * We only do this for FTYPE_STRUCT objects.
 */
static void
annotate(struct ref *ref, size_t height, size_t colour)
{
	struct field	*f;
	struct strct	*p;

	p = ref->target->parent;

	if (p->colour == colour)
		return;

	p->colour = colour;
	p->height += height;

	TAILQ_FOREACH(f, &p->fq, entries)
		if (FTYPE_STRUCT == f->type)
			annotate(f->ref, height + 1, colour);
}

/*
 * Resolve a specific update reference by looking it up in our parent
 * structure.
 * Return zero on failure, non-zero on success.
 */
static int
resolve_uref(struct uref *ref, int crq)
{
	struct field	*f;
	const char	*type;

	type = UP_MODIFY == ref->parent->type ? 
		"update" : "delete";

	assert(NULL == ref->field);
	assert(NULL != ref->parent);

	TAILQ_FOREACH(f, &ref->parent->parent->fq, entries)
		if (0 == strcasecmp(f->name, ref->name))
			break;

	if (NULL == (ref->field = f))
		warnx("%s:%zu:%zu: %s term not found",
			ref->pos.fname, ref->pos.line,
			ref->pos.column, type);
	else if (FTYPE_STRUCT == f->type)
		warnx("%s:%zu:%zu: %s term is a struct", 
			ref->pos.fname, ref->pos.line,
			ref->pos.column, type);
	else if (crq && FTYPE_PASSWORD == f->type)
		warnx("%s:%zu:%zu: %s constraint is a password", 
			ref->pos.fname, ref->pos.line,
			ref->pos.column, type);
	else
		return(1);

	return(0);
}

/*
 * Make sure that our constraint operator is consistent with the type
 * named in the constraint.
 * Returns zero on failure, non-zero on success.
 * (For the time being, this always returns non-zero.)
 */
static int
check_updatetype(struct update *up)
{
	struct uref	*ref;

	TAILQ_FOREACH(ref, &up->crq, entries)
		if ((OPTYPE_NOTNULL == ref->op ||
		     OPTYPE_ISNULL == ref->op) &&
		    ! (FIELD_NULL & ref->field->flags))
			warnx("%s:%zu:%zu: null operator "
				"on field that's never null",
				ref->pos.fname, 
				ref->pos.line,
				ref->pos.column);
	return(1);
}

/*
 * Make sure that our modification type is numeric.
 * (Text-based modifications with "add" or "sub" or otherwise don't
 * really make sense.
 */
static int
check_modtype(const struct uref *ref)
{

	assert(MODTYPE__MAX != ref->mod);

	if (MODTYPE_SET == ref->mod ||
	    FTYPE_EPOCH == ref->field->type ||
	    FTYPE_INT == ref->field->type ||
	    FTYPE_REAL == ref->field->type)
		return(1);

	warnx("%s:%zu:%zu: update modification on "
		"invalid field type (not numeric)",
		ref->pos.fname,
		ref->pos.line,
		ref->pos.column);
	return(0);
}

/*
 * Resolve all of the fields managed by struct update.
 * These are all local to the current structure.
 * (This is a constraint of SQL.)
 * Return zero on failure, non-zero on success.
 */
static int
resolve_update(struct update *up)
{
	struct uref	*ref;

	/* Will always be empty for UPT_DELETE. */

	TAILQ_FOREACH(ref, &up->mrq, entries) {
		if ( ! resolve_uref(ref, 0))
			return(0);
		if ( ! check_modtype(ref))
			return(0);
	}
	TAILQ_FOREACH(ref, &up->crq, entries)
		if ( ! resolve_uref(ref, 1))
			return(0);

	return(1);
}

/*
 * Recursively follow the chain of references in a search target,
 * finding out whether it's well-formed in the process.
 * Returns zero on failure, non-zero on success.
 */
static int
resolve_sref(struct sref *ref, struct strct *s)
{
	struct field	*f;

	TAILQ_FOREACH(f, &s->fq, entries)
		if (0 == strcasecmp(f->name, ref->name))
			break;

	/* Did we find the field in our structure? */

	if (NULL == (ref->field = f)) {
		warnx("%s:%zu:%zu: search term not found",
			ref->pos.fname, ref->pos.line,
			ref->pos.column);
		return(0);
	}

	/* 
	 * If we're following a chain, we must have a "struct" type;
	 * otherwise, we must have a native type.
	 */

	if (NULL == TAILQ_NEXT(ref, entries)) {
		if (FTYPE_STRUCT != f->type) 
			return(1);
		warnx("%s:%zu:%zu: search term leaf field "
			"is a struct", 
			ref->pos.fname, ref->pos.line,
			ref->pos.column);
		return(0);
	} else if (FTYPE_STRUCT != f->type) {
		warnx("%s:%zu:%zu: search term node field "
			"is not a struct", 
			ref->pos.fname, ref->pos.line,
			ref->pos.column);
		return(0);
	}

	/* Follow the chain of our reference. */

	ref = TAILQ_NEXT(ref, entries);
	return(resolve_sref(ref, f->ref->target->parent));
}

/*
 * Sort by reverse height.
 */
static int
parse_cmp(const void *a1, const void *a2)
{
	const struct strct 
	      *p1 = *(const struct strct **)a1, 
	      *p2 = *(const struct strct **)a2;

	return((ssize_t)p1->height - (ssize_t)p2->height);
}

/*
 * Recursively create the list of all possible search prefixes we're
 * going to see in this structure.
 * This consists of all "parent.child" chains of structure that descend
 * from the given "orig" original structure.
 * FIXME: artificially limited to 26 entries.
 */
static void
resolve_aliases(struct strct *orig, struct strct *p, 
	size_t *offs, const struct alias *prior)
{
	struct field	*f;
	struct alias	*a;
	int		 c;

	TAILQ_FOREACH(f, &p->fq, entries) {
		if (FTYPE_STRUCT != f->type)
			continue;
		assert(NULL != f->ref);
		
		a = calloc(1, sizeof(struct alias));
		if (NULL == a)
			err(EXIT_FAILURE, NULL);

		if (NULL != prior) {
			c = asprintf(&a->name, "%s.%s",
				prior->name, f->name);
			if (c < 0)
				err(EXIT_FAILURE, NULL);
		} else
			a->name = strdup(f->name);

		if (NULL == a->name)
			err(EXIT_FAILURE, NULL);

		assert(*offs < 26);
		c = asprintf(&a->alias, 
			"_%c", (char)*offs + 97);
		if (c < 0)
			err(EXIT_FAILURE, NULL);

		(*offs)++;
		TAILQ_INSERT_TAIL(&orig->aq, a, entries);
		resolve_aliases(orig, f->ref->target->parent, offs, a);
	}
}

/*
 * Check to see that our search type (e.g., list or iterate) is
 * consistent with the fields that we're searching for.
 * In other words, running an iterator search on a unique row isn't
 * generally useful.
 * Also warn if null-sensitive operators (isnull, notnull) will be run
 * on non-null fields.
 * Return zero on failure, non-zero on success.
 */
static int
check_searchtype(const struct strct *p)
{
	const struct search *srch;
	const struct sent *sent;
	const struct sref *sr;

	TAILQ_FOREACH(srch, &p->sq, entries) {
		if (SEARCH_IS_UNIQUE & srch->flags && 
		    STYPE_SEARCH != srch->type) 
			warnx("%s:%zu:%zu: multiple-result search "
				"on a unique field",
				srch->pos.fname, srch->pos.line,
				srch->pos.column);
		if ( ! (SEARCH_IS_UNIQUE & srch->flags) && 
		    STYPE_SEARCH == srch->type)
			warnx("%s:%zu:%zu: single-result search "
				"on a non-unique field",
				srch->pos.fname, srch->pos.line,
				srch->pos.column);

		TAILQ_FOREACH(sent, &srch->sntq, entries) {
			sr = TAILQ_LAST(&sent->srq, srefq);
			if ((OPTYPE_NOTNULL == sent->op ||
			     OPTYPE_ISNULL == sent->op) &&
			    ! (FIELD_NULL & sr->field->flags))
				warnx("%s:%zu:%zu: null operator "
					"on field that's never null",
					sent->pos.fname, 
					sent->pos.line,
					sent->pos.column);
			/* 
			 * FIXME: we should (in theory) allow for the
			 * unary types and equality binary types.
			 * But for now, mandate equality.
			 */
			if (OPTYPE_EQUAL != sent->op &&
			    FTYPE_PASSWORD == sr->field->type) {
				warnx("%s:%zu:%zu: password field "
					"only processes equality",
					sent->pos.fname,
					sent->pos.line,
					sent->pos.column);
				return(0);
			}
		}
	}

	return(1);
}

/*
 * Resolve the chain of search terms.
 * To do so, descend into each set of search terms for the structure and
 * resolve the fields.
 * Also set whether we have row identifiers within the search expansion.
 */
static int
resolve_search(struct search *srch)
{
	struct sent	*sent;
	struct sref	*ref;
	struct alias	*a;
	struct strct	*p;

	p = srch->parent;

	TAILQ_FOREACH(sent, &srch->sntq, entries) {
		ref = TAILQ_FIRST(&sent->srq);
		if ( ! resolve_sref(ref, p))
			return(0);
		ref = TAILQ_LAST(&sent->srq, srefq);
		if (FIELD_ROWID & ref->field->flags ||
		    FIELD_UNIQUE & ref->field->flags) {
			sent->flags |= SENT_IS_UNIQUE;
			srch->flags |= SEARCH_IS_UNIQUE;
		}
		if (NULL == sent->name)
			continue;

		/* 
		 * Look up our alias name.
		 * Our resolve_sref() function above
		 * makes sure that the reference exists,
		 * so just assert on lack of finding.
		 */

		TAILQ_FOREACH(a, &p->aq, entries)
			if (0 == strcasecmp(a->name, sent->name))
				break;
		assert(NULL != a);
		sent->alias = a;
	}

	return(1);
}

static int
check_unique(const struct unique *u)
{
	const struct nref *n;

	TAILQ_FOREACH(n, &u->nq, entries) {
		if (FTYPE_STRUCT != n->field->type)
			continue;
		warnx("%s:%zu:%zu: field not a native type",
			n->pos.fname, n->pos.line, n->pos.column);
		return(0);
	}

	return(1);
}

/*
 * Resolve the chain of unique fields.
 * These are all in the local structure.
 */
static int
resolve_unique(struct unique *u)
{
	struct nref	*n;
	struct field	*f;

	TAILQ_FOREACH(n, &u->nq, entries) {
		TAILQ_FOREACH(f, &u->parent->fq, entries) 
			if (0 == strcasecmp(f->name, n->name))
				break;
		if (NULL != (n->field = f))
			continue;
		warnx("%s:%zu:%zu: field not found",
			n->pos.fname, n->pos.line, n->pos.column);
		return(0);
	}

	return(1);
}

int
parse_link(struct config *cfg)
{
	struct update	 *u;
	struct strct	 *p;
	struct strct	**pa;
	struct field	 *f;
	struct unique	 *n;
	struct search	 *srch;
	size_t		  colour = 1, sz = 0, i = 0, hasrowid = 0;

	/* 
	 * First, establish linkage between nodes.
	 * While here, check for duplicate rowids.
	 */

	TAILQ_FOREACH(p, &cfg->sq, entries) {
		hasrowid = 0;
		TAILQ_FOREACH(f, &p->fq, entries) {
			if (FIELD_ROWID & f->flags)
				if ( ! checkrowid(f, hasrowid++))
					return(0);
			if (NULL != f->ref &&
			    (! resolve_field_source(f->ref, p) ||
			     ! resolve_field_target(f->ref, &cfg->sq) ||
			     ! linkref(f->ref) ||
			     ! checktargettype(f->ref)))
				return(0);
			if (NULL != f->eref &&
			    ! resolve_field_enum(f->eref, &cfg->eq))
				return(0);
		}
		TAILQ_FOREACH(u, &p->uq, entries)
			if ( ! resolve_update(u) ||
			     ! check_updatetype(u))
				return(0);
		TAILQ_FOREACH(u, &p->dq, entries)
			if ( ! resolve_update(u) ||
			     ! check_updatetype(u))
				return(0);
	}

	/* Check for reference recursion. */

	TAILQ_FOREACH(p, &cfg->sq, entries)
		TAILQ_FOREACH(f, &p->fq, entries)
			if (FTYPE_STRUCT == f->type) {
				if (check_recursive(f->ref, p))
					continue;
				warnx("%s:%zu:%zu: recursive "
					"reference", f->pos.fname, 
					f->pos.line, f->pos.column);
				return(0);
			}

	/* 
	 * Now follow and order all outbound links for structs.
	 * From the get-go, we don't descend into structures that we've
	 * already coloured.
	 * This establishes a "height" that we'll use when ordering our
	 * structures in the header file.
	 */

	TAILQ_FOREACH(p, &cfg->sq, entries) {
		sz++;
		if (p->colour)
			continue;
		TAILQ_FOREACH(f, &p->fq, entries)
			if (FTYPE_STRUCT == f->type) {
				p->colour = colour;
				annotate(f->ref, 1, colour);
			}
		colour++;
	}
	assert(sz > 0);

	/*
	 * Next, create unique names for all joins within a structure.
	 * We do this by creating a list of all search patterns (e.g.,
	 * user.name and user.company.name, which assumes two structures
	 * "user" and "company", the first pointing into the second,
	 * both of which contain "name").
	 */

	i = 0;
	TAILQ_FOREACH(p, &cfg->sq, entries)
		resolve_aliases(p, p, &i, NULL);

	/* Resolve search terms. */

	TAILQ_FOREACH(p, &cfg->sq, entries)
		TAILQ_FOREACH(srch, &p->sq, entries)
			if ( ! resolve_search(srch))
				return(0);

	TAILQ_FOREACH(p, &cfg->sq, entries)
		TAILQ_FOREACH(n, &p->nq, entries)
			if ( ! resolve_unique(n) ||
			     ! check_unique(n))
				return(0);

	/* See if our search type is wonky. */

	TAILQ_FOREACH(p, &cfg->sq, entries)
		if ( ! check_searchtype(p))
			return(0);

	/* 
	 * Copy the list into a temporary array.
	 * Then sort the list by reverse-height.
	 * Finally, re-create the list from the sorted elements.
	 */

	if (NULL == (pa = calloc(sz, sizeof(struct strct *))))
		err(EXIT_FAILURE, NULL);

	i = 0;
	while (NULL != (p = TAILQ_FIRST(&cfg->sq))) {
		TAILQ_REMOVE(&cfg->sq, p, entries);
		assert(i < sz);
		pa[i++] = p;
	}
	assert(i == sz);
	qsort(pa, sz, sizeof(struct strct *), parse_cmp);
	for (i = 0; i < sz; i++)
		TAILQ_INSERT_HEAD(&cfg->sq, pa[i], entries);

	free(pa);
	return(1);
}
