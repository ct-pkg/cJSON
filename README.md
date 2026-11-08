# json

JSON 文本与树之间的映射包。解析在进入树之前先跑一遍严格校验，树与渲染交给 `third_party/`
里原样保留的 cJSON 1.7.19。对外只出现 `JsonDoc *` / `JsonNode *`，C 类型不出包。

```ct
import json::JsonTypes;
import json::Json;

JsonDoc *d = "{\"name\":\"ct\",\"tags\":[\"c\",\"json\"]}".toDoc();
char *name = d.root().childByKey("name").stringValue();
printf("%s\n", name);
d.drop();
```

## 1. 定位与边界

做三件事：

- 把 JSON 文本解析成树，并在建树之前拒绝非法输入；
- 用接收者方法读树：`d.root().childByKey("a").numberValue()`；
- 把树或构建结果渲染回文本：`d.write(0)` / `d.write(1)`。

不做的事，明确写在这里：

- 不是流式解析器：`parse` 一次吃掉整段以 NUL 结尾的文本，没有增量喂数据；
- 不是 JSON5：注释、单引号、裸键、尾逗号、`NaN`、`Infinity` 全部拒绝；
- 不是 schema 校验器，不做类型约束；
- 没有节点级释放，树只能整棵由 doc 拥有（见 §9）；
- 不提供 `drop()` 之外的清理手段，`drop()` 只能调用一次。

## 2. 依赖与安装

- 编译器：`requires = ct >= 0.20.3`。类型别名（`pub type`）在本包落地时按 **ct v0.20.3**
  实测；`0.20.0` 是设计稿最初实测的版本，不代表本包还能在它上面编译。
- 依赖：`third_party/cJSON.h`、`third_party/cJSON.c`，上游 **cJSON v1.7.19**，原样保留，
  未打补丁。sha256 记录在 `tools/third_party.sha256`：

  ```
  25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee  third_party/cJSON.h
  298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d  third_party/cJSON.c
  ```

- 目录名是 `third_party/`，不是 `vendor/`。`ct get` 的 `copy_tree` 会跳过名为 `vendor` 的目录
  （那类目录由 `ct.lock` 管理），包自带的 `vendor/` 装完就会缺文件；`cflags` / `ldflags`
  里因此写的是 `vendor/json/third_party`——安装后包落在消费方的 `vendor/json/`，其后半段才是
  本包的 `third_party/`。
- 安装后的构建命令由 `ct install` 打印，等价于：

  ```sh
  ct build -I vendor/json \
      --cflags '-I vendor/json/third_party -DCJSON_NESTING_LIMIT=64' \
      --ldflags 'vendor/json/third_party/cJSON.c -lm' yourapp.ct -o yourapp
  ```

- `CJSON_NESTING_LIMIT=64` 把底层库的编译期嵌套上限钉成包默认值；不定义时上游默认值是 1000，
  会与 `setMaxDepth` 的默认 64 不一致。

## 3. 快速开始

读：

```ct
JsonDoc *d = "{\"a\":1,\"b\":[true,null],\"c\":\"x\"}".toDoc();
JsonNode *b = d.root().childByKey("b");
printf("%d\n", b.childCount());
printf("%d\n", b.childAt(0).boolValue());
printf("%s\n", d.write(0));
d.drop();
```

写：

```ct
JsonNode *o = json::Lib.newObject().put("ok", json::Lib.newBool(1));
JsonNode *a = json::Lib.newArray().append(json::Lib.newNumber(1));
a.append(json::Lib.newString("two"));
o.put("items", a);

JsonDoc *d = json::Lib.newDoc().setRoot(o);
printf("%s\n", d.write(0));
d.drop();
```

两段的右值都是一条链：`toDoc()` 从文本起链，`newObject().put(...)` 从构造器起链，
`newDoc().setRoot(...)` 把树交给 doc。构建出来的节点在你调用 `setRoot` 之前没有主人。

## 4. API 全表

四个宿主，方法名互不冲突；类型宿主之外没有散装函数。

### 4.1 `JsonNode *`（`Node`）

