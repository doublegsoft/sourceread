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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "sourceread-error.h"
#include "sourceread-storage.h"
#include "sourceread-file.h"
#include "sourceread-treesitter.h"

#define SRIX_MAGIC          0x58495253 /* ASCII for "SRIX" */
#define SRIX_VERSION        1
#define HASH_BUCKET_SIZE    4096

/* ========================================================================== */
/*                            file layout structs                             */
/* ========================================================================== */

#pragma pack(push, 1)

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint64_t file_size;
  uint64_t total_trigrams;
  uint64_t total_docs;
  uint64_t dict_offset;
  uint64_t posts_offset;
  uint64_t docs_offset;
  uint8_t  reserved[8];
} sr_srix_hdr_t;

typedef struct {
  char     trigram_key[3];
  uint8_t  reserved_pad;
  uint32_t doc_count;
  uint64_t posts_offset;
} sr_dict_rec_t;

typedef struct {
  uint16_t name_len;
  uint64_t start_byte;
  uint64_t end_byte;
} sr_doc_rec_hdr_t;

#pragma pack(pop)

/* ========================================================================== */
/*                        in-memory builder structs                           */
/* ========================================================================== */

typedef struct {
  char* name;
  uint64_t start_byte;
  uint64_t end_byte;
} sr_mem_doc_t;

typedef struct {
  uint32_t* doc_ids;
  size_t count;
  size_t capacity;
} sr_mem_post_t;

typedef struct srix_node {
  char key[3];
  sr_mem_post_t posts;
  struct srix_node* next;
} sr_node_t;

typedef struct {
  sr_mem_doc_t* docs;
  size_t doc_count;
  size_t doc_capacity;
  sr_node_t* buckets[HASH_BUCKET_SIZE];
  size_t unique_trigrams;
} sr_builder_t;

/*!
** 语法树遍历与数据落盘上下文结构体。
**
** 包含索引构建器指针以及专门用于追加存储函数源码片段的目标数据文件句柄。
*/
typedef struct {
  sr_builder_t* builder;  /* 索引构建器指针 */
  FILE* data_fp;          /* 存放抽取源码片段的 .dat 数据文件指针 */
} sr_indexer_ctx_t;


/* ========================================================================== */
/*                         helpers: hash & posts                           */
/* ========================================================================== */

static uint32_t
sr_hash(const char key[3])
{
  uint32_t h = 5381;
  h = ((h << 5) + h) + (uint8_t)key[0];
  h = ((h << 5) + h) + (uint8_t)key[1];
  h = ((h << 5) + h) + (uint8_t)key[2];
  return h % HASH_BUCKET_SIZE;
}

static void
sr_add_posts(sr_mem_post_t* p, uint32_t doc_id)
{
  /* 避免同一个函数因重复三元组而多次记录相同的 doc_id */
  if (p->count > 0 && p->doc_ids[p->count - 1] == doc_id)
  {
    return;
  }
  if (p->count >= p->capacity)
  {
    p->capacity = (p->capacity == 0) ? 4 : p->capacity * 2;
    p->doc_ids = (uint32_t*)realloc(p->doc_ids, p->capacity * sizeof(uint32_t));
  }
  p->doc_ids[p->count++] = doc_id;
}

/* ========================================================================== */
/*                        builder lifecycle & indexing                        */
/* ========================================================================== */

/*!
** 创建并初始化一个空的三元组索引构建器 (sr_builder_t) 容器。
**
** 使用 calloc 进行内存分配，确保所有字段与哈希桶指针自动归零初始化。
**
** @return 返回新创建的构建器指针；若堆内存分配失败则返回 NULL。
*/
sr_builder_t*
sr_builder_new(void)
{
  sr_builder_t* b = (sr_builder_t*)calloc(1, sizeof(sr_builder_t));
  return b;
}

