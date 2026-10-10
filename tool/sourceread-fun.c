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

#include "sourceread.h"

static const char* const usages[] = 
{
  "sourceread-fun [options]",
  NULL,
};

int main(int argc, char *argv[]) 
{
  char* index_path = NULL;
  char* data_path = NULL;
  char* pattern = NULL;

  struct argparse_option options[] = {
    OPT_HELP(),
    OPT_STRING('i', "index_path", &index_path, "index file path", NULL, 0, 0),
    OPT_STRING('d', "data_path", &data_path, "data file path", NULL, 0, 0),
    OPT_STRING('p', "pattern", &pattern, "search pattern", NULL, 0, 0),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, options, usages, 0);
  argparse_describe(&argparse, "\nPrint function source matching pattern.", NULL);
  
  argc = argparse_parse(&argparse, argc, (const char**) argv);
  if (index_path != NULL && data_path == NULL || pattern == NULL)
  {
    argparse_usage(&argparse);
    return 1;
  }

  char* source = NULL;
  sr_search_source(index_path, data_path, pattern, &source); 
  printf("%s\n", source);
  free(source);
  return 0;
}