| 方法 | 签名 | 语义 |
| --- | --- | --- |
| `kind` | `int kind()` | 返回 `JSON_UNDEFINED` / `JSON_FALSE` / `JSON_TRUE` / `JSON_NULL` / `JSON_NUMBER` / `JSON_STRING` / `JSON_ARRAY` / `JSON_OBJECT` / `JSON_RAW` |
| `isNull` `isBool` `isNumber` `isString` `isArray` `isObject` | `int isX()` | 1 / 0 |
| `boolValue` | `int boolValue()` | `true` 为 1，其余为 0 |
| `numberValue` | `double numberValue()` | 非数字节点返回 0 |
| `numberInt64` | `int numberInt64(long long *out)` | 0 成功；-1 表示不是整数或超出 ±2^53 |
| `stringValue` | `char *stringValue()` | 借用指针，非字符串返回 0 |
| `key` | `char *key()` | 对象成员名；数组元素与根返回 0 |
| `childCount` | `int childCount()` | 数组元素数 / 对象成员数 |
| `firstChild` | `JsonNode *firstChild()` | 首个子节点，没有则 0 |
| `nextSibling` | `JsonNode *nextSibling()` | 下一个兄弟，走到头返回 0 |
| `childAt` | `JsonNode *childAt(int index)` | 按下标取，越界返回 0 |
| `childByKey` | `JsonNode *childByKey(const char *key)` | 按成员名取，区分大小写，未命中返回 0 |
| `put` | `JsonNode *put(const char *key, JsonNode *child)` | 对象成员写入/替换，返回接收者；失败返回 0 |
| `append` | `JsonNode *append(JsonNode *child)` | 数组追加，返回接收者；失败返回 0 |

### 4.2 `JsonDoc *`（`Doc`）

| 方法 | 签名 | 语义 |
| --- | --- | --- |
| `setMaxDepth` | `JsonDoc *setMaxDepth(int n)` | 嵌套上限，默认 64，超过 64 按 64 计；`n <= 0` 忽略；返回接收者 |
| `setMaxNodes` | `JsonDoc *setMaxNodes(int n)` | 节点上限，默认 100000；`n <= 0` 忽略；返回接收者 |
| `setMaxString` | `JsonDoc *setMaxString(int n)` | 单个字符串解码后字节上限，默认 1048576；`n <= 0` 忽略；返回接收者 |
| `setMaxInput` | `JsonDoc *setMaxInput(int n)` | 输入字节上限，默认 8388608；`n <= 0` 忽略；返回接收者 |
| `parse` | `int parse(const char *text)` | 校验 + 建树，返回 `JSON_OK` 或负错误码；成功后旧树被替换 |
| `root` | `JsonNode *root()` | 根节点，失败后为 0 |
| `setRoot` | `JsonDoc *setRoot(JsonNode *root)` | 接管一棵构建出来的树（旧树被释放），返回接收者 |
| `write` | `char *write(int format)` | `0` 紧凑 / `1` 制表符缩进；借用指针，doc 释放后失效 |
| `writeBuf` | `int writeBuf(char *out, int cap, int format)` | 写进调用方缓冲区，返回完整长度；截断到 `cap - 1` 并补 NUL |
| `error` | `int error()` | 最近一次 `parse` 的状态码 |
| `errorOffset` | `int errorOffset()` | 出错字节偏移；无错误为 -1 |
| `errorLine` | `int errorLine()` | 出错行，1 起 |
| `errorCol` | `int errorCol()` | 出错列，按字节，1 起 |
| `drop` | `void drop()` | 释放树与渲染文本；只能调用一次 |

### 4.3 `namespace json`（`Lib`）

| 方法 | 签名 | 语义 |
| --- | --- | --- |
| `newDoc` | `JsonDoc *newDoc()` | 新建文档 |
| `newNull` `newBool` `newNumber` `newString` `newArray` `newObject` | `JsonNode *newX(...)` | 新建游离节点；必须交给某个 doc |

### 4.4 `char *`（`Text`）

| 方法 | 签名 | 语义 |
| --- | --- | --- |
| `toDoc` | `JsonDoc *toDoc()` | 建 doc 并解析；失败返回 0 且 doc 已释放 |

