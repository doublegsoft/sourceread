/*
**                                                         __
**    _________  __  _______________  ________  ____ _____/ /
**   / ___/ __ \/ / / / ___/ ___/ _ \/ ___/ _ \/ __ `/ __  / 
**  (__  ) /_/ / /_/ / /  / /__/  __/ /  /  __/ /_/ / /_/ /  
** /____/\____/\__,_/_/   \___/\___/_/   \___/\__,_/\__,_/                                                          
*/
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include <argparse.h>
#include <tree_sitter/api.h>

#include "sourceread.h"

const TSLanguage* tree_sitter_sql(void);

static const char* const usages[] = 
{
  "sourceread4sql [options]",
  NULL,
};


int main(int argc, char *argv[]) 
{
  char* file = NULL;
  char* proj = NULL;
  char* type = NULL;

  struct argparse_option options[] = {
    OPT_HELP(),
    OPT_STRING('f', "file", &file, "input file path", NULL, 0, 0),
    OPT_STRING('p', "project", &proj, "input project path", NULL, 0, 0),
    OPT_STRING('t', "type", &type, "grammar node type", NULL, 0, 0),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, options, usages, 0);
  argparse_describe(&argparse, "\nParse sql file or projects to AST.", NULL);
  
  argc = argparse_parse(&argparse, argc, (const char**) argv);
  if (file == NULL && proj == NULL) 
  {
    argparse_usage(&argparse);
    return 1;
  }
  if (proj != NULL && type == NULL)
  {
    argparse_usage(&argparse);
    return 1;
  }

  TSParser* parser = ts_parser_new();

  if (!ts_parser_set_language(parser, tree_sitter_sql())) {
    fprintf(stderr, "Error loading sql grammar.\n");
    ts_parser_delete(parser);
    return 1;
  }

  if (file)
  {
    const char* source_code = sr_read_file(file);
    TSTree* tree = ts_parser_parse_string(
      parser,
      NULL,
      source_code,
      strlen(source_code)
    );
    TSNode root_node = ts_tree_root_node(tree);
    sr_print_source(root_node, type, source_code, 0);
    ts_tree_delete(tree);
  } 
  else if (proj)
  {
    sr_walk_dir(proj, ".sql", parser, type, NULL, NULL);
  }

  ts_parser_delete(parser);

  return 0;
}
