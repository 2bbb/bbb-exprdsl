#pragma once
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bbb {
namespace detail { class bytecode_compiler; }

// =============================
// public api
// =============================

struct compile_error {
	std::size_t pos = 0;   // 0-based index into the input string
	std::string message;
};

struct compiled_expr {
	double operator()(double x, double y, double z, double w) const {
		ctx c{{x, y, z, w}};
		return vm_eval(c);
	}

	std::string expr;

private:
	struct ctx { double v[4]; };

	enum class op : std::uint8_t {
		push_const,
		push_var,
		pop,
		to_bool,     // normalize top to 0/1
		neg,
		logical_not,

		add, sub, mul, div_, mod, pow,

		lt, le, gt, ge, eq, ne,

		jz,          // pop cond; if false => pc = target
		jmp,         // pc = target

		call,        // arg = function id

		end
	};

	struct instr {
		op opcode = op::end;
		double imm = 0.0; // for push_const
		int arg = 0;      // var index, jump target, func id
	};

	std::vector<instr> code_;

	static bool truth(double v) { return v != 0.0; }

	double vm_eval(const ctx &c) const {
		std::vector<double> st;
		st.reserve(64);

		auto pop = [&]() -> double {
			double v = st.back();
			st.pop_back();
			return v;
		};
		auto push = [&](double v) { st.push_back(v); };

		std::size_t pc = 0;
		while (pc < code_.size()) {
			const instr &in = code_[pc];
			switch (in.opcode) {
				case op::push_const:
					push(in.imm);
					++pc;
					break;
				case op::push_var:
					push(c.v[in.arg]);
					++pc;
					break;
				case op::pop:
					(void)pop();
					++pc;
					break;
				case op::to_bool: {
					double v = pop();
					push(truth(v) ? 1.0 : 0.0);
					++pc;
					break;
				}
				case op::neg: {
					double a = pop();
					push(-a);
					++pc;
					break;
				}
				case op::logical_not: {
					double a = pop();
					push(!truth(a) ? 1.0 : 0.0);
					++pc;
					break;
				}

				case op::add: { double b = pop(), a = pop(); push(a + b); ++pc; break; }
				case op::sub: { double b = pop(), a = pop(); push(a - b); ++pc; break; }
				case op::mul: { double b = pop(), a = pop(); push(a * b); ++pc; break; }
				case op::div_: { double b = pop(), a = pop(); push(a / b); ++pc; break; }
				case op::mod: { double b = pop(), a = pop(); push(std::fmod(a, b)); ++pc; break; }
				case op::pow: { double b = pop(), a = pop(); push(std::pow(a, b)); ++pc; break; }

				case op::lt: { double b = pop(), a = pop(); push(a < b  ? 1.0 : 0.0); ++pc; break; }
				case op::le: { double b = pop(), a = pop(); push(a <= b ? 1.0 : 0.0); ++pc; break; }
				case op::gt: { double b = pop(), a = pop(); push(b < a  ? 1.0 : 0.0); ++pc; break; }
				case op::ge: { double b = pop(), a = pop(); push(b <= a ? 1.0 : 0.0); ++pc; break; }
				case op::eq: { double b = pop(), a = pop(); push(a == b ? 1.0 : 0.0); ++pc; break; }
				case op::ne: { double b = pop(), a = pop(); push(a != b ? 1.0 : 0.0); ++pc; break; }

				case op::jz: {
					double cond = pop(); // consumes condition
					if (!truth(cond)) pc = static_cast<std::size_t>(in.arg);
					else ++pc;
					break;
				}
				case op::jmp:
					pc = static_cast<std::size_t>(in.arg);
					break;

				case op::call: {
					const int fid = in.arg;

					auto pop1 = [&]() { return pop(); };
					auto pop2 = [&]() {
						double b = pop();
						double a = pop();
						return std::pair<double,double>{a,b};
					};

					switch (fid) {
						// 1-arg
						case 0: push(std::sin(pop1())); break;
						case 1: push(std::cos(pop1())); break;
						case 2: push(std::tan(pop1())); break;
						case 3: push(std::asin(pop1())); break;
						case 4: push(std::acos(pop1())); break;
						case 5: push(std::atan(pop1())); break;
						case 6: push(std::exp(pop1())); break;
						case 7: push(std::log(pop1())); break;
						case 8: push(std::log10(pop1())); break;
						case 9: push(std::sqrt(pop1())); break;
						case 10: push(std::fabs(pop1())); break;
						case 11: push(std::floor(pop1())); break;
						case 12: push(std::ceil(pop1())); break;
						case 13: push(std::round(pop1())); break;

						// 2-arg
						case 14: { auto [a, b] = pop2(); push(std::pow(a, b)); } break;
						case 15: { auto [a, b] = pop2(); push(std::atan2(a, b)); } break;
						case 16: { auto [a, b] = pop2(); push(std::fmod(a, b)); } break;
						case 17: { auto [a, b] = pop2(); push(a < b ? a : b); } break;
						case 18: { auto [a, b] = pop2(); push(b < a ? a : b); } break;

						default:
							push(std::numeric_limits<double>::quiet_NaN());
							break;
					}
					++pc;
					break;
				}

				case op::end:
					return st.empty() ? 0.0 : st.back();
			}
		}
		return st.empty() ? 0.0 : st.back();
	}

	friend std::pair<compiled_expr, std::optional<compile_error>>
	compile(std::string_view);
	friend class detail::bytecode_compiler;
};

// forward
inline std::pair<compiled_expr, std::optional<compile_error>>
compile(std::string_view input);

// =============================
// internals: lexer / parser / folding / bc compiler
// =============================
namespace detail {

// ---------- tokens ----------
enum class tok_kind : std::uint8_t {
	end,
	number,
	ident,
	var,      // var_index 0..3
	lparen, rparen,
	comma,
	plus, minus, star, slash, percent, caret,
	bang,
	less, less_eq, greater, greater_eq,
	eq_eq, bang_eq,
	and_and, or_or,
	question, colon
};

struct token {
	tok_kind kind = tok_kind::end;
	std::size_t pos = 0;
	double number = 0.0;
	int var_index = -1;
	std::string ident;
};

// ---------- lexer ----------
class lexer {
public:
	explicit lexer(std::string_view s) : src_(s) {}

	const token &peek() {
		if (!has_peek_) { peek_tok_ = next_impl(); has_peek_ = true; }
		return peek_tok_;
	}

	token next() {
		if (has_peek_) { has_peek_ = false; return peek_tok_; }
		return next_impl();
	}

private:
	std::string_view src_;
	std::size_t i_ = 0;
	bool has_peek_ = false;
	token peek_tok_;

	static bool is_ident_start(char c) {
		return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
	}
	static bool is_ident_cont(char c) {
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
	}

	void skip_ws() {
		while (i_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[i_]))) ++i_;
	}

	token next_impl() {
		skip_ws();
		token t;
		t.pos = i_;

		if (i_ >= src_.size()) { t.kind = tok_kind::end; return t; }
		const char c = src_[i_];

		auto match2 = [&](char a, char b) {
			return (i_ + 1 < src_.size() && src_[i_] == a && src_[i_ + 1] == b);
		};

		if (match2('&','&')) { i_ += 2; t.kind = tok_kind::and_and; return t; }
		if (match2('|','|')) { i_ += 2; t.kind = tok_kind::or_or;  return t; }
		if (match2('=','=')) { i_ += 2; t.kind = tok_kind::eq_eq;  return t; }
		if (match2('!','=')) { i_ += 2; t.kind = tok_kind::bang_eq; return t; }
		if (match2('<','=')) { i_ += 2; t.kind = tok_kind::less_eq; return t; }
		if (match2('>','=')) { i_ += 2; t.kind = tok_kind::greater_eq; return t; }

		switch (c) {
			case '(': ++i_; t.kind = tok_kind::lparen; return t;
			case ')': ++i_; t.kind = tok_kind::rparen; return t;
			case ',': ++i_; t.kind = tok_kind::comma;  return t;
			case '+': ++i_; t.kind = tok_kind::plus;   return t;
			case '-': ++i_; t.kind = tok_kind::minus;  return t;
			case '*': ++i_; t.kind = tok_kind::star;   return t;
			case '/': ++i_; t.kind = tok_kind::slash;  return t;
			case '%': ++i_; t.kind = tok_kind::percent;return t;
			case '^': ++i_; t.kind = tok_kind::caret;  return t;
			case '!': ++i_; t.kind = tok_kind::bang;   return t;
			case '<': ++i_; t.kind = tok_kind::less;   return t;
			case '>': ++i_; t.kind = tok_kind::greater;return t;
			case '?': ++i_; t.kind = tok_kind::question;return t;
			case ':': ++i_; t.kind = tok_kind::colon;  return t;
			default: break;
		}

		// $1..$4
		if (c == '$') {
			++i_;
			std::size_t start = i_;
			if (i_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[i_]))) {
				throw std::runtime_error("Expected digit after '$'");
			}
			int n = 0;
			while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_]))) {
				n = n * 10 + (src_[i_] - '0');
				++i_;
			}
			if (n < 1 || 4 < n) throw std::runtime_error("Variable index after '$' must be 1..4");
			t.kind = tok_kind::var;
			t.var_index = n - 1;
			t.pos = start - 1;
			return t;
		}

		// x,y,z,w
		if (c=='x' || c=='y' || c=='z' || c=='w') {
			++i_;
			t.kind = tok_kind::var;
			t.var_index = (c=='x') ? 0 : (c=='y') ? 1 : (c=='z') ? 2 : 3;
			return t;
		}

		// ident
		if (is_ident_start(c)) {
			std::size_t start = i_;
			++i_;
			while (i_ < src_.size() && is_ident_cont(src_[i_])) ++i_;
			t.kind = tok_kind::ident;
			t.ident = std::string(src_.substr(start, i_ - start));
			return t;
		}

		// number
		if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
			std::size_t start = i_;
			bool saw_digit = false;

			if (c == '.') {
				++i_;
				while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_]))) { ++i_; saw_digit = true; }
				if (!saw_digit) throw std::runtime_error("Invalid number literal");
			} else {
				while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_]))) { ++i_; saw_digit = true; }
				if (i_ < src_.size() && src_[i_] == '.') {
					++i_;
					while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_]))) ++i_;
				}
			}

			if (i_ < src_.size() && (src_[i_] == 'e' || src_[i_] == 'E')) {
				std::size_t epos = i_;
				++i_;
				if (i_ < src_.size() && (src_[i_] == '+' || src_[i_] == '-')) ++i_;
				if (i_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[i_]))) {
					i_ = epos;
				} else {
					while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_]))) ++i_;
				}
			}

			std::string num_str(src_.substr(start, i_ - start));
			char *endp = nullptr;
			double val = std::strtod(num_str.c_str(), &endp);
			if (!endp || endp == num_str.c_str()) throw std::runtime_error("Failed to parse number");

			t.kind = tok_kind::number;
			t.number = val;
			t.pos = start;
			return t;
		}

		throw std::runtime_error("Unexpected character");
	}
};

