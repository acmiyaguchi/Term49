/*
 * bbnix activation-manifest parser. The bbnix deploy-bundle ships
 * <root>/etc/bbnix-env (and a POSIX-sh reference applier at
 * <root>/etc/bbnix-activate). Term50 is the other in-tree implementation of
 * that parser, called from the pty-fork child before execl of the login
 * shell. See bbnix_env.h for the format.
 */

#include "bbnix_env.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BBNIX_ENV_LINE_BUF 1024
#define BBNIX_ENV_VAL_BUF  1024

/* Expand $ROOT and $HOME literals in `in` into `out`, single forward pass.
 * Any other byte (including a stray `$` not followed by ROOT/HOME) is
 * copied verbatim. Mirrors bbnix-activate's expansion: the token is matched
 * as a literal substring, no delimiter required after -- the shipped
 * manifest only uses $ROOT/ and $HOME (path-suffixed), so collisions like
 * `$ROOTFS` are not a concern. Returns 0 if the expansion would overflow. */
static int expand_value(const char *in, const char *root, const char *home,
                        char *out, size_t out_cap) {
	size_t oi = 0;
	while(*in){
		const char *sub = NULL;
		size_t sub_len = 0;
		size_t skip = 0;
		if(in[0] == '$' && strncmp(in + 1, "ROOT", 4) == 0){
			sub = root; sub_len = strlen(root); skip = 5;
		} else if(in[0] == '$' && strncmp(in + 1, "HOME", 4) == 0){
			sub = home; sub_len = strlen(home); skip = 5;
		}
		if(sub != NULL){
			if(oi + sub_len >= out_cap){ return 0; }
			memcpy(out + oi, sub, sub_len);
			oi += sub_len;
			in += skip;
		} else {
			if(oi + 1 >= out_cap){ return 0; }
			out[oi++] = *in++;
		}
	}
	out[oi] = '\0';
	return 1;
}

/* Prepend "<val>" to colon-list env var `key`. Sets to <val> if currently
 * unset/empty; otherwise to "<val>:<existing>". */
static void env_prepend_colon(const char *key, const char *val) {
	const char *cur = getenv(key);
	if(cur == NULL || cur[0] == '\0'){
		setenv(key, val, 1);
		return;
	}
	size_t len = strlen(val) + 1 + strlen(cur) + 1;
	char *composed = malloc(len);
	if(composed == NULL){
		fprintf(stderr, "bbnix-env: could not allocate %s - leaving unchanged\n",
		        key);
		return;
	}
	snprintf(composed, len, "%s:%s", val, cur);
	setenv(key, composed, 1);
	free(composed);
}

static int is_regular_file(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_directory(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int bbnix_apply_env_manifest(const char *root) {
	char path[1024];
	if(snprintf(path, sizeof(path), "%s/etc/bbnix-env", root)
	   >= (int)sizeof(path)){
		fprintf(stderr, "bbnix-env: root path too long, skipping manifest\n");
		return -1;
	}

	FILE *fp = fopen(path, "r");
	if(fp == NULL){
		fprintf(stderr, "bbnix-env: cannot open %s: %s\n",
		        path, strerror(errno));
		return -1;
	}

	const char *home = getenv("HOME");
	if(home == NULL){ home = ""; }

	char line[BBNIX_ENV_LINE_BUF];
	int lineno = 0;
	while(fgets(line, sizeof(line), fp) != NULL){
		lineno++;

		size_t len = strlen(line);
		if(len > 0 && line[len - 1] == '\n'){
			line[--len] = '\0';
		} else if(len == sizeof(line) - 1){
			/* No newline came back and the buffer is full -- the line was
			 * truncated. Drain to the next newline so we don't read its
			 * tail as the next "line". */
			int c;
			fprintf(stderr, "bbnix-env: line %d truncated, skipping\n", lineno);
			while((c = fgetc(fp)) != EOF && c != '\n'){ }
			continue;
		}

		/* Strip trailing whitespace, matching bbnix-activate's POSIX
		 * `IFS=' \t' read -r mode rest` semantics: read strips trailing
		 * IFS from the final field. Without this a stray trailing space
		 * on a value line (e.g. `set TERMINFO=$ROOT/terminfo `) would
		 * leave the space in the value here while the shim drops it. */
		while(len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')){
			line[--len] = '\0';
		}

		char *p = line;
		while(*p == ' ' || *p == '\t'){ p++; }
		if(*p == '\0' || *p == '#'){ continue; }

		char *mode = p;
		while(*p && *p != ' ' && *p != '\t'){ p++; }
		if(*p == '\0'){
			fprintf(stderr, "bbnix-env: line %d missing KEY=VALUE\n", lineno);
			continue;
		}
		*p++ = '\0';
		while(*p == ' ' || *p == '\t'){ p++; }

		char *eq = strchr(p, '=');
		if(eq == NULL){
			fprintf(stderr, "bbnix-env: line %d missing '=' in '%s'\n",
			        lineno, p);
			continue;
		}
		*eq = '\0';
		const char *key = p;
		const char *raw_val = eq + 1;

		char val[BBNIX_ENV_VAL_BUF];
		if(!expand_value(raw_val, root, home, val, sizeof(val))){
			fprintf(stderr, "bbnix-env: line %d value expansion overflowed\n",
			        lineno);
			continue;
		}

		if(strcmp(mode, "set") == 0){
			setenv(key, val, 1);
		} else if(strcmp(mode, "default") == 0){
			/* Treat empty as unset, matching bbnix-activate's ${KEY:=$VAL}. */
			const char *cur = getenv(key);
			if(cur == NULL || cur[0] == '\0'){
				setenv(key, val, 1);
			}
		} else if(strcmp(mode, "prepend") == 0){
			env_prepend_colon(key, val);
		} else if(strcmp(mode, "set-if-file") == 0){
			if(is_regular_file(val)){ setenv(key, val, 1); }
		} else if(strcmp(mode, "set-if-dir") == 0){
			if(is_directory(val)){ setenv(key, val, 1); }
		} else {
			fprintf(stderr, "bbnix-env: line %d unknown mode '%s'\n",
			        lineno, mode);
		}
	}

	fclose(fp);
}
