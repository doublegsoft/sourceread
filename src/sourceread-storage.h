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
#ifndef __SOURCEREAD_STORAGE_H__
#define __SOURCEREAD_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <tree_sitter/api.h>

int
sr_build_index(TSParser* parser,
               const char* index_path, 
               const char* data_path, 
               const char* proj_path,
               const char* file_exts,
               const char* node_type);

int
sr_search_source(const char* index_path, 
                 const char* pattern,
                 char** source);

#ifdef __cplusplus
}
#endif

#endif // __SOURCEREAD_STORAGE_H__