/*!
** 向内存构建器的哈希表中注册一个 3 字符三元组 (Trigram)，并追加关联的文档 ID。
**
** 算法流程：
** 1. 计算三元组的哈希值，定位到目标哈希桶；
** 2. 遍历拉链冲突链表，若该三元组已存在，直接调用 sr_add_posts 将 doc_id 追加到倒排列表中；
** 3. 若未命中，则新建节点，保存 3 字节 Key，初始化倒排列表，并以头插法挂入哈希桶，
**    同时递增全局唯一三元组计数器 (unique_trigrams)。
**
** @param b       索引构建器指针。
** @param key     定长 3 字节的三元组字符数组（非空，无需以 '\0' 结尾）。
** @param doc_id  产生该三元组的文档/函数唯一标识编号。
*/
static void
sr_builder_add_ngram(sr_builder_t* b, const char key[3], uint32_t doc_id)
{
  uint32_t bucket = sr_hash(key);
  sr_node_t* node = b->buckets[bucket];

  while (node)
  {
    if (memcmp(node->key, key, 3) == 0)
    {
      sr_add_posts(&node->posts, doc_id);
      return;
    }
    node = node->next;
  }

  node = (sr_node_t*)malloc(sizeof(sr_node_t));
  memcpy(node->key, key, 3);
  node->posts.doc_ids = NULL;
  node->posts.count = 0;
  node->posts.capacity = 0;
  sr_add_posts(&node->posts, doc_id);

  node->next = b->buckets[bucket];
  b->buckets[bucket] = node;
  b->unique_trigrams++;
}

/*!
** 向构建器中登记一个完整文档（函数符号），并自动切分三元组建立倒排索引。
**
** 执行逻辑：
** 1. 动态扩容并记录文档实体元数据（函数名副本、源文件起始及结束字节位置）；
** 2. 生成递增分配的全局唯一 doc_id；
** 3. 采用长度为 3 的滑动窗口线性切分函数名，统一转换为小写字符，
**    将生成的所有三元组逐个写入倒排索引哈希表。
**
** @param b           索引构建器指针。
** @param name        函数/符号的名称字符串（以 '\0' 结尾）。
** @param start_byte  函数在源代码文件中的起始字节偏移量。
** @param end_byte    函数在源代码文件中的结束字节偏移量。
*/
void
sr_builder_add_doc(
  sr_builder_t* b, 
  const char* name, 
  uint64_t start_byte, 
  uint64_t end_byte
) {
  if (b->doc_count >= b->doc_capacity)
  {
    b->doc_capacity = (b->doc_capacity == 0) ? 8 : b->doc_capacity * 2;
    b->docs = (sr_mem_doc_t*)realloc(b->docs, b->doc_capacity * sizeof(sr_mem_doc_t));
  }

  uint32_t doc_id = (uint32_t)b->doc_count++;
  b->docs[doc_id].name = strdup(name);
  b->docs[doc_id].start_byte = start_byte;
  b->docs[doc_id].end_byte = end_byte;

  /* 提取 Trigrams: 滑动 3 字符窗口 */
  size_t len = strlen(name);
  if (len < 3) return;

  for (size_t i = 0; i <= len - 3; i++)
  {
    char gram[3];
    gram[0] = (char)tolower((unsigned char)name[i]);
    gram[1] = (char)tolower((unsigned char)name[i + 1]);
    gram[2] = (char)tolower((unsigned char)name[i + 2]);
    sr_builder_add_ngram(b, gram, doc_id);
  }
}

/*!
** 释放索引内存构建器 (sr_builder_t) 及其深层嵌套所占用的全部动态堆内存。
**
** 逐层清理包括：
** 1. 各个已缓存文档记录对应的动态字符串名称以及文档数组本身；
** 2. 全部哈希桶链表节点，以及每个节点所包含的 doc_ids 倒排数组；
** 3. 构建器控制块本体指针。
**
** @param b 待销毁的构建器对象指针（支持传入 NULL，此时直接安全返回）。
*/
static void
sr_builder_free(sr_builder_t* b)
{
  if (!b) return;
  for (size_t i = 0; i < b->doc_count; i++) free(b->docs[i].name);
  free(b->docs);

  for (size_t i = 0; i < HASH_BUCKET_SIZE; i++)
  {
    sr_node_t* cur = b->buckets[i];
    while (cur)
    {
      sr_node_t* next = cur->next;
      free(cur->posts.doc_ids);
      free(cur);
      cur = next;
    }
  }
  free(b);
}

