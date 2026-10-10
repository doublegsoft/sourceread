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
#ifndef __SOURCEREAD_ANALYSIS_H__
#define __SOURCEREAD_ANALYSIS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <tree_sitter/api.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                leaf / self-contained utility function analyzer             */
/* ========================================================================== */

/*!
** 字符串比较回调（用于 qsort / bsearch）。
*/
static inline int
sr_str_compare_cb(const void* a, const void* b)
{
  const char* s1 = *(const char* const*)a;
  const char* s2 = *(const char* const*)b;
  return strcmp(s1, s2);
}

/*!
** 纯内存快速二分检索：检查某个被调用的函数名是否属于“本项目自定义函数”。
**
** @param sorted_names 已按字母序排好序的项目所有函数名数组
** @param count        函数总数
** @param callee_name  被调用的函数名称
**
** @return bool        若在项目函数列表中找到返回 true，否则（系统库函数/外部API）返回 false
*/
static inline bool
sr_is_internal_project_func(char* const* sorted_names, size_t count, const char* callee_name)
{
  if (!sorted_names || count == 0 || !callee_name) return false;

  const char* key = callee_name;
  void* found = bsearch(&key, sorted_names, count, sizeof(char*), sr_str_compare_cb);
  return (found != NULL);
}

/*!
** 递归扫描 AST，检查当前函数体内部是否存在对工程内部自定义函数的依赖。
**
** @param node              当前 AST 语法节点
** @param source_code       当前函数的完整源代码文本
** @param current_func_name 当前函数自身名称（允许自递归）
** @param sorted_names      已排序的项目全部函数名内存列表
** @param total_funcs       函数总数
** @param out_bad_callee    [传出参数] 若命中内部依赖，记录所依赖的内部函数名
**
** @return bool             依赖了自定义函数返回 true；完全无依赖/仅依赖系统函数返回 false
*/
static inline bool
sr_check_has_internal_dep(TSNode node, 
                          const char* source_code, 
                          const char* current_func_name,
                          char* const* sorted_names, 
                          size_t total_funcs,
                          char** out_bad_callee)
{
  if (ts_node_is_null(node)) return false;

  const char* type = ts_node_type(node);

  /* 捕获函数调用表达式：如 foo(...) */
  if (strcmp(type, "call_expression") == 0)
  {
    TSNode fn_node = ts_node_child_by_field_name(node, "function", 8);
    if (!ts_node_is_null(fn_node))
    {
      const char* fn_type = ts_node_type(fn_node);

      /* 普通函数调用：直接标识符 identifier */
      if (strcmp(fn_type, "identifier") == 0)
      {
        uint32_t start = ts_node_start_byte(fn_node);
        uint32_t end = ts_node_end_byte(fn_node);
        uint32_t len = end - start;

        char callee[256];
        if (len < sizeof(callee))
        {
          memcpy(callee, source_code + start, len);
          callee[len] = '\0';

          /* 允许自递归：自己调用自己不属于外部依赖 */
          if (strcmp(callee, current_func_name) != 0)
          {
            /* 纯内存极速二分判定是否属于本项目内部函数 */
            if (sr_is_internal_project_func(sorted_names, total_funcs, callee))
            {
              if (out_bad_callee && !*out_bad_callee)
              {
                *out_bad_callee = strdup(callee);
              }
              return true; /* 命中项目内部依赖 */
            }
          }
        }
      }
    }
  }

  /* 深度优先递归检查所有子语法节点 */
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++)
  {
    TSNode child = ts_node_child(node, i);
    if (sr_check_has_internal_dep(child, source_code, current_func_name, 
                                  sorted_names, total_funcs, out_bad_callee))
    {
      return true;
    }
  }

  return false;
}

