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
#include "sourceread-util.h"

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
*/
typedef struct {
  sr_builder_t* builder;  /* 索引构建器指针 */
  FILE* data_fp;          /* 存放抽取源码片段的 .dat 数据文件指针 */
} sr_indexer_ctx_t;

/*!
** 遍历访问者回调函数指针定义。
*/
typedef void (*sr_doc_visitor_fn)(uint32_t doc_id, 
                                  const char* fn_name, 
                                  const char* code, 
                                  size_t code_len, 
                                  void* userdata);

/* ========================================================================== */
/*                         helpers: hash & posts                              */
/* ========================================================================== */

static void
sr_add_posts(sr_mem_post_t* p, uint32_t doc_id)
{
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

sr_builder_t*
sr_builder_new(void)
{
  sr_builder_t* b = (sr_builder_t*)calloc(1, sizeof(sr_builder_t));
  return b;
}

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

  /* 提取 Trigrams */
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

static int
sr_compare_dict(const void* a, const void* b)
{
  const sr_dict_rec_t* r1 = (const sr_dict_rec_t*)a;
  const sr_dict_rec_t* r2 = (const sr_dict_rec_t*)b;
  return memcmp(r1->trigram_key, r2->trigram_key, 3);
}

static int
sr_compare_sym(const void* a, const void* b)
{
  const sr_sym_rec_t* r1 = (const sr_sym_rec_t*)a;
  const sr_sym_rec_t* r2 = (const sr_sym_rec_t*)b;
  if (r1->hash < r2->hash) return -1;
  if (r1->hash > r2->hash) return 1;
  return (int)((int64_t)r1->doc_id - (int64_t)r2->doc_id);
}

/*!
** 将内存中构建的三元组索引结构及完整符号表序列化到指定二进制文件中。
*/
static bool
sr_save_file(sr_builder_t* b, const char* filepath)
{
  FILE* fp = fopen(filepath, "wb");
  if (!fp) return false;

  /* 收集字典节点并排序 */
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

  /* 1. 占位写入 Header */
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

  /* 3. 写入 Section 3: Postings 命中倒排链 */
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
  fwrite(doc_offsets, sizeof(uint64_t), b->doc_count, fp); /* 占位跳跃表 */

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
  long doc_records_end = ftell(fp);
  fseek(fp, doc_table_pos, SEEK_SET);
  fwrite(doc_offsets, sizeof(uint64_t), b->doc_count, fp);
  fseek(fp, doc_records_end, SEEK_SET);

  /* 5. 写入 Section 5: Symbol Table (完整函数名哈希排序索引表) */
  uint64_t symtab_offset = (uint64_t)ftell(fp);
  if (b->doc_count > 0)
  {
    sr_sym_rec_t* symtab = (sr_sym_rec_t*)malloc(sizeof(sr_sym_rec_t) * b->doc_count);
    for (size_t i = 0; i < b->doc_count; i++)
    {
      symtab[i].hash = sr_hash64(b->docs[i].name);
      symtab[i].doc_id = (uint32_t)i;
    }
    qsort(symtab, b->doc_count, sizeof(sr_sym_rec_t), sr_compare_sym);
    fwrite(symtab, sizeof(sr_sym_rec_t), b->doc_count, fp);
    free(symtab);
  }

  /* 回填更新 Header */
  long file_end_pos = ftell(fp);
  hdr.file_size = (uint64_t)file_end_pos;
  hdr.symtab_offset = symtab_offset;
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
** 精确匹配完整的函数名称（使用符号表 O(log N) 二分快速查找）。
**
** @param index_path .srix 二进制索引文件路径
** @param data_path  .dat 源码数据文件路径
** @param pattern    待完全匹配的函数名（不限字符长度）
** @param source     [输出参数] 传出查找到的首个匹配源码 (调用方需 free)
**
** @return int 错误码 (SR_SUCCESS 为成功，无完全匹配项返回 SR_ERR_SEARCH_MATCH_NOT_FOUND)
*/
int
sr_match_source(const char* index_path, 
                const char* data_path, 
                const char* pattern, 
                char** source)
{
  if (!index_path || !data_path || !pattern || !source) {
    return SR_ERR_COMM_PARAM_NULL;
  }
  *source = NULL;

  FILE* idx_fp = fopen(index_path, "rb");
  if (!idx_fp) return SR_ERR_FILE_HANDLER_OPEN;

  sr_srix_hdr_t hdr;
  if (fread(&hdr, sizeof(hdr), 1, idx_fp) != 1 || hdr.magic != SRIX_MAGIC) {
    fclose(idx_fp);
    return SR_ERR_INDEX_HDR_MAGIC_MISMATCH;
  }

  if (hdr.symtab_offset == 0 || hdr.total_docs == 0) {
    fclose(idx_fp);
    return SR_ERR_SEARCH_MATCH_NOT_FOUND;
  }

  FILE* data_fp = fopen(data_path, "rb");
  if (!data_fp) {
    fclose(idx_fp);
    return SR_ERR_FILE_HANDLER_OPEN;
  }

  /* 1. 针对完整函数名计算 64 位哈希并在符号表进行二分检索 */
  uint64_t target_hash = sr_hash64(pattern);
  int64_t low = 0;
  int64_t high = (int64_t)hdr.total_docs - 1;
  int64_t hit_idx = -1;

  while (low <= high)
  {
    int64_t mid = low + (high - low) / 2;
    fseek(idx_fp, (long)(hdr.symtab_offset + mid * sizeof(sr_sym_rec_t)), SEEK_SET);
    sr_sym_rec_t rec;
    if (fread(&rec, sizeof(rec), 1, idx_fp) != 1) break;

    if (rec.hash == target_hash)
    {
      hit_idx = mid;
      break;
    }
    else if (rec.hash < target_hash)
    {
      low = mid + 1;
    }
    else
    {
      high = mid - 1;
    }
  }

  if (hit_idx == -1) {
    fclose(idx_fp);
    fclose(data_fp);
    return SR_ERR_SEARCH_MATCH_NOT_FOUND;
  }

  /* 2. 命中哈希后向左右线性扩展扫描同哈希条目 */
  int64_t l = hit_idx;
  while (l > 0)
  {
    fseek(idx_fp, (long)(hdr.symtab_offset + (l - 1) * sizeof(sr_sym_rec_t)), SEEK_SET);
    sr_sym_rec_t rec;
    if (fread(&rec, sizeof(rec), 1, idx_fp) == 1 && rec.hash == target_hash) l--;
    else break;
  }

  int64_t r = hit_idx;
  while (r < (int64_t)hdr.total_docs - 1)
  {
    fseek(idx_fp, (long)(hdr.symtab_offset + (r + 1) * sizeof(sr_sym_rec_t)), SEEK_SET);
    sr_sym_rec_t rec;
    if (fread(&rec, sizeof(rec), 1, idx_fp) == 1 && rec.hash == target_hash) r++;
    else break;
  }

  /* 3. 读取文档实际函数名进行 strcmp 精确比对 */
  uint32_t match_count = 0;
  char* first_matched_code = NULL;

  for (int64_t i = l; i <= r; i++)
  {
    fseek(idx_fp, (long)(hdr.symtab_offset + i * sizeof(sr_sym_rec_t)), SEEK_SET);
    sr_sym_rec_t rec;
    fread(&rec, sizeof(rec), 1, idx_fp);

    /* 跳转至文档记录头获取函数名称与偏移 */
    fseek(idx_fp, (long)(hdr.docs_offset + rec.doc_id * sizeof(uint64_t)), SEEK_SET);
    uint64_t doc_rec_offset = 0;
    fread(&doc_rec_offset, sizeof(uint64_t), 1, idx_fp);

    fseek(idx_fp, (long)doc_rec_offset, SEEK_SET);
    sr_doc_rec_hdr_t dhdr;
    fread(&dhdr, sizeof(dhdr), 1, idx_fp);

    char name_buf[256];
    if (dhdr.name_len >= sizeof(name_buf)) continue;
    fread(name_buf, sizeof(char), dhdr.name_len, idx_fp);
    name_buf[dhdr.name_len] = '\0';

    if (strcmp(name_buf, pattern) == 0)
    {
      size_t code_len = (size_t)(dhdr.end_byte - dhdr.start_byte);
      char* code_buf = (char*)malloc(code_len + 1);
      if (code_buf)
      {
        fseek(data_fp, (long)dhdr.start_byte, SEEK_SET);
        fread(code_buf, 1, code_len, data_fp);
        code_buf[code_len] = '\0';

        match_count++;
        printf("\n[*] EXACT MATCH [%u]: %s (Doc ID: %u)\n", match_count, name_buf, rec.doc_id);
        printf("    Offset: [%llu ~ %llu] (%zu bytes)\n", 
               (unsigned long long)dhdr.start_byte, 
               (unsigned long long)dhdr.end_byte, 
               code_len);
        printf("---------------------- SOURCE CODE --------------------\n");
        printf("%s\n", code_buf);
        printf("-------------------------------------------------------\n");

        if (!first_matched_code) first_matched_code = code_buf;
        else free(code_buf);
      }
    }
  }

  fclose(idx_fp);
  fclose(data_fp);

  if (match_count == 0) {
    return SR_ERR_SEARCH_MATCH_NOT_FOUND;
  }

  *source = first_matched_code;
  return SR_SUCCESS;
}

/*!
** 混合检索：完全匹配优先 (Priority 1) + 三元组模糊/子串匹配 (Priority 2)。
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

  printf("\n=======================================================\n");
  printf("  Search Results for: '%s'\n", pattern);
  printf("=======================================================\n");

  /* ========================================================
  ** 【优先级 1】执行完全匹配检索 (调用 sr_match_source)
  ** ======================================================== */
  char* exact_code = NULL;
  int exact_rc = sr_match_source(index_path, data_path, pattern, &exact_code);
  bool has_exact = (exact_rc == SR_SUCCESS);

  if (has_exact) {
    *source = exact_code; /* 优先保留完全匹配的源码 */
  }

  /* ========================================================
  ** 【优先级 2】模糊/子串匹配检索 (Trigram 倒排求交)
  ** ======================================================== */
  size_t plen = strlen(pattern);
  if (plen < 3) {
    /* 若 pattern 小于 3 字符，Trigram 无法工作，直接依据完全匹配结果返回 */
    return has_exact ? SR_SUCCESS : SR_ERR_SEARCH_QUERY_TOO_SHORT;
  }

  FILE* idx_fp = fopen(index_path, "rb");
  if (!idx_fp) {
    return has_exact ? SR_SUCCESS : SR_ERR_FILE_HANDLER_OPEN;
  }

  sr_srix_hdr_t hdr;
  if (fread(&hdr, sizeof(hdr), 1, idx_fp) != 1 || hdr.magic != SRIX_MAGIC) {
    fclose(idx_fp);
    return has_exact ? SR_SUCCESS : SR_ERR_INDEX_HDR_MAGIC_MISMATCH;
  }

  uint32_t* candidate_docs = NULL;
  uint32_t candidate_count = 0;
  bool trigram_ok = true;

  for (size_t i = 0; i <= plen - 3; i++) {
    char gram[3];
    gram[0] = (char)tolower((unsigned char)pattern[i]);
    gram[1] = (char)tolower((unsigned char)pattern[i + 1]);
    gram[2] = (char)tolower((unsigned char)pattern[i + 2]);

    uint32_t* current_list = NULL;
    uint32_t current_count = 0;

    if (!sr_find_posts_for_gram(idx_fp, &hdr, gram, &current_list, &current_count)) {
      trigram_ok = false;
      break;
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
      trigram_ok = false;
      break;
    }
  }

  if (!trigram_ok || candidate_count == 0) {
    free(candidate_docs);
    fclose(idx_fp);
    return has_exact ? SR_SUCCESS : SR_ERR_SEARCH_MATCH_NOT_FOUND;
  }

  FILE* data_fp = fopen(data_path, "rb");
  if (!data_fp) {
    free(candidate_docs);
    fclose(idx_fp);
    return has_exact ? SR_SUCCESS : SR_ERR_FILE_HANDLER_OPEN;
  }

  uint32_t partial_match_count = 0;
  bool partial_header_printed = false;

  for (uint32_t i = 0; i < candidate_count; i++) {
    uint32_t doc_id = candidate_docs[i];

    fseek(idx_fp, (long)(hdr.docs_offset + doc_id * sizeof(uint64_t)), SEEK_SET);
    uint64_t doc_rec_offset = 0;
    if (fread(&doc_rec_offset, sizeof(uint64_t), 1, idx_fp) != 1) continue;

    fseek(idx_fp, (long)doc_rec_offset, SEEK_SET);
    sr_doc_rec_hdr_t dhdr;
    if (fread(&dhdr, sizeof(dhdr), 1, idx_fp) != 1) continue;

    char name_buf[256];
    if (dhdr.name_len >= sizeof(name_buf)) continue;
    if (fread(name_buf, sizeof(char), dhdr.name_len, idx_fp) != dhdr.name_len) continue;
    name_buf[dhdr.name_len] = '\0';

    /* 去重：如果完全相等，已在优先级 1 中展示过，跳过 */
    if (strcmp(name_buf, pattern) == 0) {
      continue;
    }

    /* 子串匹配 */
    if (strstr(name_buf, pattern) != NULL) {
      if (dhdr.end_byte < dhdr.start_byte) continue;

      size_t snippet_len = (size_t)(dhdr.end_byte - dhdr.start_byte);
      char* code_buf = (char*)malloc(snippet_len + 1);
      if (!code_buf) continue;

      if (fseek(data_fp, (long)dhdr.start_byte, SEEK_SET) == 0) {
        size_t read_bytes = fread(code_buf, 1, snippet_len, data_fp);
        code_buf[read_bytes] = '\0';

        if (!partial_header_printed) {
          printf("\n>>> [PRIORITY 2: PARTIAL MATCHES] <<<\n");
          partial_header_printed = true;
        }

        partial_match_count++;
        printf("\n[-] PARTIAL MATCH [%u]: %s (Doc ID: %u)\n", partial_match_count, name_buf, doc_id);
        printf("    Offset Range: [%llu ~ %llu] (%zu bytes)\n", 
               (unsigned long long)dhdr.start_byte, 
               (unsigned long long)dhdr.end_byte, 
               snippet_len);
        printf("---------------------- SOURCE CODE --------------------\n");
        printf("%s\n", code_buf);
        printf("-------------------------------------------------------\n");

        if (!*source) {
          *source = code_buf;
        } else {
          free(code_buf);
        }
      } else {
        free(code_buf);
      }
    }
  }

  free(candidate_docs);
  fclose(idx_fp);
  fclose(data_fp);

  if (!has_exact && partial_match_count == 0) {
    return SR_ERR_SEARCH_MATCH_NOT_FOUND;
  }

  return SR_SUCCESS;
}

/* ========================================================================== */
/*                        Tree-sitter parser & build API                      */
/* ========================================================================== */

static char*
sr_fun_name(TSNode node, const char* source_code)
{
  if (ts_node_is_null(node) || !source_code) return NULL;

  if (strcmp(ts_node_type(node), "function_definition") != 0)
  {
    return NULL;
  }

  TSNode decl = ts_node_child_by_field_name(node, "declarator", 10);

  while (!ts_node_is_null(decl))
  {
    const char* type = ts_node_type(decl);

    if (strcmp(type, "identifier") == 0)
    {
      uint32_t start = ts_node_start_byte(decl);
      uint32_t end = ts_node_end_byte(decl);
      uint32_t len = end - start;

      char* name = (char*)malloc(len + 1);
      if (!name) return NULL;

      memcpy(name, source_code + start, len);
      name[len] = '\0';
      return name;
    }

    TSNode inner = ts_node_child_by_field_name(decl, "declarator", 10);
    if (!ts_node_is_null(inner))
    {
      decl = inner;
    }
    else
    {
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
    fputc('\n', ctx->data_fp);
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

int
sr_traverse_each(const char* index_path, 
                 const char* data_path, 
                 sr_doc_visitor_fn visitor, 
                 void* userdata)
{
  if (!index_path || !data_path || !visitor) return SR_ERR_COMM_PARAM_NULL;

  FILE* idx_fp = fopen(index_path, "rb");
  if (!idx_fp) return SR_ERR_FILE_HANDLER_OPEN;

  sr_srix_hdr_t hdr;
  if (fread(&hdr, sizeof(hdr), 1, idx_fp) != 1 || hdr.magic != SRIX_MAGIC) {
    fclose(idx_fp);
    return SR_ERR_INDEX_HDR_MAGIC_MISMATCH;
  }

  FILE* data_fp = fopen(data_path, "rb");
  if (!data_fp) {
    fclose(idx_fp);
    return SR_ERR_FILE_HANDLER_OPEN;
  }

  uint64_t first_rec_offset = hdr.docs_offset + (hdr.total_docs * sizeof(uint64_t));
  fseek(idx_fp, (long)first_rec_offset, SEEK_SET);

  for (uint64_t doc_id = 0; doc_id < hdr.total_docs; doc_id++) {
    sr_doc_rec_hdr_t dhdr;
    if (fread(&dhdr, sizeof(dhdr), 1, idx_fp) != 1) break;

    char* name_buf = (char*)malloc(dhdr.name_len + 1);
    fread(name_buf, sizeof(char), dhdr.name_len, idx_fp);
    name_buf[dhdr.name_len] = '\0';

    size_t len = (size_t)(dhdr.end_byte - dhdr.start_byte);
    char* code = (char*)malloc(len + 1);
    fseek(data_fp, (long)dhdr.start_byte, SEEK_SET);
    fread(code, 1, len, data_fp);
    code[len] = '\0';

    /* 回调业务层 */
    visitor((uint32_t)doc_id, name_buf, code, len, userdata);

    free(name_buf);
    free(code);
  }

  fclose(idx_fp);
  fclose(data_fp);
  return SR_SUCCESS;
}