/* ========================================================================== */
/*                       file serialization (saving)                          */
/* ========================================================================== */

/*!
** 三元组字典条目的排序比对回调函数（用于 qsort）。
**
** 按照 3 字节的 trigram_key 进行内存字节序比对，确保输出到文件的字典表
** 具备全局升序特性，以便后续通过二分查找 (Binary Search) 在 O(log N) 内检索。
**
** @param a  待比对的第一个 sr_dict_rec_t 记录指针。
** @param b  待比对的第二个 sr_dict_rec_t 记录指针。
** @return   根据字节序返回小于 0、等于 0 或大于 0 的整型差值。
*/
static int
sr_compare_dict(const void* a, const void* b)
{
  const sr_dict_rec_t* r1 = (const sr_dict_rec_t*)a;
  const sr_dict_rec_t* r2 = (const sr_dict_rec_t*)b;
  return memcmp(r1->trigram_key, r2->trigram_key, 3);
}

/*!
** 将内存中构建的三元组索引结构序列化并持久化到指定的二进制文件中。
**
** 文件布局排布逻辑：
** 1. Section 1 (Header): 写入总控文件头，包含魔数、版本及各段的起始绝对文件偏移量。
** 2. Section 2 (Dictionary): 汇集哈希桶中的所有三元组，经 qsort 全局排序后，
**    紧凑写入定长的字典记录项，每一项都预先记录其对应倒排列表的物理偏移。
** 3. Section 3 (Postings Arena): 顺序写入每个三元组名下命中记录的全部文档 ID 数组 (uint32_t)。
** 4. Section 4 (Documents Directory): 写入文档条目区，包含用于快速寻址的文档偏移跳跃表 (doc_offsets)
**    以及包含函数名、源码起止位置的变长真实元数据。
** 5. 回填更新：最后重新 seek 到文件头部，回填总文件大小及文档偏移表，完成持久化。
**
** @param b         包含已解析文档与全部三元组的内存构建器指针。
** @param filepath  目标输出持久化文件的文件路径。
** @return          序列化写入成功返回 true；文件创建或写入失败返回 false。
*/
static bool
sr_save_file(sr_builder_t* b, const char* filepath)
{
  FILE* fp = fopen(filepath, "wb");
  if (!fp) return false;

  /* 收集字典节点并排序以支持二进制检索 (Binary Search) */
  sr_dict_rec_t* dict = (sr_dict_rec_t*)malloc(
    sizeof(sr_dict_rec_t) * b->unique_trigrams
  );
  sr_node_t** node_ptrs = (sr_node_t**)malloc(
    sizeof(sr_node_t*) * b->unique_trigrams
  );

  size_t idx = 0;
  for (size_t i = 0; i < HASH_BUCKET_SIZE; i++)
  {
    sr_node_t* cur = b->buckets[i];
    while (cur)
    {
      memcpy(dict[idx].trigram_key, cur->key, 3);
      dict[idx].reserved_pad = 0;
      dict[idx].doc_count = (uint32_t)cur->posts.count;
      dict[idx].posts_offset = 0;
      node_ptrs[idx] = cur;
      idx++;
      cur = cur->next;
    }
  }

  qsort(dict, b->unique_trigrams, sizeof(sr_dict_rec_t), sr_compare_dict);

  /* 计算各段偏移 */
  uint64_t dict_offset = sizeof(sr_srix_hdr_t);
  uint64_t posts_offset = dict_offset + (sizeof(sr_dict_rec_t) * b->unique_trigrams);

  uint64_t cur_post_off = posts_offset;
  for (size_t i = 0; i < b->unique_trigrams; i++)
  {
    dict[i].posts_offset = cur_post_off;
    cur_post_off += dict[i].doc_count * sizeof(uint32_t);
  }

  uint64_t docs_offset = cur_post_off;

  /* 1. 写入 Header */
  sr_srix_hdr_t hdr = {0};
  hdr.magic = SRIX_MAGIC;
  hdr.version = SRIX_VERSION;
  hdr.total_trigrams = b->unique_trigrams;
  hdr.total_docs = b->doc_count;
  hdr.dict_offset = dict_offset;
  hdr.posts_offset = posts_offset;
  hdr.docs_offset = docs_offset;
  fwrite(&hdr, sizeof(hdr), 1, fp);

  /* 2. 写入 Section 2: Trigram 字典表 */
  fwrite(dict, sizeof(sr_dict_rec_t), b->unique_trigrams, fp);

  /* 3. 写入 Section 3: posts 命中倒排链 */
  for (size_t i = 0; i < b->unique_trigrams; i++)
  {
    for (size_t j = 0; j < b->unique_trigrams; j++)
    {
      if (memcmp(dict[i].trigram_key, node_ptrs[j]->key, 3) == 0)
      {
        fwrite(node_ptrs[j]->posts.doc_ids, sizeof(uint32_t), dict[i].doc_count, fp);
        break;
      }
    }
  }

  /* 4. 写入 Section 4: Document 文档表 (含前导偏移表) */
  uint64_t* doc_offsets = (uint64_t*)malloc(sizeof(uint64_t) * b->doc_count);
  long doc_table_pos = ftell(fp);
  fwrite(doc_offsets, sizeof(uint64_t), b->doc_count, fp); /* 占位 */

  for (size_t i = 0; i < b->doc_count; i++)
  {
    doc_offsets[i] = (uint64_t)ftell(fp);
    sr_doc_rec_hdr_t dhdr;
    dhdr.name_len = (uint16_t)strlen(b->docs[i].name);
    dhdr.start_byte = b->docs[i].start_byte;
    dhdr.end_byte = b->docs[i].end_byte;

    fwrite(&dhdr, sizeof(dhdr), 1, fp);
    fwrite(b->docs[i].name, sizeof(char), dhdr.name_len, fp);
  }

  /* 回填 doc_offsets */
  long end_pos = ftell(fp);
  fseek(fp, doc_table_pos, SEEK_SET);
  fwrite(doc_offsets, sizeof(uint64_t), b->doc_count, fp);

  /* 回填总大小 */
  hdr.file_size = (uint64_t)end_pos;
  fseek(fp, 0, SEEK_SET);
  fwrite(&hdr, sizeof(hdr), 1, fp);

  free(dict);
  free(node_ptrs);
  free(doc_offsets);
  fclose(fp);
  return true;
}

