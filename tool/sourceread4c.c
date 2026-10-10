/*
**                                                         __
**    _________  __  _______________  ________  ____ _____/ /
**   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  / 
**  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /  
** /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/       
**
** Copyright [yyyy] [name of copyright owner]
** 
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
** 
**     http://www.apache.org/licenses/LICENSE-2.0
** 
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.                                                   
*/
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include <argparse.h>
#include <tree_sitter/api.h>

#include "sourceread.h"

#define INDENT_SIZE 2
#define INITIAL_BUFFER_CAPACITY 256

static void
sr_parse_file(TSParser* parser, const char* nodetype, const char* filepath, void* userdata)
{
  char* source_code = sr_read_file(filepath);
  TSTree* tree = ts_parser_parse_string(
    parser,
    NULL,
    source_code,
    strlen(source_code)
  );
  TSNode root_node = ts_tree_root_node(tree);
  sr_print_source(root_node, nodetype, source_code, 2);
  free(source_code);
  ts_tree_delete(tree);
}

const TSLanguage* tree_sitter_c(void);

static const char* const usages[] = 
{
  "sourceread4c [options]",
  NULL,
};

int main(int argc, char *argv[]) 
{
  char* file = NULL;
  char* proj = NULL;
  char* type = NULL;
  char* ext = NULL;

  struct argparse_option options[] = {
    OPT_HELP(),
    OPT_STRING('f', "file", &file, "input file path", NULL, 0, 0),
    OPT_STRING('p', "project", &proj, "input project path", NULL, 0, 0),
    OPT_STRING('t', "type", &type, "grammar node type", NULL, 0, 0),
    OPT_STRING('x', "extension", &ext, "file extension", NULL, 0, 0),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, options, usages, 0);
  argparse_describe(&argparse, "\nParse c file or projects to AST.", NULL);
  
  argc = argparse_parse(&argparse, argc, (const char**) argv);
  if (file == NULL && proj == NULL) 
  {
    argparse_usage(&argparse);
    return 1;
  }
  if (proj != NULL && ext == NULL || type == NULL)
  {
    argparse_usage(&argparse);
    return 1;
  }

  TSParser* parser = ts_parser_new();

  if (!ts_parser_set_language(parser, tree_sitter_c())) {
    fprintf(stderr, "Error loading C grammar.\n");
    ts_parser_delete(parser);
    return 1;
  }

  if (file)
  {
    sr_parse_file(parser, type, file, NULL);
  } 
  else if (proj)
  {
    // sr_walk_dir(proj, ext, parser, type, sr_parse_file, NULL);
    sr_build_index(parser, "fun.sri", "fun.srd", proj, ".c,.h", type);
  }

  ts_parser_delete(parser);
  return 0;
}