// ---------- function whitelist ----------
struct func_spec { int fid; int argc; };

inline const std::unordered_map<std::string, func_spec>& func_table() {
	// fid must match compiled_expr::op::call dispatch
	static const std::unordered_map<std::string, func_spec> tbl = {
		{"sin",   {0, 1}}, {"cos",   {1, 1}}, {"tan",   {2, 1}},
		{"asin",  {3, 1}}, {"acos",  {4, 1}}, {"atan",  {5, 1}},
		{"exp",   {6, 1}}, {"log",   {7, 1}}, {"log10", {8, 1}},
		{"sqrt",  {9, 1}}, {"abs",   {10,1}}, {"floor", {11,1}},
		{"ceil",  {12,1}}, {"round", {13,1}},
		{"pow",   {14,2}}, {"atan2", {15,2}}, {"fmod",  {16,2}},
		{"min",   {17,2}}, {"max",   {18,2}},
	};
	return tbl;
}

// ---------- ast (compile-time only) ----------
struct node { virtual ~node() = default; };
using node_ptr = std::unique_ptr<node>;

struct num_node : node { double n; std::size_t pos; num_node(double v, std::size_t p): n(v), pos(p) {} };
struct var_node : node { int index; std::size_t pos; var_node(int i, std::size_t p): index(i), pos(p) {} };