/* ========================================================================== */
/*                           query execution engine                           */
/* ========================================================================== */

/*!
** 在二进制索引文件中，通过二分查找定位指定的 3 字符三元组 (Trigram)，并读取其对应的倒排列表 (Postings)。
**
** 算法原理：
** 1. 索引文件中的字典段 (Dictionary Section) 是定长记录且按字节序严格升序排列的；
** 2. 利用二分查找 (Binary Search) 在 O(log N) 次文件 Seek 内精确定位到目标三元组；
** 3. 命中后，读取其在倒排区 (Postings Arena) 的文件偏移与文档计数，并分配内存加载全部命中的 Document ID。
**
** @param fp         已打开的 SRIX 二进制索引文件指针。
** @param hdr        索引文件头元数据指针，包含字典段的起始偏移与三元组总数。
** @param gram       待查找的 3 字节三元组字符数组（无需以 '\0' 结尾）。
** @param out_list   输出参数：查找到时指向新分配的 Doc ID 数组，调用方需负责 free()。
** @param out_count  输出参数：命中包含该三元组的文档总数。
** @return           成功找到并成功加载倒排列表返回 true；未找到或读取失败返回 false。
*/
static bool
sr_find_posts_for_gram(
  FILE* fp, 
  const sr_srix_hdr_t* hdr, 
  const char gram[3], 
  uint32_t** out_list, 
  uint32_t* out_count
) {
  int64_t low = 0;
  int64_t high = (int64_t)hdr->total_trigrams - 1;

  while (low <= high)
  {
    int64_t mid = low + (high - low) / 2;
    fseek(fp, (long)(hdr->dict_offset + mid * sizeof(sr_dict_rec_t)), SEEK_SET);

    sr_dict_rec_t rec;
    if (fread(&rec, sizeof(rec), 1, fp) != 1) break;

    int cmp = memcmp(gram, rec.trigram_key, 3);
    if (cmp == 0)
    {
      *out_count = rec.doc_count;
      *out_list = (uint32_t*)malloc(rec.doc_count * sizeof(uint32_t));
      fseek(fp, (long)rec.posts_offset, SEEK_SET);
      fread(*out_list, sizeof(uint32_t), rec.doc_count, fp);
      return true;
    }
    else if (cmp < 0)
    {
      high = mid - 1;
    }
    else
    {
      low = mid + 1;
    }
  }
  return false;
}

