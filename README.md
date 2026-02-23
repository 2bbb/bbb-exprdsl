# bbb_exprdsl

A C/C++-like math expression DSL that compiles from a string and evaluates at runtime as `double f(double x, double y, double z, double w)`.

## Features
- `namespace bbb`
- snake_case names for classes/functions
- header-only
- bytecode-based stack VM execution
- short-circuit evaluation for `&&`, `||`, and `?:` (implemented with jump instructions)
- `^` means exponentiation (right-associative)
- `%` uses `std::fmod`
- numeric type is `double`; boolean semantics are `0 => false`, non-zero => `true`; logical/comparison operators return `1/0`
- division by zero and similar cases follow IEEE754 and return `±inf` / `NaN` (no exceptions)

## Operators
- Arithmetic: `+ - * / % ^`
- Comparison: `< <= > >= == !=` (returns 1 or 0)
- Logical: `! && ||` (short-circuit)
- Ternary: `cond ? a : b` (short-circuit, right-associative)

## Variables
- `x, y, z, w` or `$1, $2, $3, $4`

## Function Whitelist
- 1-arg: `sin cos tan asin acos atan exp log log10 sqrt abs floor ceil round`
- 2-arg: `pow atan2 fmod min max`

## Optimizations (Safe Set)
- constant folding (when a subexpression is fully constant)
- short-circuit simplifications:
  - `0 && X -> 0`
  - `1 || X -> 1`
  - if `cond` in `cond ? A : B` is constant, keep only the selected branch
- unary plus simplification: `+X -> X`
- no algebraic reassociation that may alter edge semantics (for example signed zero behavior)

## Usage
```cpp
#include "bbb/exprdsl.hpp"

#include <iostream>

int main() {
	auto [e, err] = bbb::compile("x != 0 && (y / x) > 1 ? pow(z, 2) : 0");
	if (err) {
		std::cerr << "error at " << err->pos << ": " << err->message << "\n";
		return 1;
	}
	std::cout << e(2, 5, 3, 0) << "\n"; // 9
}
```