enum class un_op : std::uint8_t { plus, minus, logical_not, to_bool };
struct unary_node : node { un_op op; std::size_t pos; node_ptr a; unary_node(un_op o, std::size_t p, node_ptr x): op(o), pos(p), a(std::move(x)) {} };

enum class bin_op : std::uint8_t {
	add, sub, mul, div_, mod, pow,
	lt, le, gt, ge, eq, ne,
	and_and, or_or
};
struct binary_node : node { bin_op op; std::size_t pos; node_ptr l, r; binary_node(bin_op o, std::size_t p, node_ptr a, node_ptr b): op(o), pos(p), l(std::move(a)), r(std::move(b)) {} };

struct ternary_node : node { std::size_t pos; node_ptr c, t, f; ternary_node(std::size_t p, node_ptr cc, node_ptr tt, node_ptr ff): pos(p), c(std::move(cc)), t(std::move(tt)), f(std::move(ff)) {} };

struct call_node : node {
	int fid;
	int argc;
	std::size_t pos;
	std::vector<node_ptr> args;
	call_node(int f, int a, std::size_t p, std::vector<node_ptr> as): fid(f), argc(a), pos(p), args(std::move(as)) {}
};

// ---------- parser ----------
class parser {
public:
	explicit parser(std::string_view s) : lex_(s) {}