/*!
** 计算两个升序排列的 Document ID 倒排数组的交集（即逻辑 AND 运算）。
**
** 算法原理：
** 基于双指针法 (Two-Pointer Algorithm) 同时线性扫描两个升序数组：
** - 当 a[i] == b[j] 时，两边同时命中，计入结果集合，双指针同步后移；
** - 当 a[i] < b[j] 时，指针 i 递增；
** - 当 a[i] > b[j] 时，指针 j 递增；
** 整体时间复杂度为 O(len_a + len_b)，极大优于嵌套循环或哈希集合方案。
**
** @param a        第一个升序 Doc ID 数组指针。
** @param len_a    数组 a 的元素个数。
** @param b        第二个升序 Doc ID 数组指针。
** @param len_b    数组 b 的元素个数。
** @param out_len  输出参数：最终交集结果数组中的有效元素个数。
** @return         返回新分配的交集数组指针（最大可能占用 len_a * sizeof(uint32_t) 字节），
**                 调用方需负责 free()。
*/
static uint32_t*
sr_intersect_posts(
  const uint32_t* a, 
  uint32_t len_a, 
  const uint32_t* b, 
  uint32_t len_b, 
  uint32_t* out_len
) {
  uint32_t* res = (uint32_t*)malloc(len_a * sizeof(uint32_t));
  uint32_t i = 0, j = 0, k = 0;

  while (i < len_a && j < len_b)
  {
    if (a[i] == b[j])
    {
      res[k++] = a[i];
      i++;
      j++;
    }
    else if (a[i] < b[j])
    {
      i++;
    }
    else
    {
      j++;
    }
  }

  *out_len = k;
  return res;
}

/*!
** 从 Tree-sitter C 语言的函数定义节点中精准提取纯函数名称。
**
** 兼容语法特例：
** - 普通函数:      void foo() {}
** - 指针返回类型:   char* bar() {} (内部包裹 pointer_declarator)
** - 复杂修饰声明:   static inline const char* baz() {}
**
** 算法原理：
** 从 function_definition 节点的 "declarator" 属性开始向下解包，
** 穿透 pointer_declarator、parenthesized_declarator、function_declarator，
** 直到命中终结符类型 "identifier"。
**
** @param node         Tree-sitter 节点（类型需为 "function_definition"）。
** @param source_code  包含完整源代码文本的字符串缓冲区。
** @return             返回新分配的函数名称字符串（调用方需 free()）；
**                     若非函数节点或提取失败则返回 NULL。
*/
static char*
sr_fun_name(TSNode node, const char* source_code)
{
  if (ts_node_is_null(node) || !source_code) return NULL;

  /* 仅针对函数定义节点进行解析 */
  if (strcmp(ts_node_type(node), "function_definition") != 0)
  {
    return NULL;
  }

  /* 获取声明体声明符 (declarator) */
  TSNode decl = ts_node_child_by_field_name(node, "declarator", 10);

  /* 向下穿透解包，直到找到真正的 identifier 节点 */
  while (!ts_node_is_null(decl))
  {
    const char* type = ts_node_type(decl);

    if (strcmp(type, "identifier") == 0)
    {
      /* 命中目标函数名标识符 */
      uint32_t start = ts_node_start_byte(decl);
      uint32_t end = ts_node_end_byte(decl);
      uint32_t len = end - start;

      char* name = (char*)malloc(len + 1);
      if (!name) return NULL;

      memcpy(name, source_code + start, len);
      name[len] = '\0';
      return name;
    }

    /* 常见的中间嵌套包装层：function_declarator 或 pointer_declarator */
    TSNode inner = ts_node_child_by_field_name(decl, "declarator", 10);
    if (!ts_node_is_null(inner))
    {
      decl = inner;
    }
    else
    {
      /* 若无 field_name，尝试读取第一个有效的子节点 */
      uint32_t count = ts_node_child_count(decl);
      bool advanced = false;
      for (uint32_t i = 0; i < count; i++)
      {
        TSNode child = ts_node_child(decl, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "identifier") == 0 ||
            strcmp(child_type, "function_declarator") == 0 ||
            strcmp(child_type, "pointer_declarator") == 0)
        {
          decl = child;
          advanced = true;
          break;
        }
      }
      if (!advanced) break;
    }
  }

  return NULL;
}

