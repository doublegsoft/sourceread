```
https://tree-sitter.github.io/tree-sitter/
```

Here is the categorized reference list of the most important and commonly used **Tree-sitter C grammar (`tree-sitter-c`) node types**. 

In Tree-sitter, node types are returned as C strings via `ts_node_type(node)`.

---

### 1. Root & Top-Level Declarations

| Node Type (`ts_node_type`) | Description / C Syntax Example |
| :--- | :--- |
| **`translation_unit`** | The root node representing the entire source file. |
| **`function_definition`** | Full function definition: `int add(int a) { return a; }` |
| **`declaration`** | Variable or function prototype: `int x;` or `void foo();` |
| **`type_definition`** | Typedef statement: `typedef struct foo foo_t;` |
| **`linkage_specification`** | Extern linkage: `extern "C" { ... }` |

---

### 2. Functions & Parameters

| Node Type | Description / C Syntax Example |
| :--- | :--- |
| **`function_declarator`** | Function signature part: `add(int a, int b)` |
| **`parameter_list`** | The parentheses list: `(int a, int b)` |
| **`parameter_declaration`**| A single parameter: `int a` |
| **`variadic_parameter`** | Ellipsis parameter: `...` |

---

### 3. Data Types & Type Specifiers

| Node Type | Description / C Syntax Example |
| :--- | :--- |
| **`primitive_type`** | Built-in primitive: `int`, `char`, `void`, `float`, `double` |
| **`type_identifier`** | User-defined typedef type name: `size_t`, `uint32_t`, `my_struct_t` |
| **`struct_specifier`** | Struct definition: `struct Point { int x; int y; }` |
| **`union_specifier`** | Union definition: `union Data { int i; float f; }` |
| **`enum_specifier`** | Enum definition: `enum Color { RED, GREEN }` |
| **`pointer_declarator`** | Pointer token: `*ptr` |
| **`array_declarator`** | Array bracket token: `arr[10]` |
| **`type_qualifier`** | Qualifiers: `const`, `volatile`, `restrict`, `atomic` |

---

### 4. Statements (Control Flow & Scopes)

| Node Type | Description / C Syntax Example |
| :--- | :--- |
| **`compound_statement`** | A block of code in curly braces: `{ ... }` |
| **`expression_statement`**| A statement ending in semicolon: `x = 10;` or `foo();` |
| **`if_statement`** | If block: `if (cond) { ... } else { ... }` |
| **`for_statement`** | For loop: `for (int i = 0; i < 10; i++) { ... }` |
| **`while_statement`** | While loop: `while (running) { ... }` |
| **`do_statement`** | Do-while loop: `do { ... } while (cond);` |
| **`switch_statement`** | Switch block: `switch (val) { ... }` |
| **`case_statement`** | Case label: `case 1: ...` or `default: ...` |
| **`return_statement`** | Return statement: `return 0;` |
| **`break_statement`** | `break;` |
| **`continue_statement`** | `continue;` |
| **`goto_statement`** | `goto error;` |
| **`labeled_statement`** | Jump target label: `error: return -1;` |

---

### 5. Expressions & Operators

| Node Type | Description / C Syntax Example |
| :--- | :--- |
| **`call_expression`** | Function call: `printf("%d", x)` |
| **`argument_list`** | Arguments inside function call: `("%d", x)` |
| **`binary_expression`** | Math/Logic binary ops: `a + b`, `x && y`, `x == y` |
| **`unary_expression`** | Unary ops: `!flag`, `-val`, `&var`, `*ptr`, `~mask` |
| **`update_expression`** | Increment/Decrement: `i++`, `--count` |
| **`assignment_expression`**| Assignment: `x = 5`, `x += 2` |
| **`conditional_expression`**| Ternary operator: `cond ? a : b` |
| **`cast_expression`** | Typecast: `(uint32_t)val` |
| **`sizeof_expression`** | Size calculation: `sizeof(int)` or `sizeof expr` |
| **`subscript_expression`** | Array access: `arr[i]` |
| **`field_expression`** | Struct field access: `point.x` or `node->next` |
| **`parenthesized_expression`**| Parentheses group: `(a + b)` |
| **`comma_expression`** | Comma operator: `(a = 1, b = 2)` |

---

### 6. Literals & Identifiers (Leaf / Terminal Nodes)

| Node Type | Description / C Syntax Example |
| :--- | :--- |
| **`identifier`** | Variable or function name: `foo`, `main`, `index` |
| **`number_literal`** | Numbers: `42`, `0xFF`, `3.14159f` |
| **`string_literal`** | Double-quoted strings: `"hello world"` |
| **`char_literal`** | Single-quoted characters: `'a'`, `'\n'` |
| **`true` / `false`** | Boolean literals: `true`, `false` |
| **`null`** | Null literal: `NULL` |

---

### 7. Preprocessor Directives

| Node Type | Description / C Syntax Example |
| :--- | :--- |
| **`preproc_include`** | Include header: `#include <stdio.h>` |
| **`preproc_def`** | Macro constant: `#define MAX_SIZE 1024` |
| **`preproc_function_def`**| Function-like macro: `#define MIN(a, b) ((a) < (b) ? (a) : (b))` |
| **`preproc_if`** | `#if ... #endif` |
| **`preproc_ifdef`** | `#ifdef DEBUG ... #endif` |
| **`preproc_else`** | `#else` |
| **`preproc_elif`** | `#elif ...` |

---

### 8. Comments

| Node Type | Description / C Syntax Example |
| :--- | :--- |
| **`comment`** | Single-line (`// ...`) or Multi-line (`/* ... */`) comments. |

---

### Practical Example: Extracting Function Names

To extract the function name from a `function_definition` node:

```c
// When node is "function_definition":
// It typically has a "declarator" field which is a "function_declarator",
// and that contains the "identifier".

TSNode declarator = ts_node_child_by_field_name(node, "declarator", strlen("declarator"));
while (strcmp(ts_node_type(declarator), "function_declarator") != 0 && 
       !ts_node_is_null(declarator)) {
  declarator = ts_node_child_by_field_name(declarator, "declarator", strlen("declarator"));
}

TSNode fn_name_node = ts_node_child_by_field_name(declarator, "declarator", strlen("declarator"));
if (strcmp(ts_node_type(fn_name_node), "identifier") == 0) {
  // Found the actual function name!
}
```