/*!
** 检索并列出项目中所有“零工程内部依赖”的纯工具类函数。
**
** 核心逻辑：
** 1. 启动时一次性加载项目所有函数名并排序（纯内存，避免任何文件游标错位问题）；
** 2. 基于跳跃表 doc_offsets 稳定定位并读取每个函数源码，杜绝循环提前中断；
** 3. 利用 Tree-sitter 分析 call_expression，凡是调用的函数不在项目列表中，
**    均视为外部系统/libc函数；
** 4. 统计并打印所有零内部依赖的独立函数。
**
** @param parser      已初始化的 Tree-sitter C 语言解析器
** @param index_path  .srix 索引文件路径
** @param data_path   .dat 数据文件路径
**
** @return int 成功识别并打印的数量
*/
static inline int
sr_find_standalone_utilities(TSParser* parser, 
                             const char* index_path, 
                             const char* data_path)
{
  if (!parser || !index_path || !data_path) return -1;

  FILE* idx_fp = fopen(index_path, "rb");
  if (!idx_fp) {
    fprintf(stderr, "[Error] Failed to open index file: %s\n", index_path);
    return -1;
  }

  sr_srix_hdr_t hdr;
  if (fread(&hdr, sizeof(hdr), 1, idx_fp) != 1 || hdr.magic != SRIX_MAGIC) {
    fprintf(stderr, "[Error] Header magic mismatch in %s\n", index_path);
    fclose(idx_fp);
    return -1;
  }

  if (hdr.total_docs == 0) {
    fclose(idx_fp);
    printf("Index is empty (0 functions).\n");
    return 0;
  }

  FILE* data_fp = fopen(data_path, "rb");
  if (!data_fp) {
    fprintf(stderr, "[Error] Failed to open data file: %s\n", data_path);
    fclose(idx_fp);
    return -1;
  }

  /* 1. 将文档跳跃表 doc_offsets 加载到内存，实现稳定的绝对寻址 */
  uint64_t* doc_offsets = (uint64_t*)malloc(sizeof(uint64_t) * hdr.total_docs);
  fseek(idx_fp, (long)hdr.docs_offset, SEEK_SET);
  fread(doc_offsets, sizeof(uint64_t), hdr.total_docs, idx_fp);

  /* 2. 将所有项目的函数名提取到内存，构建全局项目符号词典 (纯内存，零 I/O 干扰) */
  char** project_funcs = (char**)malloc(sizeof(char*) * hdr.total_docs);
  for (uint64_t i = 0; i < hdr.total_docs; i++)
  {
    fseek(idx_fp, (long)doc_offsets[i], SEEK_SET);
    sr_doc_rec_hdr_t dhdr;
    fread(&dhdr, sizeof(dhdr), 1, idx_fp);

    project_funcs[i] = (char*)malloc(dhdr.name_len + 1);
    fread(project_funcs[i], sizeof(char), dhdr.name_len, idx_fp);
    project_funcs[i][dhdr.name_len] = '\0';
  }

  /* 对函数名数组排序，以便后续 bsearch 能够以 O(log N) 快速查找 */
  qsort(project_funcs, hdr.total_docs, sizeof(char*), sr_str_compare_cb);

  printf("=====================================================================\n");
  printf("  Scanning Standalone Utility Functions (Total Functions: %llu)\n", 
         (unsigned long long)hdr.total_docs);
  printf("=====================================================================\n");

  uint32_t standalone_count = 0;

  /* 3. 使用跳跃表绝对定位，逐个分析每个函数（绝不会被内层查询破坏游标） */
  for (uint64_t doc_id = 0; doc_id < hdr.total_docs; doc_id++)
  {
    fseek(idx_fp, (long)doc_offsets[doc_id], SEEK_SET);
    sr_doc_rec_hdr_t dhdr;
    fread(&dhdr, sizeof(dhdr), 1, idx_fp);

    char name_buf[256];
    if (dhdr.name_len >= sizeof(name_buf)) continue;
    fread(name_buf, sizeof(char), dhdr.name_len, idx_fp);
    name_buf[dhdr.name_len] = '\0';

    /* 读取该函数的源码切片 */
    if (dhdr.end_byte < dhdr.start_byte) continue;
    size_t code_len = (size_t)(dhdr.end_byte - dhdr.start_byte);
    char* code = (char*)malloc(code_len + 1);
    fseek(data_fp, (long)dhdr.start_byte, SEEK_SET);
    fread(code, 1, code_len, data_fp);
    code[code_len] = '\0';

    /* 4. Tree-sitter AST 静态依赖扫描 */
    TSTree* tree = ts_parser_parse_string(parser, NULL, code, code_len);
    TSNode root_node = ts_tree_root_node(tree);

    char* bad_callee = NULL;
    bool has_internal_dep = sr_check_has_internal_dep(
      root_node, code, name_buf, project_funcs, hdr.total_docs, &bad_callee
    );

    /* 5. 命中：零工程内部依赖的独立工具函数 */
    if (!has_internal_dep)
    {
      standalone_count++;
      printf("\n[%u] STANDALONE UTILITY: %s (Doc ID: %llu)\n", 
             standalone_count, name_buf, (unsigned long long)doc_id);
      printf("    Characteristics: ZERO Project Dependencies (Only libc/syscalls used)\n");
      printf("------------------------ SOURCE CODE ------------------------\n");
      printf("%s\n", code);
      printf("-------------------------------------------------------------\n");
    }
    else
    {
      free(bad_callee);
    }

    ts_tree_delete(tree);
    free(code);
  }

  /* 6. 清理内存资源 */
  for (uint64_t i = 0; i < hdr.total_docs; i++) {
    free(project_funcs[i]);
  }
  free(project_funcs);
  free(doc_offsets);

  fclose(idx_fp);
  fclose(data_fp);

  printf("\nAnalysis Completed! Found %u Pure Standalone Utility Functions.\n", standalone_count);
  return (int)standalone_count;
}

#ifdef __cplusplus
}
#endif

#endif /* __SOURCEREAD_ANALYSIS_H__ */