/*!
** 递归遍历 Tree-sitter 语法树节点，匹配目标语法实体并注册到索引构建器中。
**
** 算法流程：
** 1. 递归基校验：检查当前节点是否为空 (ts_node_is_null)；
** 2. 节点类型比对：若当前节点类型与传入的目标类型 (node_type，例如 "function_definition") 匹配：
**    - 获取该节点在原始源文件中的起止字节偏移量 [start_byte, end_byte]；
**    - 提取纯函数/实体名称 (sr_fun_name)；
**    - 若成功提取名称，将其作为文档实体登记到三元组索引构建器 (sr_builder_add_doc)；
**    - 释放临时分配的函数名字符串堆内存；
** 3. 递归下降：若当前节点类型不匹配，则深度优先遍历其全部子节点继续查找。
**
** @param builder      三元组索引内存构建器指针，用于持久化或累积符号信息。
** @param node         当前递归访问的 Tree-sitter 语法树节点。
** @param node_type    需要捕获的目标语法节点类型名称（如 "function_definition"）。
** @param source_code  对应的完整原始源代码字符串缓冲区。
** @param indent       预留排版缩进层级参数。
*/
static void
sr_visit_node(sr_indexer_ctx_t* ctx, 
              TSNode node, 
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

    uint64_t data_start = (uint64_t)ftell(ctx->data_fp);
    
    char* fmt_src = sr_format_code(raw, 2);
    free(raw);

    size_t fmt_len = strlen(fmt_src);
    fwrite(fmt_src, sizeof(char), fmt_len, ctx->data_fp);
    fputc('\n', ctx->data_fp); /* 添加换行符作为分隔 */
    uint64_t data_end = (uint64_t)ftell(ctx->data_fp);
    char* fn = sr_fun_name(node, source_code);
    if (fn)
    {
      sr_builder_add_doc(ctx->builder, fn, data_start, data_end);
      free(fn);
    }
    free(fmt_src);
  }
  else 
  {
    for (uint32_t i = 0; i < child_count; i++) 
    {
      TSNode child = ts_node_child(node, i);
      sr_visit_node(ctx, child, node_type, source_code, indent);
    }
  }
}

/*!
** 读取并解析单个源文件，构建完整抽象语法树 (AST) 并提取符号索引。
**
** 执行逻辑：
** 1. 将文件内容完整读入内存缓冲区；
** 2. 调用 Tree-sitter 解析器将源文本解析为语法树 (TSTree)；
** 3. 从根节点 (root_node) 开始深度优先递归扫描，捕获匹配 nodetype 的目标节点；
** 4. 析构并释放语法树以及源代码文本占用的堆内存，防止内存泄漏。
**
** @param parser    已初始化且配置好目标语言的 Tree-sitter 解析器句柄。
** @param nodetype  需要扫描提取的目标 AST 节点类型（如 "function_definition"）。
** @param filepath  目标源文件的本地磁盘路径。
** @param userdata  用户自定义上下文指针（此处传入 sr_builder_t* 索引构建器）。
*/
static void
sr_visit_file(TSParser* parser, const char* nodetype, const char* filepath, void* userdata)
{
  sr_indexer_ctx_t* ctx = (sr_indexer_ctx_t*)userdata;

  char* source_code = sr_read_file(filepath);
  TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
  TSNode root_node = ts_tree_root_node(tree);
  sr_visit_node(ctx, root_node, nodetype, source_code, 2);
  free(source_code);
  ts_tree_delete(tree);
}