## 5. 教程正文

### 5.1 解析与读取

```ct
JsonDoc *d = json::Lib.newDoc().setMaxDepth(32).setMaxNodes(10000);
int st = d.parse("{\"id\":7,\"tags\":[\"a\",\"b\"]}");
if (st != JSON_OK) {
    printf("bad json at %d:%d (%d)\n", d.errorLine(), d.errorCol(), st);
    d.drop();
    return 1;
}
long long id;
d.root().childByKey("id").numberInt64(&id);
printf("id=%lld tags=%d\n", id, d.root().childByKey("tags").childCount());
d.drop();
```

`parse` 成功会清掉上一次的错误状态，同一个 doc 可以反复解析：旧树释放，新树接管。

### 5.2 严格层：在建树之前拒绝

`parse` 的顺序是「先单遍校验文本，再交给底层建树」。所以下列输入会拿到精确位置，而不是
一颗半成品树：

| 输入 | 状态码 | 位置 |
| --- | --- | --- |
| `{"a":1,}` | `JSON_ERR_SYNTAX` | 1:8（`}`） |
| `[1,]` | `JSON_ERR_SYNTAX` | 1:4（`]`） |
| `01` | `JSON_ERR_SYNTAX` | 1:2（`1`） |
| `1.` | `JSON_ERR_SYNTAX` | 1:3（行尾） |
| `"bad\q"` | `JSON_ERR_SYNTAX` | 1:5（反斜杠） |
| `"\u12"` | `JSON_ERR_SYNTAX` | 1:2（反斜杠） |
| `"\uD800"` | `JSON_ERR_SYNTAX` | 1:2（反斜杠） |
| `{"a":1,"a":2}` | `JSON_ERR_DUP_KEY` | 1:8（第二个键的首字节） |
| `1e999` | `JSON_ERR_NONFINITE` | 1:1（数字首字节） |
| `"\u0000"` | `JSON_ERR_CONTROL` | 1:2（反斜杠） |
| 裸 `0xFF` | `JSON_ERR_UTF8` | 1:2（该字节） |
| 裸 `0x01` | `JSON_ERR_CONTROL` | 该字节 |
| UTF-8 BOM | `JSON_ERR_BOM` | 1:1 |

键的比较按「解码后的字节 + 长度」做，所以 `{"a\u0061":1}` 会被判成重复键，而
`{"a\u0000b":1,"a":2}` 报的是 `JSON_ERR_CONTROL`（先撞上 NUL 拒绝），不是假重复。

### 5.3 限制

```ct
JsonDoc *d = json::Lib.newDoc()
    .setMaxDepth(8).setMaxNodes(1000).setMaxString(4096).setMaxInput(65536);
```

四个 setter 都返回接收者本身，可以继续接 `parse`。非正数被忽略（保持默认），深度超过 64
按 64 计。节点计数口径：根算 1 个，每个数组元素算 1 个，每个对象成员算 1 个，键不计。

### 5.4 构建

```ct
JsonNode *o = json::Lib.newObject().put("id", json::Lib.newNumber(7));
JsonNode *tags = json::Lib.newArray().append(json::Lib.newString("a"));
tags.append(json::Lib.newString("b"));
o.put("tags", tags);

JsonDoc *d = json::Lib.newDoc().setRoot(o);
char *text = d.write(1);
printf("%s\n", text);
d.drop();
```

`put` 对已存在的键做替换，不产生重复成员；对非对象节点返回 0。`append` 对非数组节点返回 0。

### 5.5 渲染与缓冲区

`write(0)` 紧凑，`write(1)` 用制表符缩进。两者都返回 doc 内部持有的借用指针，下一次
`write` 会替换它；`drop` 之后失效。要自己持有文本用 `writeBuf`：

```ct
char buf[64];
int need = d.writeBuf(buf, 64, 0);   /* 返回完整长度，可能大于 64 */
```

`writeBuf` 至少写入 `cap - 1` 个字节并补 NUL；`cap <= 0` 只返回长度。

### 5.6 链式三形态