	node_ptr parse_all() {
		auto n = parse_expr();
		if (lex_.peek().kind != tok_kind::end) fail(lex_.peek().pos, "Unexpected token after end of expression");
		return n;
	}

private:
	lexer lex_;

	[[noreturn]] void fail(std::size_t pos, const std::string &msg) {
		throw std::runtime_error(std::to_string(pos) + ":" + msg);
	}

	bool accept(tok_kind k) {
		if (lex_.peek().kind == k) { lex_.next(); return true; }
		return false;
	}

	token expect(tok_kind k, const char *what) {
		token t = lex_.next();
		if (t.kind != k) fail(t.pos, std::string("Expected ") + what);
		return t;
	}

	node_ptr parse_expr() { return parse_conditional(); }

	node_ptr parse_conditional() {
		auto c = parse_logical_or();
		if (accept(tok_kind::question)) {
			std::size_t p = lex_.peek().pos;
			auto t = parse_expr();
			expect(tok_kind::colon, "':' in conditional operator");
			auto f = parse_conditional();
			return std::make_unique<ternary_node>(p, std::move(c), std::move(t), std::move(f));
		}
		return c;
	}

	node_ptr parse_logical_or() {
		auto n = parse_logical_and();
		while (accept(tok_kind::or_or)) {
			std::size_t p = lex_.peek().pos;
			auto r = parse_logical_and();
			n = std::make_unique<binary_node>(bin_op::or_or, p, std::move(n), std::move(r));
		}
		return n;
	}

	node_ptr parse_logical_and() {
		auto n = parse_equality();
		while (accept(tok_kind::and_and)) {
			std::size_t p = lex_.peek().pos;
			auto r = parse_equality();
			n = std::make_unique<binary_node>(bin_op::and_and, p, std::move(n), std::move(r));
		}
		return n;
	}

