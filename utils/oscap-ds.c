/*
 * Copyright 2012 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Martin Preisler <mpreisle@redhat.com>
 */

/* Standard header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/* DS */
#include <ds.h>

#include "oscap-tool.h"

static struct oscap_module* DS_SUBMODULES[];
bool getopt_ds(int argc, char **argv, struct oscap_action *action);
int app_ds_sds_split(const struct oscap_action *action);
int app_ds_sds_compose(const struct oscap_action *action);
int app_ds_rds_create(const struct oscap_action *action);

struct oscap_module OSCAP_DS_MODULE = {
	.name = "ds",
	.parent = &OSCAP_ROOT_MODULE,
	.summary = "DataStream utilities",
	.submodules = DS_SUBMODULES
};

static struct oscap_module DS_SDS_SPLIT_MODULE = {
	.name = "sds_split",
	.parent = &OSCAP_DS_MODULE,
	.summary = "Split given SourceDataStream into separate files",
	.usage = "sds.xml target_directory/",
	.help = NULL,
	.opt_parser = getopt_ds,
	.func = app_ds_sds_split
};

static struct oscap_module DS_SDS_COMPOSE_MODULE = {
	.name = "sds_compose",
	.parent = &OSCAP_DS_MODULE,
	.summary = "Compose SourceDataStream from given XCCDF",
	.usage = "xccdf-file.xml target_datastream.xml",
	.help = NULL,
	.opt_parser = getopt_ds,
	.func = app_ds_sds_compose
};

static struct oscap_module DS_RDS_CREATE_MODULE = {
	.name = "rds_create",
	.parent = &OSCAP_DS_MODULE,
	.summary = "Create a ResultDataStream from given SourceDataStream, XCCDF results and one or more OVAL results",
	.usage = "sds.xml target-arf.xml results-xccdf.xml [results-oval1.xml [results-oval2.xml]]",
	.help = NULL,
	.opt_parser = getopt_ds,
	.func = app_ds_rds_create
};

static struct oscap_module* DS_SUBMODULES[] = {
	&DS_SDS_SPLIT_MODULE,
	&DS_SDS_COMPOSE_MODULE,
	&DS_RDS_CREATE_MODULE,
	NULL
};

bool getopt_ds(int argc, char **argv, struct oscap_action *action) {

	if( (action->module == &DS_SDS_SPLIT_MODULE) ) {
		if(  argc != 5 ) {
			oscap_module_usage(action->module, stderr, "Wrong number of parameteres.\n");
			return false;
		}
		action->ds_action = malloc(sizeof(struct ds_action));
		action->ds_action->file = argv[3];
		action->ds_action->target = argv[4];
	}
	else if( (action->module == &DS_SDS_COMPOSE_MODULE) ) {
		if(  argc != 5 ) {
			oscap_module_usage(action->module, stderr, "Wrong number of parameteres.\n");
			return false;
		}
		action->ds_action = malloc(sizeof(struct ds_action));
		action->ds_action->file = argv[3];
		action->ds_action->target = argv[4];
	}
	else if( (action->module == &DS_RDS_CREATE_MODULE) ) {
		if(  argc < 6 ) {
			oscap_module_usage(action->module, stderr, "Wrong number of parameteres.\n");
			return false;
		}
		action->ds_action = malloc(sizeof(struct ds_action));
		action->ds_action->file = argv[3];
		action->ds_action->target = argv[4];
		action->ds_action->xccdf_result = argv[5];
		action->ds_action->oval_results = &argv[6];
		action->ds_action->oval_result_count = argc - 6;
	}
	return true;
}

int app_ds_sds_split(const struct oscap_action *action) {
	int ret;

	/* Validate */
	if (action->validate)
	{
		if (!oscap_validate_document(action->ds_action->file, OSCAP_DOCUMENT_SDS, NULL, (action->verbosity >= 0 ? oscap_reporter_fd : NULL), stdout))
		{
			fprintf(stdout, "Invalid input source data stream in in %s\n", action->ds_action->file);
			ret = OSCAP_ERROR;
			goto cleanup;
		}
	}

	ds_sds_decompose(action->ds_action->file, NULL, action->ds_action->target, NULL);

	// TODO: error handling
	ret = OSCAP_OK;

cleanup:
	free(action->ds_action);
	return ret;
}

int app_ds_sds_compose(const struct oscap_action *action) {
	int ret;

	ds_sds_compose_from_xccdf(action->ds_action->file, action->ds_action->target);

	if (action->validate)
	{
		if (!oscap_validate_document(action->ds_action->target, OSCAP_DOCUMENT_SDS, NULL, (action->verbosity >= 0 ? oscap_reporter_fd : NULL), stdout))
		{
			fprintf(stdout, "Exported Source Data Stream '%s' is not valid, it has not been exported correctly!\n", action->ds_action->target);
			ret = OSCAP_ERROR;
			goto cleanup;
		}
	}

	// TODO: error handling
	ret = OSCAP_OK;

cleanup:
	free(action->ds_action);
	return ret;
}

int app_ds_rds_create(const struct oscap_action *action) {
	int ret;

	if (action->validate)
	{
		if (!oscap_validate_document(action->ds_action->file, OSCAP_DOCUMENT_SDS, NULL, (action->verbosity >= 0 ? oscap_reporter_fd : NULL), stdout))
		{
			fprintf(stdout, "Given Source Data Stream '%s' does not validate!\n", action->ds_action->file);
			ret = OSCAP_ERROR;
			goto cleanup;
		}

		if (!oscap_validate_document(action->ds_action->xccdf_result, OSCAP_DOCUMENT_XCCDF, NULL, (action->verbosity >= 0 ? oscap_reporter_fd : NULL), stdout))
		{
			fprintf(stdout, "Given XCCDF result file '%s' does not validate!\n", action->ds_action->xccdf_result);
			ret = OSCAP_ERROR;
			goto cleanup;
		}
	}

	char** oval_result_files = malloc(sizeof(char*) * (action->ds_action->oval_result_count + 1));

	size_t i;
	for (i = 0; i < action->ds_action->oval_result_count; ++i)
	{
		oval_result_files[i] = action->ds_action->oval_results[i];

		if (action->validate)
		{
			if (!oscap_validate_document(oval_result_files[i], OSCAP_DOCUMENT_OVAL_RESULTS, NULL, (action->verbosity >= 0 ? oscap_reporter_fd : NULL), stdout))
			{
				fprintf(stdout, "Given OVAL results file '%s' does not validate!\n", oval_result_files[i]);
				ret = OSCAP_ERROR;
				goto cleanup;
			}
		}
	}
	oval_result_files[i] = NULL;

	ds_rds_create(action->ds_action->file, action->ds_action->xccdf_result,
		   oval_result_files, action->ds_action->target);

	free(oval_result_files);

	const char* full_validation = getenv("OSCAP_FULL_VALIDATION");

	if (action->validate && full_validation)
	{
		if (!oscap_validate_document(action->ds_action->target, OSCAP_DOCUMENT_ARF, NULL, (action->verbosity >= 0 ? oscap_reporter_fd : NULL), stdout))
		{
			fprintf(stdout, "Exported Result Data Stream '%s' is not valid, it has not been exported correctly!\n", action->ds_action->target);
			ret = OSCAP_ERROR;
			goto cleanup;
		}
	}

	// TODO: error handling
	ret = OSCAP_OK;

cleanup:
	free(action->ds_action);
	return ret;
}