```ct
/* 变量接收者 */
JsonDoc *a1 = json::Lib.newDoc();
a1.setMaxDepth(8).parse("[1]");

/* 字面量接收者 */
JsonDoc *a2 = "{\"a\":1}".toDoc();

/* 调用结果接收者 */
JsonDoc *a3 = json::Lib.newDoc().setMaxDepth(16);
```

已知编译限制：临时值只能起一段链，且必须位于语句或赋值开头。把链再嵌进更大的表达式会报
`a method call on a temporary is only allowed at the start of a statement or of an assignment`：

```ct
/* 编译失败 */
printf("%d\n", text().toDoc().root().kind());

/* 合法写法 */
JsonDoc *d = text().toDoc();
printf("%d\n", d.root().kind());
```

变量开头的链不受这条限制，`printf("%d\n", d.root().childByKey("a").childCount())` 是合法的。
包内 `tests/compile_fail/temp_receiver.ct` 用编译失败把这个限制钉住。

## 6. 常见配方

读配置里的整数并给默认值：

```ct
long long port;
JsonNode *n = d.root().childByKey("port");
if (n == 0 || n.numberInt64(&port) != 0) {
    port = 8080;
}
```

遍历数组：

```ct
JsonNode *it = d.root().childByKey("tags").firstChild();
while (it != 0) {
    printf("%s\n", it.stringValue());
    it = it.nextSibling();
}
```

构造嵌套请求体：

```ct
JsonNode *req = json::Lib.newObject().put("method", json::Lib.newString("ping"));
req.put("params", json::Lib.newArray().append(json::Lib.newNumber(1)));
JsonDoc *d = json::Lib.newDoc().setRoot(req);
printf("%s\n", d.write(0));
d.drop();
```

严格校验外部输入并打印位置：

```ct
if (d.parse(text) != JSON_OK) {
    printf("line %d col %d: %d\n", d.errorLine(), d.errorCol(), d.error());
}
```

## 7. 排错

状态码（`JsonStatus`）：

| 常量 | 值 | 含义 |
| --- | ---: | --- |
| `JSON_OK` | 0 | 成功 |
| `JSON_ERR_SYNTAX` | -1 | 语法错误（含尾逗号、未知转义、缺冒号/逗号、尾随垃圾） |
| `JSON_ERR_DUP_KEY` | -2 | 同一对象里解码后重复的键 |
| `JSON_ERR_NONFINITE` | -3 | 数字溢出成 inf / NaN |
| `JSON_ERR_LIMIT` | -4 | 深度、节点、字符串或输入超限 |
| `JSON_ERR_UTF8` | -5 | 非法 UTF-8（过长的编码、孤立首字节） |
| `JSON_ERR_CONTROL` | -6 | 未转义的控制字节，或解码后为 `0x00` 的键/值 |
| `JSON_ERR_BOM` | -7 | 文本以 UTF-8 BOM 开头 |

错误位置规则：

- 位置 = **触发错误的那个字节**；行 1 起，列按字节 1 起；
- `\u` 相关错误（位数不足、孤立代理项、`\u0000`）一律指反斜杠；
- 重复键指第二个键的**首字节**（它的开引号）；
- 数字溢出指该数字的**首字节**（`-` 也算）；
- `setMaxString` 指把解码长度推过上限的那个字节；转义序列指反斜杠；
- 输入整体超长（`setMaxInput`）没有单一触发字节，固定报 1:1；
- 无错误时 `error` 为 `JSON_OK`、`errorOffset` 为 -1。

`toDoc()` 失败返回 0，且内部 doc 已经释放，错误码随之丢失。要看错误必须自己建 doc：

```ct
JsonDoc *d = json::Lib.newDoc();
if (d.parse(text) != JSON_OK) {
    printf("%d at %d:%d\n", d.error(), d.errorLine(), d.errorCol());
}
d.drop();
```

## 8. 术语表（Glossary）

术语按三类收录：本包特有概念、全仓易混概念、正文出现过但未解释的英文术语。
类型名、方法名、文件名与源码逐字一致。

