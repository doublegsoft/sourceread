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

#ifdef __cplusplus
}
#endif

#endif // __SOURCEREAD_STORAGE_H__