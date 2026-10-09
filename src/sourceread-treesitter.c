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
#include <dirent.h>
#include <sys/stat.h>

#include "sourceread-file.h"
#include "sourceread-treesitter.h"

/* ========================================================================== */
/*                                sr_print_source                             */
/* ========================================================================== */

void
sr_print_source(TSNode node, 
                const char* node_type, 
                const char* source_code, 
                int indent)
{
  if (ts_node_is_null(node)) return;

  const char* type = ts_node_type(node);
  uint32_t child_count = ts_node_child_count(node);

  if (strcmp(type, node_type) == 0)
  {
    char* raw = sr_node_text(node, source_code);
    if (!raw) return;
    char* formatted = sr_format_code(raw, indent);
    if (formatted)
    {
      printf("%s\n", formatted);
      free(formatted);
    }
    free(raw);
  }
  else 
  {
    for (uint32_t i = 0; i < child_count; i++) 
    {
      TSNode child = ts_node_child(node, i);
      sr_print_source(child, node_type, source_code, indent);
    }
  }
}

/* ========================================================================== */
/*                                 sr_node_text                               */
/* ========================================================================== */
char*
sr_node_text(TSNode node, const char* source_code)
{
  if (ts_node_is_null(node) || !source_code)
  {
    return NULL;
  }

  uint32_t start = ts_node_start_byte(node);
  uint32_t end   = ts_node_end_byte(node);

  if (end < start)
  {
    return NULL;
  }

  size_t len = end - start;
  char* text = (char*)malloc(len + 1);
  if (!text)
  {
    perror("malloc failed");
    return NULL;
  }

  memcpy(text, source_code + start, len);
  text[len] = '\0';

  return text;
}

void
sr_extract_methods(TSNode node, const char* source_code, const char* method_node_type)
{
  if (ts_node_is_null(node) || !source_code || !method_node_type)
  {
    return;
  }

  const char* type = ts_node_type(node);

  // 1. 匹配传入的目标节点类型
  if (strcmp(type, method_node_type) == 0)
  {
    // 尝试提取方法名：
    // 大部分语言使用 "name" 字段（如 Java、Python、Go、Rust），
    // C/C++ 等语言通常使用 "declarator" 字段。
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node))
    {
      name_node = ts_node_child_by_field_name(node, "declarator", 10);
    }

    char* method_name = sr_node_text(name_node, source_code);
    char* method_body = sr_node_text(node, source_code);
    if (method_body == NULL || strlen(method_body) == 0)
      return;

    printf("========================================\n");
    printf("[Type]: %s\n", method_node_type);
    printf("[Name]: %s\n", method_name ? method_name : "<anonymous>");
    printf("[Source Code]:\n%s\n", method_body ? method_body : "");
    printf("========================================\n\n");

    free(method_name);
    free(method_body);

    // 命中目标方法后，若不需要继续提取其内部嵌套的方法/闭包，直接返回；
    // 若需要解析内部嵌套函数，去掉 return 即可。
    return;
  }

  // 2. 递归遍历子节点
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++)
  {
    TSNode child = ts_node_child(node, i);
    sr_extract_methods(child, source_code, method_node_type);
  }
}