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
  uint64_t symtab_offset; /* 符号表 (完整函数名索引) 起始偏移量 */
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

/* 定长 12 字节完整函数名符号索引条目 */
typedef struct {
  uint64_t hash;          /* 函数名 64 位 FNV-1a 哈希值 */
  uint32_t doc_id;        /* 文档 ID */
} sr_sym_rec_t;

#pragma pack(pop)

/*!
** 文档/符号节点遍历访问者回调函数指针类型定义。
**
** 用于在全量遍历索引文件 (如 sr_traverse_each) 时，将逐个解析出的文档元数据
** 以及对应的源代码切片数据派发给上层业务逻辑（如批量打印、导出 JSON、图数据构建等）。
**
** 注意事项 (Memory Ownership)：
** - fn_name 与 code 缓冲区在单次迭代中分配，并在当前回调函数返回后由遍历引擎立即销毁释放；
** - 若业务层需要跨作用域持久持有这些文本数据，必须在回调内自行进行深拷贝 (例如 strdup 或 memcpy)。
**
** @param doc_id    当前文档/符号在索引中的全局唯一标识编号（0 起始递增）。
** @param fn_name   函数或代码符号的名称字符串（以 '\0' 结尾）。
** @param code      从数据文件中检索抽取的完整源码片段文本（以 '\0' 结尾）。
** @param code_len  源码片段的有效字节长度（不包含结尾的 '\0'）。
** @param userdata  调用方通过遍历入口透传的用户自定义上下文指针。
*/
typedef void (*sr_doc_visitor_fn)(uint32_t doc_id, 
                                  const char* fn_name, 
                                  const char* code, 
                                  size_t code_len, 
                                  void* userdata);

/*!
** 全量扫描目标工程源码并构建三元组符号索引及源码片段数据文件。
**
** 整体执行流程：
** 1. 创建并以二进制写入模式打开 .dat 目标数据文件，用于流式落盘抽取的源码片段；
** 2. 初始化三元组内存构建器容器 (sr_builder_t) 及上下文控制块；
** 3. 递归遍历指定工程目录 (sr_walk_dir)，匹配目标文件后缀，调用 Tree-sitter 
**    解析 AST 语法树并提取目标节点（如函数定义）；
** 4. 将提取的节点源码格式化后追加存入 .dat 文件，同时将符号元数据与生成的 Trigrams 
**    登记至构建器内存哈希表中；
** 5. 调用 sr_save_file 将内存索引全量序列化输出至 .srix 持久化文件；
** 6. 刷新并安全关闭文件句柄，释放构建器全部动态堆内存资源。
**
** @param parser      已配置并初始化目标编程语言的 Tree-sitter 解析器实例句柄。
** @param index_path  持久化输出的目标 .srix 二进制索引文件路径。
** @param data_path   持久化输出的目标 .dat 源码切片数据文件路径。
** @param proj_path   待扫描的目标源码项目工程根目录路径。
** @param file_exts   需要过滤采集的文件后缀名规则字符串（如 ".c;.h"）。
** @param node_type   需要在 AST 语法树中匹配并提取的目标语法节点类型（如 "function_definition"）。
**
** @return int        执行状态码：成功返回 0；若目标数据文件创建/打开失败返回 1。
*/
int
sr_build_index(TSParser* parser,
               const char* index_path, 
               const char* data_path, 
               const char* proj_path,
               const char* file_exts,
               const char* node_type);

               /*!
** 根据关键字在基于 Trigram 的 SRIX 二进制索引中检索函数，并从源码数据文件中抽取其完整代码片段。
**
** 算法原理与执行流程：
** 1. **索引校验**：打开索引文件并验证 Header 魔数 (SRIX) 是否合法；
** 2. **Trigram 分词与倒排求交**：
**    - 将传入的 pattern 切分为长度为 3 的滑动窗口字符小写三元组；
**    - 利用二分查找在 Section 2 (Dictionary) 中检索各 Trigram 对应的 Postings 倒排链；
**    - 对命中的多个候选链进行升序双指针求交 (AND 运算)，快速过滤出候选文档 Doc ID 集合；
** 3. **元数据过滤 (False Positive Filtering)**：
**    - 根据候选 Doc ID 在 Section 4 中查找对应的符号名称；
**    - 校验符号名是否包含 pattern（亦可根据业务需求改为 strcmp 精确匹配）；
** 4. **读取原始代码片段**：
**    - 命中候选后，从 `sr_doc_rec_hdr_t` 提取对应源码在 `data_path` 文件中的 `start_byte` 与 `end_byte`；
**    - Seek 到数据文件的物理偏移位置，分配 `(end_byte - start_byte + 1)` 字节并读出完整代码片段；
** 5. **资源释放**：释放倒排检索过程中的临时内存并返回。
**
** @param index_path   .srix 二进制索引文件路径。
** @param data_path    存放已提取源代码片段的 .dat 数据文件路径。
** @param pattern      待检索的目标符号/函数名（长度需 >= 3）。
** @param source       [输出参数] 存放查找到的源代码片段缓冲区指针（调用方负责 free）。
**
** @return int         状态码：0 表示成功，负数表示失败或未找到（参见错误码定义）。
*/
int
sr_search_source(const char* index_path, 
                 const char* data_path, 
                 const char* pattern,
                 char** source);

/*!
** 不走 Trigram 倒排索引，以流式访问方式全量顺序遍历全部文档节点并触发用户回调。
**
** 算法原理与执行逻辑：
** 1. 参数校验与文件打开：校验入参有效性，读取并验证 .srix 索引头的魔数一致性；
** 2. 定位文档实体区：越过 Section 4 前导的 doc_offsets 偏移跳跃表，
**    直接 seek 到首个文档实体的物理文件起始偏移，充分利用顺序读的高 I/O 吞吐优势；
** 3. 线性流式迭代：按 total_docs 计数循环解析每个文档实体的变长头信息与名称字符串；
** 4. 源码提取与回调派发：依据文档记录中的物理起止区间 [start_byte, end_byte]，
**    从 .dat 数据文件中读取对应的源代码文本，并通过 visitor 回调函数将节点数据派发给调用方；
** 5. 资源清理：每轮循环内按需释放临时分配的缓冲区内存，遍历结束后关闭所有打开的文件句柄。
**
** @param index_path  .srix 二进制索引文件路径。
** @param data_path   .dat 源码切片数据文件路径。
** @param visitor     用户自定义的节点遍历消费回调函数指针 (sr_doc_visitor_fn)。
** @param userdata    传递给回调函数的透传用户上下文指针（支持传 NULL）。
**
** @return int        执行状态码：
**                    - SR_SUCCESS: 全量遍历成功完成；
**                    - SR_ERR_COMM_PARAM_NULL: 传入的核心参数指针为空；
**                    - SR_ERR_FILE_HANDLER_OPEN: 索引或数据文件打开失败；
**                    - SR_ERR_INDEX_HDR_MAGIC_MISMATCH: 索引文件头校验失败或格式不合法。
*/
int
sr_traverse_each(const char* index_path, 
                 const char* data_path, 
                 sr_doc_visitor_fn visitor, 
                 void* userdata);

#ifdef __cplusplus
}
#endif

#endif // __SOURCEREAD_STORAGE_H__