	node_ptr parse_equality() {
		auto n = parse_relational();
		while (true) {
			if (accept(tok_kind::eq_eq)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_relational();
				n = std::make_unique<binary_node>(bin_op::eq, p, std::move(n), std::move(r));
			} else if (accept(tok_kind::bang_eq)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_relational();
				n = std::make_unique<binary_node>(bin_op::ne, p, std::move(n), std::move(r));
			} else break;
		}
		return n;
	}

	node_ptr parse_relational() {
		auto n = parse_additive();
		while (true) {
			if (accept(tok_kind::less)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_additive();
				n = std::make_unique<binary_node>(bin_op::lt, p, std::move(n), std::move(r));
			} else if (accept(tok_kind::less_eq)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_additive();
				n = std::make_unique<binary_node>(bin_op::le, p, std::move(n), std::move(r));
			} else if (accept(tok_kind::greater)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_additive();
				n = std::make_unique<binary_node>(bin_op::gt, p, std::move(n), std::move(r));
			} else if (accept(tok_kind::greater_eq)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_additive();
				n = std::make_unique<binary_node>(bin_op::ge, p, std::move(n), std::move(r));
			} else break;
		}
		return n;
	}

	node_ptr parse_additive() {
		auto n = parse_multiplicative();
		while (true) {
			if (accept(tok_kind::plus)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_multiplicative();
				n = std::make_unique<binary_node>(bin_op::add, p, std::move(n), std::move(r));
			} else if (accept(tok_kind::minus)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_multiplicative();
				n = std::make_unique<binary_node>(bin_op::sub, p, std::move(n), std::move(r));
			} else break;
		}
		return n;
	}

	node_ptr parse_multiplicative() {
		auto n = parse_unary();
		while (true) {
			if (accept(tok_kind::star)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_unary();
				n = std::make_unique<binary_node>(bin_op::mul, p, std::move(n), std::move(r));
			} else if (accept(tok_kind::slash)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_unary();
				n = std::make_unique<binary_node>(bin_op::div_, p, std::move(n), std::move(r));
			} else if (accept(tok_kind::percent)) {
				std::size_t p = lex_.peek().pos;
				auto r = parse_unary();
				n = std::make_unique<binary_node>(bin_op::mod, p, std::move(n), std::move(r));
			} else break;
		}
		return n;
	}

	node_ptr parse_unary() {
		if (accept(tok_kind::plus))  return std::make_unique<unary_node>(un_op::plus, lex_.peek().pos, parse_unary());
		if (accept(tok_kind::minus)) return std::make_unique<unary_node>(un_op::minus, lex_.peek().pos, parse_unary());
		if (accept(tok_kind::bang))  return std::make_unique<unary_node>(un_op::logical_not, lex_.peek().pos, parse_unary());
		return parse_power();
	}

	node_ptr parse_power() {
		auto n = parse_primary();
		if (accept(tok_kind::caret)) {
			std::size_t p = lex_.peek().pos;
			auto r = parse_unary();
			n = std::make_unique<binary_node>(bin_op::pow, p, std::move(n), std::move(r));
		}
		return n;
	}