| 术语 | 含义 | 本包中的所指 |
| --- | --- | --- |
| 句柄（handle） | 一个不透明指针，调用方只通过方法操作它，不直接碰内部结构。 | `JsonDoc *` 与 `JsonNode *`；两个 tag 都归本包（`struct ct_json_doc`、`struct ct_json_node`），vendored 的 `struct cJSON` 只在 shim 内部出现。 |
| 宿主（host） | trait 方法挂靠的类型。 | 四个宿主：`JsonNode *`（`Node`）、`JsonDoc *`（`Doc`）、`namespace json`（`Lib`）、`char *`（`Text`）。 |
| 映射（map trait） | 把 trait 方法绑定到宿主并转调 C 函数的声明。 | `Json.m.ct` 里的 `map trait Node for type JsonNode *` 等四段，每行转调一个 `ct_json_*` 函数。 |
| 链式返回自身（return receiver） | 方法返回接收者本身，使下一次调用可以接在后面。 | `setMaxDepth` / `setMaxNodes` / `setMaxString` / `setMaxInput` / `setRoot` / `put` / `append` 都返回接收者。 |
| 所有权（ownership） | 谁负责释放一块内存的约定。 | 只有 `JsonDoc` 拥有树；`drop()` 映射到 `ct_json_doc_free()`，对整棵树调用一次 `cJSON_Delete()`。 |
| 借用（borrow） | 只取得指针的使用权，不承担释放责任。 | `root()`、`firstChild()`、`nextSibling()`、`childAt()`、`childByKey()`、`stringValue()`、`key()`、`write()` 返回的都是借用指针，有效期到所属 doc 被 `drop()`。 |
| `JsonDoc` | 一棵 JSON 树及其解析状态的所有者。 | `Json.m.ct` 中 `pub type JsonDoc = struct ct_json_doc;`；由 `json::Lib.newDoc()` 或 `"…".toDoc()` 创建，`drop()` 只挂在这个类型上。 |
| `JsonNode` | 树中一个值的句柄。 | `pub type JsonNode = struct ct_json_node;`；读取、`put`、`append` 都在它上面，它永远不拥有节点，因此没有节点级释放——对借用节点调用 `cJSON_Delete()` 会在 doc 释放时双重释放。 |
| `ct_json_node` | 本包自己的节点句柄 tag，定义在 `Json.shim.h`，不单独分配。 | 句柄地址就是节点地址（`ct_json_handle()` / `ct_json_node_of()` 是恒等转换），所以 `setRoot(o)` 之后 `d.root() == o` 成立；vendored 的 `struct cJSON` 不再出现在任何公开签名里。 |
| 严格模式（strict mode） | 先按规范逐字节校验文本，通过后才交给底层建树。 | `parse()` 先跑 `ct_json_validate()`，再调 `cJSON_ParseWithOpts()`；本包没有宽松开关，严格是唯一模式，失败时不会留下半棵树。 |
| 节点计数（node count） | `setMaxNodes` 的口径：一次解析允许创建多少个节点。 | 根算 1，每个数组元素算 1，每个对象成员算 1，成员名不计；默认上限 100000。 |
| `setMaxDepth`（`max_depth`） | 输入允许的最大嵌套层数。 | 默认 64；上限由 `CJSON_NESTING_LIMIT` 在编译期钉死，所以 `setMaxDepth()` 只能收紧，传大于 64 的值按 64 处理。 |
| `CJSON_NESTING_LIMIT` | cJSON 的编译期嵌套上限宏。 | 在 `ct.pkg` 的 `cflags` 里以 `-DCJSON_NESTING_LIMIT=64` 固定，`CT_JSON_DEPTH_CAP` 直接取它的值。 |
| RFC 8259 | 定义 JSON 文本格式的 IETF 标准。 | 严格层的验收基准；唯一偏离是拒绝 `\u0000`，见下一行。 |
| `\u0000` | JSON 转义序列，解码后是一个 NUL 字节。 | 本包返回 `JSON_ERR_CONTROL` 而不是接受：`char *` 表面存不下内嵌 NUL；夹具在 `tests/unit.ct`。 |
| BOM | Byte Order Mark，UTF-8 下是开头的 `EF BB BF` 三字节。 | 出现在输入开头时直接返回 `JSON_ERR_BOM`，位置固定 1:1。 |
| 往返（round-trip） | 文本解析成树、再把树渲染回文本的可逆性。 | 只在 15 位有效数字内成立；需要精确整数时用 `numberInt64()`，不要依赖文本层。 |
| `%1.15g` | C 的数字格式，最多 15 位有效数字。 | `third_party/cJSON.c` 的 `print_number()` 用它输出数字，这是“15 位往返”的直接来源。 |
| 临时值（temporary） | 没有变量名、只存在于表达式里的中间结果。 | 编译器只允许临时值出现在链首：`"…".toDoc()` 合法，把它再嵌进别的调用会被拒绝；夹具 `tests/compile_fail/temp_receiver.ct`。 |
| 游离节点（detached node） | 尚未被任何 doc 接管的构造结果。 | `json::Lib.newNull` / `newBool` / `newNumber` / `newString` / `newArray` / `newObject` 的返回值；必须交给 `setRoot()`、`put()` 或 `append()`，否则就是无主泄漏。 |
| cJSON | 本包 vendored 的 C 语言 JSON 库。 | `third_party/cJSON.h` 与 `third_party/cJSON.c`，版本 1.7.19，原样保留、未打补丁。 |
| `third_party/` | 包内自带第三方源码的目录名。 | 存放 cJSON；不能改名为 `vendor/`，因为 `ct get` 会整体跳过该名字。 |
| `NUL` | 字节值 0 的写法，不是字符 `'0'`。 | `parse()` 用 `strlen()` 取输入长度，所以文本不能内嵌 NUL；`writeBuf()` 截断后补 NUL。 |

