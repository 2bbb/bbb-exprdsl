# bbb_exprdsl

C/C++ライクな数式DSLを文字列からコンパイルし、実行時に `double f(double x,double y,double z,double w)` として評価します。

## 特徴
- `namespace bbb`
- クラス/関数は snake_case
- ヘッダーオンリー
- バイトコード（スタックVM）で実行
- `&& || ?:` は短絡評価（ジャンプ命令で実現）
- `^` は累乗（右結合）
- `%` は `std::fmod`
- 数値は `double`、真偽は `0 => false` / それ以外 `true`、論理/比較は `1/0` を返す
- 0除算などは **IEEE754に従い** `±inf` / `NaN` をそのまま返します（例外は投げません）

## 演算子
- 算術: `+ - * / % ^`
- 比較: `< <= > >= == !=`（結果は 1 or 0）
- 論理: `! && ||`（短絡）
- 三項: `cond ? a : b`（短絡、右結合）

## 変数
- `x,y,z,w` または `$1,$2,$3,$4`

## 関数（ホワイトリスト）
- 1引数: `sin cos tan asin acos atan exp log log10 sqrt abs floor ceil round`
- 2引数: `pow atan2 fmod min max`

## 最適化（安全側）
- 定数畳み込み（部分式がすべて定数のとき）
- `0 && X -> 0`, `1 || X -> 1`, `cond?A:B` の `cond` が定数なら片側のみ
- `+X -> X`
- 代数的再結合（例: `2*x*3 -> 6*x`）や、符号付きゼロが変わる変形（例: `-(x-3)->3-x`）は行いません

## 使い方
```cpp
#include "bbb/exprdsl.hpp"

#include <iostream>

int main() {
  auto [e, err] = bbb::compile("x != 0 && (y/x) > 1 ? pow(z,2) : 0");
  if (err) {
    std::cerr << "error at " << err->pos << ": " << err->message << "\n";
    return 1;
  }
  std::cout << e(2, 5, 3, 0) << "\n"; // 9
}
```