	node_ptr parse_primary() {
		const token &t = lex_.peek();
		switch (t.kind) {
			case tok_kind::number: {
				token tt = lex_.next();
				return std::make_unique<num_node>(tt.number, tt.pos);
			}
			case tok_kind::var: {
				token tt = lex_.next();
				if (tt.var_index < 0 || 3 < tt.var_index) fail(tt.pos, "Invalid variable index");
				return std::make_unique<var_node>(tt.var_index, tt.pos);
			}
			case tok_kind::ident: {
				token id = lex_.next();
				if (!accept(tok_kind::lparen)) fail(id.pos, "Identifier must be a function call like name(...)");
				auto it = func_table().find(id.ident);
				if (it == func_table().end()) fail(id.pos, "Unknown or disallowed function: " + id.ident);

				func_spec spec = it->second;
				std::vector<node_ptr> args;

				if (!accept(tok_kind::rparen)) {
					args.push_back(parse_expr());
					while (accept(tok_kind::comma)) args.push_back(parse_expr());
					expect(tok_kind::rparen, "')' to close function call");
				}

				if (static_cast<int>(args.size()) != spec.argc) {
					fail(id.pos, "Function '" + id.ident + "' expects " + std::to_string(spec.argc) +
											 " args, got " + std::to_string(args.size()));
				}
				return std::make_unique<call_node>(spec.fid, spec.argc, id.pos, std::move(args));
			}
			case tok_kind::lparen: {
				(void)lex_.next();
				auto n = parse_expr();
				expect(tok_kind::rparen, "')'");
				return n;
			}
			default:
				fail(t.pos, "Expected primary expression");
		}
	}
};

inline compile_error to_compile_error(const std::runtime_error &e) {
	std::string s = e.what();
	auto p = s.find(':');
	if (p == std::string::npos) return compile_error{0, s};
	std::size_t pos = 0;
	try { pos = static_cast<std::size_t>(std::stoull(s.substr(0, p))); }
	catch (...) { pos = 0; }
	return compile_error{pos, s.substr(p + 1)};
}

// ---------- constant folding (safe set) ----------
inline bool is_num(const node &n, double *out = nullptr) {
	if (auto p = dynamic_cast<const num_node *>(&n)) {
		if (out) *out = p->n;
		return true;
	}
	return false;
}

inline double b2d(bool v) { return v ? 1.0 : 0.0; }
inline bool truth(double v) { return v != 0.0; }

inline double eval_func(int fid, double a) {
	switch (fid) {
		case 0: return std::sin(a);
		case 1: return std::cos(a);
		case 2: return std::tan(a);
		case 3: return std::asin(a);
		case 4: return std::acos(a);
		case 5: return std::atan(a);
		case 6: return std::exp(a);
		case 7: return std::log(a);
		case 8: return std::log10(a);
		case 9: return std::sqrt(a);
		case 10: return std::fabs(a);
		case 11: return std::floor(a);
		case 12: return std::ceil(a);
		case 13: return std::round(a);
		default: return std::numeric_limits<double>::quiet_NaN();
	}
}

inline double eval_func(int fid, double a, double b) {
	switch (fid) {
		case 14: return std::pow(a,b);
		case 15: return std::atan2(a,b);
		case 16: return std::fmod(a,b);
		case 17: return (a<b) ? a : b;
		case 18: return (b < a) ? a : b;
		default: return std::numeric_limits<double>::quiet_NaN();
	}
}