## 9. 限制与未支持

- **没有节点级释放**。任何树都必须由某个 doc 持有；`JsonNode *` 一律是借用，`drop()` 只在
  `JsonDoc *` 上存在。
- **构建链必须立刻交给 `newDoc().setRoot(...)`**。`newObject()` / `newNull()` 等返回的游离节点
  不被任何 doc 持有，忘了 `setRoot` 就是无主泄漏。
- **`put` / `append` 失败返回 0 时子节点未被收养**。这属于程序性错误路径（对非对象 `put`、
  对非数组 `append`、传 0），失败时不接管子节点，由调用方负责把它交给别的容器。
- **`drop()` 只能调用一次**，不是幂等操作。
- **数字只有 15 位有效数字**。渲染用 `%1.15g`，`9007199254740991` 输出为
  `9.00719925474099e+15`；需要精确整数往返时不要依赖文本层，改用 `numberInt64`。
- **字符串里不能有 `0x00`**。RFC 8259 允许 `\u0000`，但 `char *` 表面存不下它，本包直接以
  `JSON_ERR_CONTROL` 拒绝——这是对 RFC 的一项偏离，夹具见 `tests/unit.ct`。
- **嵌套上限 64**。编译期由 `CJSON_NESTING_LIMIT=64` 钉死，`setMaxDepth` 只能往小调。
- **单根**。没有数组/对象以外的顶层容器，也没有多文档流。
- **临时值链限制**，见 §5.6。
- **不合并重复键**。重复键直接报错，不做后者覆盖前者。
- **渲染顺序**是插入顺序，不做键排序。

## 10. 版本与变更记录

### 0.1.0（2026-11-08）

- 首个版本：严格校验 + 解析 + 树读取 + 构建 + 渲染。
- 四个宿主：`JsonNode *`（19 个方法）、`JsonDoc *`（14 个）、`namespace json`（7 个构造器）、
  `char *`（1 个 `toDoc`）。
- 底层 cJSON 1.7.19 原样放在 `third_party/`，sha256 见 `tools/third_party.sha256`。
- 按评审裁决删除节点级 `drop()` 与 `toJSON()`：doc 唯一拥有树，节点一律借用。
- `JsonNode` 的底层收进包内节点句柄（tag 由 shim 提供）：公开签名、`pub type` 别名与
  生成 C 里都不再出现三方结构体名。
- 门槛：`tests/run.sh` 与 `tools/check.sh` 在 ct v0.20.3 上全绿，`-Wall -Wextra` 零告警。
