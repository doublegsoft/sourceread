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
#include <argparse.h>
#include <tree_sitter/api.h>

#include "sourceread.h"
#include "sourceread-analysis.h"

extern const TSLanguage *tree_sitter_c(void);

static const char* const usages[] = 
{
  "sourceread-fun [options]",
  NULL,
};

int main(int argc, char *argv[]) 
{
  char* index_path = NULL;
  char* data_path = NULL;

  struct argparse_option options[] = {
    OPT_HELP(),
    OPT_STRING('i', "index_path", &index_path, "index file path", NULL, 0, 0),
    OPT_STRING('d', "data_path", &data_path, "data file path", NULL, 0, 0),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, options, usages, 0);
  argparse_describe(&argparse, "\nPrint function source matching pattern.", NULL);
  
  argc = argparse_parse(&argparse, argc, (const char**) argv);
  if (index_path != NULL && data_path == NULL)
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

  sr_find_standalone_utilities(parser, index_path, data_path);
  return 0;
}