inline node_ptr fold_constants(node_ptr n) {
	if (!n) return n;

	if (dynamic_cast<num_node *>(n.get()) || dynamic_cast<var_node *>(n.get())) return n;

	if (auto p = dynamic_cast<unary_node *>(n.get())) {
		p->a = fold_constants(std::move(p->a));

		double av = 0.0;
		if (is_num(*p->a, &av)) {
			switch (p->op) {
				case un_op::plus:  return std::make_unique<num_node>(+av, p->pos);
				case un_op::minus: return std::make_unique<num_node>(-av, p->pos);
				case un_op::logical_not: return std::make_unique<num_node>(b2d(!truth(av)), p->pos);
				case un_op::to_bool: return std::make_unique<num_node>(b2d(truth(av)), p->pos);
			}
		}

		if (p->op == un_op::plus) return std::move(p->a); // +x -> x
		return n;
	}

	if (auto p = dynamic_cast<call_node *>(n.get())) {
		for (auto &a : p->args) a = fold_constants(std::move(a));

		if (p->argc == 1) {
			double a0 = 0.0;
			if (is_num(*p->args[0], &a0)) {
				return std::make_unique<num_node>(eval_func(p->fid, a0), p->pos);
			}
		} else if (p->argc == 2) {
			double a0 = 0.0, a1 = 0.0;
			if (is_num(*p->args[0], &a0) && is_num(*p->args[1], &a1)) {
				return std::make_unique<num_node>(eval_func(p->fid, a0, a1), p->pos);
			}
		}
		return n;
	}

	if (auto p = dynamic_cast<ternary_node *>(n.get())) {
		p->c = fold_constants(std::move(p->c));

		double cv = 0.0;
		if (is_num(*p->c, &cv)) {
			if (truth(cv)) return fold_constants(std::move(p->t));
			return fold_constants(std::move(p->f));
		}

		p->t = fold_constants(std::move(p->t));
		p->f = fold_constants(std::move(p->f));
		return n;
	}

	if (auto p = dynamic_cast<binary_node *>(n.get())) {
		p->l = fold_constants(std::move(p->l));

		if (p->op == bin_op::and_and) {
			double lv = 0.0;
			if (is_num(*p->l, &lv)) {
				if (!truth(lv)) {
					return std::make_unique<num_node>(0.0, p->pos);
				} else {
					p->r = fold_constants(std::move(p->r));
					return std::make_unique<unary_node>(un_op::to_bool, p->pos, std::move(p->r));
				}
			}
			p->r = fold_constants(std::move(p->r));
			return n;
		}

		if (p->op == bin_op::or_or) {
			double lv = 0.0;
			if (is_num(*p->l, &lv)) {
				if (truth(lv)) {
					return std::make_unique<num_node>(1.0, p->pos);
				} else {
					p->r = fold_constants(std::move(p->r));
					return std::make_unique<unary_node>(un_op::to_bool, p->pos, std::move(p->r));
				}
			}
			p->r = fold_constants(std::move(p->r));
			return n;
		}

		p->r = fold_constants(std::move(p->r));

		double a = 0.0, b = 0.0;
		if (is_num(*p->l, &a) && is_num(*p->r, &b)) {
			double res = std::numeric_limits<double>::quiet_NaN();
			switch (p->op) {
				case bin_op::add: res = a + b; break;
				case bin_op::sub: res = a - b; break;
				case bin_op::mul: res = a * b; break;
				case bin_op::div_: res = a / b; break;
				case bin_op::mod: res = std::fmod(a, b); break;
				case bin_op::pow: res = std::pow(a, b); break;

				case bin_op::lt: res = b2d(a <  b); break;
				case bin_op::le: res = b2d(a <= b); break;
				case bin_op::gt: res = b2d(b <  a); break;
				case bin_op::ge: res = b2d(b <= a); break;
				case bin_op::eq: res = b2d(a == b); break;
				case bin_op::ne: res = b2d(a != b); break;

				case bin_op::and_and:
				case bin_op::or_or:
					break;
			}
			return std::make_unique<num_node>(res, p->pos);
		}

		return n;
	}

	return n;
}

// ---------- bytecode compiler ----------
class bytecode_compiler {
public:
	using op = compiled_expr::op;
	using instr = compiled_expr::instr;

	std::vector<instr> code;

	void emit(op opcode, int arg = 0, double imm = 0.0) {
		instr in;
		in.opcode = opcode;
		in.arg = arg;
		in.imm = imm;
		code.push_back(in);
	}

	std::size_t emit_placeholder(op opcode) {
		emit(opcode, 0, 0.0);
		return code.size() - 1;
	}

	void patch_target(std::size_t at, std::size_t target) {
		code[at].arg = static_cast<int>(target);
	}