int
sr_build_index(TSParser* parser,
               const char* index_path, 
               const char* data_path, 
               const char* proj_path,
               const char* file_exts,
               const char* node_type)
{
  FILE* data_fp = fopen(data_path, "wb");
  if (!data_fp) return 1;
  sr_builder_t* builder = sr_builder_new();
  sr_indexer_ctx_t ctx = {
    .builder = builder,
    .data_fp = data_fp
  };
  sr_walk_dir(proj_path, file_exts, parser, node_type, sr_visit_file, &ctx);
  sr_save_file(builder, index_path);
  fflush(data_fp);
  fclose(data_fp);
  sr_builder_free(builder);
  return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

/*!
** 检索并打印所有匹配 pattern 的函数名及其源码片段。
**
** 算法与改造点：
** 1. 采用 Trigram 求交过滤出候选 Doc 集合；
** 2. 打开 .dat 数据文件并保持句柄打开状态，遍历全部 candidate_docs；
** 3. 移除单次命中的 break 逻辑，支持打印出全部满足模糊/子串匹配的函数；
** 4. 统计总命中数 (match_count)；
** 5. 若有命中，将首个/聚合匹配的源码通过 *source 传出供调用者使用。
**
** @param index_path .srix 二进制索引文件路径
** @param data_path  .dat 源码数据文件路径
** @param pattern    待检索的函数名关键字 (>= 3字符)
** @param source     [输出参数] 传出查找到的源码内容 (调用方需负责 free)
**
** @return int 错误码 (SR_SUCCESS 为成功，无匹配返回 SR_ERR_SEARCH_MATCH_NOT_FOUND)
*/
int
sr_search_source(const char* index_path, 
                 const char* data_path, 
                 const char* pattern,
                 char** source)
{
  if (!index_path || !data_path || !pattern || !source) {
    return SR_ERR_COMM_PARAM_NULL;
  }
  *source = NULL;

  size_t plen = strlen(pattern);
  if (plen < 3) {
    /* Trigram 引擎至少需要 3 个字符建立滑动窗口 */
    return SR_ERR_SEARCH_QUERY_TOO_SHORT;
  }

  /* 1. 打开索引文件并校验 Header 魔数 */
  FILE* idx_fp = fopen(index_path, "rb");
  if (!idx_fp) {
    return SR_ERR_FILE_HANDLER_OPEN;
  }

  sr_srix_hdr_t hdr;
  if (fread(&hdr, sizeof(hdr), 1, idx_fp) != 1 || hdr.magic != SRIX_MAGIC) {
    fclose(idx_fp);
    return SR_ERR_INDEX_HDR_MAGIC_MISMATCH;
  }

  /* 2. Trigram 检索及多倒排链求交集 (AND 操作) */
  uint32_t* candidate_docs = NULL;
  uint32_t candidate_count = 0;

  for (size_t i = 0; i <= plen - 3; i++) {
    char gram[3];
    gram[0] = (char)tolower((unsigned char)pattern[i]);
    gram[1] = (char)tolower((unsigned char)pattern[i + 1]);
    gram[2] = (char)tolower((unsigned char)pattern[i + 2]);

    uint32_t* current_list = NULL;
    uint32_t current_count = 0;

    /* 在 Section 2 (Dictionary) 进行二分查找 */
    if (!sr_find_posts_for_gram(idx_fp, &hdr, gram, &current_list, &current_count)) {
      free(candidate_docs);
      fclose(idx_fp);
      return SR_ERR_SEARCH_MATCH_NOT_FOUND; /* 某个三元组不存在，说明无匹配项 */
    }

    if (i == 0) {
      candidate_docs = current_list;
      candidate_count = current_count;
    } else {
      uint32_t next_len = 0;
      uint32_t* next_docs = sr_intersect_posts(
        candidate_docs, candidate_count, current_list, current_count, &next_len
      );
      free(candidate_docs);
      free(current_list);
      candidate_docs = next_docs;
      candidate_count = next_len;
    }

    if (candidate_count == 0) {
      free(candidate_docs);
      fclose(idx_fp);
      return SR_ERR_SEARCH_MATCH_NOT_FOUND;
    }
  }

  /* 3. 准备打开数据文件，开始遍历所有候选文档 */
  FILE* data_fp = fopen(data_path, "rb");
  if (!data_fp) {
    free(candidate_docs);
    fclose(idx_fp);
    return SR_ERR_FILE_HANDLER_OPEN;
  }

  uint32_t match_count = 0;
  char* first_matched_code = NULL;

  printf("\n=======================================================\n");
  printf("  Search Results for Pattern: '%s' (Candidates: %u)\n", pattern, candidate_count);
  printf("=======================================================\n");

  for (uint32_t i = 0; i < candidate_count; i++) {
    uint32_t doc_id = candidate_docs[i];

    /* 定位到 Section 4 的跳跃表获取文档条目的绝对偏移量 */
    if (fseek(idx_fp, (long)(hdr.docs_offset + doc_id * sizeof(uint64_t)), SEEK_SET) != 0) {
      continue;
    }
    uint64_t doc_rec_offset = 0;
    if (fread(&doc_rec_offset, sizeof(uint64_t), 1, idx_fp) != 1) continue;

    /* 读取文档头 (包含名称长度与源码在 .dat 中的起止字节) */
    if (fseek(idx_fp, (long)doc_rec_offset, SEEK_SET) != 0) continue;
    sr_doc_rec_hdr_t dhdr;
    if (fread(&dhdr, sizeof(dhdr), 1, idx_fp) != 1) continue;

    /* 读取函数名称 */
    char name_buf[256];
    if (dhdr.name_len >= sizeof(name_buf)) continue;
    if (fread(name_buf, sizeof(char), dhdr.name_len, idx_fp) != dhdr.name_len) continue;
    name_buf[dhdr.name_len] = '\0';

    /* 二次校验：函数名是否包含目标 pattern (子串匹配) */
    if (strstr(name_buf, pattern) != NULL) {
      if (dhdr.end_byte < dhdr.start_byte) continue;

      size_t snippet_len = (size_t)(dhdr.end_byte - dhdr.start_byte);
      char* code_buf = (char*)malloc(snippet_len + 1);
      if (!code_buf) continue;

      /* 定位到 .dat 数据文件中的物理偏移并读取源代码片段 */
      if (fseek(data_fp, (long)dhdr.start_byte, SEEK_SET) == 0) {
        size_t read_bytes = fread(code_buf, 1, snippet_len, data_fp);
        code_buf[read_bytes] = '\0';

        match_count++;

        /* 格式化打印当前命中的函数信息 */
        printf("\n[%u] MATCHED FUNCTION: %s (Doc ID: %u)\n", match_count, name_buf, doc_id);
        printf("    Offset Range: [%llu ~ %llu] (%zu bytes)\n", 
               (unsigned long long)dhdr.start_byte, 
               (unsigned long long)dhdr.end_byte, 
               snippet_len);
        printf("---------------------- SOURCE CODE --------------------\n");
        printf("%s\n", code_buf);
        printf("-------------------------------------------------------\n");

        /* 保留首个命中的源码片段输出给 *source (若调用方需要) */
        if (!first_matched_code) {
          first_matched_code = code_buf;
        } else {
          free(code_buf); /* 避免内存泄漏 */
        }
      } else {
        free(code_buf);
      }
    }
  }

  /* 4. 清理资源 */
  free(candidate_docs);
  fclose(idx_fp);
  fclose(data_fp);

  if (match_count == 0) {
    printf("No matching functions found for pattern: '%s'\n", pattern);
    return SR_ERR_SEARCH_MATCH_NOT_FOUND;
  }

  printf("\nTotal Matched Functions: %u\n", match_count);
  *source = first_matched_code;

  return SR_SUCCESS;
}