	void compile(const node &n) {
		if (auto p = dynamic_cast<const num_node *>(&n)) { emit(op::push_const, 0, p->n); return; }
		if (auto p = dynamic_cast<const var_node *>(&n)) { emit(op::push_var, p->index); return; }

		if (auto p = dynamic_cast<const unary_node *>(&n)) {
			compile(*p->a);
			switch (p->op) {
				case un_op::plus: break;
				case un_op::minus: emit(op::neg); break;
				case un_op::logical_not: emit(op::logical_not); break;
				case un_op::to_bool: emit(op::to_bool); break;
			}
			return;
		}

		if (auto p = dynamic_cast<const call_node *>(&n)) {
			for (auto &a : p->args) compile(*a);
			emit(op::call, p->fid);
			return;
		}

		if (auto p = dynamic_cast<const ternary_node *>(&n)) {
			compile(*p->c);
			emit(op::to_bool);
			std::size_t jz_else = emit_placeholder(op::jz);
			compile(*p->t);
			std::size_t jmp_end = emit_placeholder(op::jmp);
			std::size_t else_pc = code.size();
			patch_target(jz_else, else_pc);
			compile(*p->f);
			std::size_t end_pc = code.size();
			patch_target(jmp_end, end_pc);
			return;
		}

		if (auto p = dynamic_cast<const binary_node *>(&n)) { compile_binary(*p); return; }

		emit(op::push_const, 0, std::numeric_limits<double>::quiet_NaN());
	}

private:
	void compile_binary(const binary_node &b) {
		if (b.op == bin_op::and_and) {
			compile(*b.l);
			emit(op::to_bool);
			std::size_t jz_false = emit_placeholder(op::jz);
			compile(*b.r);
			emit(op::to_bool);
			std::size_t jmp_end = emit_placeholder(op::jmp);
			std::size_t false_pc = code.size();
			patch_target(jz_false, false_pc);
			emit(op::push_const, 0, 0.0);
			std::size_t end_pc = code.size();
			patch_target(jmp_end, end_pc);
			return;
		}

		if (b.op == bin_op::or_or) {
			compile(*b.l);
			emit(op::to_bool);
			std::size_t jz_eval_b = emit_placeholder(op::jz);
			emit(op::push_const, 0, 1.0);
			std::size_t jmp_end = emit_placeholder(op::jmp);
			std::size_t eval_b_pc = code.size();
			patch_target(jz_eval_b, eval_b_pc);
			compile(*b.r);
			emit(op::to_bool);
			std::size_t end_pc = code.size();
			patch_target(jmp_end, end_pc);
			return;
		}

		compile(*b.l);
		compile(*b.r);

		switch (b.op) {
			case bin_op::add: emit(op::add); break;
			case bin_op::sub: emit(op::sub); break;
			case bin_op::mul: emit(op::mul); break;
			case bin_op::div_: emit(op::div_); break;
			case bin_op::mod: emit(op::mod); break;
			case bin_op::pow: emit(op::pow); break;

			case bin_op::lt: emit(op::lt); break;
			case bin_op::le: emit(op::le); break;
			case bin_op::gt: emit(op::gt); break;
			case bin_op::ge: emit(op::ge); break;
			case bin_op::eq: emit(op::eq); break;
			case bin_op::ne: emit(op::ne); break;

			case bin_op::and_and:
			case bin_op::or_or:
				break;
		}
	}
};

} // namespace detail

// =============================
// compile()
// =============================
inline std::pair<compiled_expr, std::optional<compile_error>>
compile(std::string_view input) {
	compiled_expr out;
	out.expr = std::string(input);

	try {
		detail::parser p(input);
		auto ast = p.parse_all();

		// safe optimizations:
		// - fold pure constant subexpressions
		// - short-circuit simplifications for &&, ||, ?: when condition is constant
		ast = detail::fold_constants(std::move(ast));

		detail::bytecode_compiler bc;
		bc.compile(*ast);
		bc.emit(compiled_expr::op::end);

		out.code_ = std::move(bc.code);
		return {std::move(out), std::nullopt};
	} catch (const std::runtime_error &e) {
		return {compiled_expr{}, detail::to_compile_error(e)};
	} catch (...) {
		return {compiled_expr{}, compile_error{0, "Unknown error"}};
	}
}

} // namespace bbb
