#include <sim/cpp_syntax_highlighter.h>
#include <simlib/debug.h>
#include <simlib/logger.h>
#include <simlib/meta.h>
#include <simlib/utilities.h>

using std::array;
using std::string;
using std::vector;

#if 0
# warning "Before committing disable this debug"
# define DEBUG_CSH(...) __VA_ARGS__
# include <cassert>
# undef try_assert
# define try_assert assert
#else
# define DEBUG_CSH(...)
#endif

typedef int8_t StyleType;

namespace {

enum Style : StyleType {
	PREPROCESSOR = 0,
	COMMENT = 1,
	BUILTIN_TYPE = 2,
	KEYWORD = 3,
	STRING_LITERAL = 4,
	CHARACTER = 5,
	ESCAPED_CHARACTER = 6,
	DIGIT = 7,
	CONSTANT = 8,
	FUNCTION = 9,
	OPERATOR = 10
};

struct Word {
	const char* str;
	uint8_t size;
	Style style;

	template<uint8_t N>
	constexpr Word(const char(&s)[N], Style stl) : str(s), size(N - 1),
		style(stl) {}
};

} // anonymous namespace

static constexpr array<const char*, 11> begin_style {{
	"<span style=\"color:#00a000\">",
	"<span style=\"color:#a0a0a0\">",
	"<span style=\"color:#0000ff;font-weight:bold\">",
	"<span style=\"color:#c90049;font-weight:bold;\">",
	"<span style=\"color:#ff0000\">",
	"<span style=\"color:#e0a000\">",
	"<span style=\"color:#d923e9\">",
	"<span style=\"color:#d923e9\">",
	"<span style=\"color:#a800ff\">",
	"<span style=\"color:#0086b3\">",
	"<span style=\"color:#515125\">",
}};

static constexpr const char* end_style = "</span>";

static constexpr array<Word, 124> words {{
	{"", COMMENT}, // Guard - ignored
	{"uint_least16_t", BUILTIN_TYPE},
	{"uint_least32_t", BUILTIN_TYPE},
	{"uint_least64_t", BUILTIN_TYPE},
	{"uint_fast16_t", BUILTIN_TYPE},
	{"uint_fast64_t", BUILTIN_TYPE},
	{"uint_least8_t", BUILTIN_TYPE},
	{"int_least16_t", BUILTIN_TYPE},
	{"int_least32_t", BUILTIN_TYPE},
	{"int_least64_t", BUILTIN_TYPE},
	{"uint_fast32_t", BUILTIN_TYPE},
	{"uint_fast8_t", BUILTIN_TYPE},
	{"int_fast16_t", BUILTIN_TYPE},
	{"int_least8_t", BUILTIN_TYPE},
	{"int_fast32_t", BUILTIN_TYPE},
	{"int_fast64_t", BUILTIN_TYPE},
	{"int_fast8_t", BUILTIN_TYPE},
	{"_Char16_t", BUILTIN_TYPE},
	{"_Char32_t", BUILTIN_TYPE},
	{"uintptr_t", BUILTIN_TYPE},
	{"uintmax_t", BUILTIN_TYPE},
	{"wint_t", BUILTIN_TYPE},
	{"wctrans_t", BUILTIN_TYPE},
	{"unsigned", BUILTIN_TYPE},
	{"uint16_t", BUILTIN_TYPE},
	{"uint32_t", BUILTIN_TYPE},
	{"uint64_t", BUILTIN_TYPE},
	{"intmax_t", BUILTIN_TYPE},
	{"wctype_t", BUILTIN_TYPE},
	{"intptr_t", BUILTIN_TYPE},
	{"wchar_t", BUILTIN_TYPE},
	{"uint8_t", BUILTIN_TYPE},
	{"int16_t", BUILTIN_TYPE},
	{"int32_t", BUILTIN_TYPE},
	{"int64_t", BUILTIN_TYPE},
	{"wchar_t", BUILTIN_TYPE},
	{"double", BUILTIN_TYPE},
	{"signed", BUILTIN_TYPE},
	{"size_t", BUILTIN_TYPE},
	{"time_t", BUILTIN_TYPE},
	{"int8_t", BUILTIN_TYPE},
	{"short", BUILTIN_TYPE},
	{"float", BUILTIN_TYPE},
	{"void", BUILTIN_TYPE},
	{"char", BUILTIN_TYPE},
	{"bool", BUILTIN_TYPE},
	{"long", BUILTIN_TYPE},
	{"int", BUILTIN_TYPE},
	{"nullptr_t", BUILTIN_TYPE},
	{"auto", BUILTIN_TYPE},
	{"align_union", KEYWORD},
	{"alignof", KEYWORD},
	{"and", KEYWORD},
	{"and_eq", KEYWORD},
	{"asm", KEYWORD},
	{"bitand", KEYWORD},
	{"bitor", KEYWORD},
	{"break", KEYWORD},
	{"case", KEYWORD},
	{"catch", KEYWORD},
	{"class", KEYWORD},
	{"compl", KEYWORD},
	{"const", KEYWORD},
	{"const_cast", KEYWORD},
	{"constexpr", KEYWORD},
	{"continue", KEYWORD},
	{"decltype", KEYWORD},
	{"default", KEYWORD},
	{"delete", KEYWORD},
	{"do", KEYWORD},
	{"dynamic_cast", KEYWORD},
	{"else", KEYWORD},
	{"enum", KEYWORD},
	{"explicit", KEYWORD},
	{"export", KEYWORD},
	{"extern", KEYWORD},
	{"final", KEYWORD},
	{"for", KEYWORD},
	{"friend", KEYWORD},
	{"goto", KEYWORD},
	{"if", KEYWORD},
	{"import", KEYWORD},
	{"inline", KEYWORD},
	{"mutable", KEYWORD},
	{"namespace", KEYWORD},
	{"new", KEYWORD},
	{"noexcept", KEYWORD},
	{"not", KEYWORD},
	{"not_eq", KEYWORD},
	{"nullptr", KEYWORD},
	{"operator", KEYWORD},
	{"or", KEYWORD},
	{"or_eq", KEYWORD},
	{"override", KEYWORD},
	{"private", KEYWORD},
	{"protected", KEYWORD},
	{"public", KEYWORD},
	{"register", KEYWORD},
	{"reinterpret_cast", KEYWORD},
	{"return", KEYWORD},
	{"sizeof", KEYWORD},
	{"static", KEYWORD},
	{"static_assert", KEYWORD},
	{"static_cast", KEYWORD},
	{"struct", KEYWORD},
	{"switch", KEYWORD},
	{"template", KEYWORD},
	{"this", KEYWORD},
	{"throw", KEYWORD},
	{"try", KEYWORD},
	{"typedef", KEYWORD},
	{"typeid", KEYWORD},
	{"typename", KEYWORD},
	{"union", KEYWORD},
	{"using", KEYWORD},
	{"virtual", KEYWORD},
	{"volatile", KEYWORD},
	{"while", KEYWORD},
	{"xor", KEYWORD},
	{"xor_eq", KEYWORD},
	{"true", CONSTANT},
	{"false", CONSTANT},
	{"NULL", CONSTANT},
	{"nullptr", CONSTANT},
}};

/* Some ugly meta programming used to extract KEYWORDS from words */

template<size_t N>
constexpr uint count_keywords(const array<Word, N>& arr, size_t idx = 0) {
	return (idx == N ? 0 :
		(arr[idx].style == KEYWORD) + count_keywords(arr, idx + 1));
}

template<size_t N, size_t... Idx>
constexpr array<meta::string, N> extract_keywords_from_append(
	const array<meta::string, N>& base, meta::string x, meta::Seq<Idx...>)
{
	return {{base[Idx]..., x}};
}

template<size_t N, size_t RES_N, size_t RES_END>
constexpr typename std::enable_if<
	RES_END >= RES_N, array<meta::string, RES_N>>::type
	extract_keywords_from(const array<Word, N>&,
	array<meta::string, RES_N> res = {}, size_t = 0)
{
	return res;
}

template<size_t N, size_t RES_N, size_t RES_END = 0>
constexpr typename std::enable_if<
	RES_END < RES_N, array<meta::string, RES_N>>::type
	extract_keywords_from(const array<Word, N>& arr,
	array<meta::string, RES_N> res = {}, size_t idx = 0)
{
	return (idx == N ? res : (arr[idx].style == KEYWORD ?
		extract_keywords_from<N, RES_N, RES_END + 1>(
			arr, extract_keywords_from_append(
				res, {arr[idx].str, arr[idx].size}, meta::GenSeq<RES_END>{}),
			idx + 1)
		: extract_keywords_from<N, RES_N, RES_END>(arr, res, idx + 1)));
}

static constexpr auto cpp_keywords =
	extract_keywords_from<words.size(), count_keywords(words)>(words);

// Important: elements have to be sorted!
static_assert(meta::is_sorted(cpp_keywords),
	"cpp_keywords has to be sorted, fields in words with style KEYWORD are "
	"probably unsorted");

CppSyntaxHighlighter::CppSyntaxHighlighter() {
	static_assert(words[0].size == 0, "First (zero) element of words is a guard"
		" - because Aho-Corasick implementation takes only positive IDs");
	for (uint i = 1; i < words.size(); ++i)
		aho.addPattern(words[i].str, i);

	aho.buildFails();
}

string CppSyntaxHighlighter::operator()(const std::string& input) const {
	constexpr int BEGIN_GUARDS = 2, BEGIN = BEGIN_GUARDS;
	constexpr int END_GUARDS = 2;
	constexpr char GUARD_CHARACTER = '\0';

	// Make sure we can use int
	if (input.size() + BEGIN_GUARDS + END_GUARDS > INT_MAX)
		THROW("Input string too long");

	/* Remove "\\\n" sequences */
	int end = input.size();
	// BEGIN_GUARDS x '\0' - guards
	string str(BEGIN_GUARDS, GUARD_CHARACTER);
	str.reserve(end + 64); // Pre-allocation

	for (int i = 0; i < end; ++i) {
		// i + 1 is safe - std::string adds extra '\0' at the end
		if (input[i] == '\\' && input[i + 1] == '\n') {
			++i;
			continue;
		}

		str += input[i];
	}

	end = str.size();

	// Append end guards (one is already in str - terminating null character)
	str.insert(str.end(), std::max(0, END_GUARDS - 1), GUARD_CHARACTER);
	DEBUG_CSH(stdlog("end: ", toString(end));)

	vector<StyleType> begs(end, -1); // here beginnings of styles are marked
	vector<int8_t> ends(str.size() + 1); // ends[i] = # of endings (of styles)
	                                     // JUST BEFORE str[i]

	/* Mark comments string / character literals */

	for (int i = BEGIN; i < end; ++i) {
		// Comments
		if (str[i] == '/') {
			// (One-)line comment
			if (str[i + 1] == '/') {
				begs[i] = COMMENT;
				i += 2;
				while (i < end && str[i] != '\n')
					++i;
				++ends[i];

			// Multi-line comment
			} else if (str[i + 1] == '*') {
				begs[i] = COMMENT;
				i += 3;
				while (i < end && !(str[i - 1] == '*' && str[i] == '/'))
					++i;
				++ends[i + 1];
			}
			continue;
		}

		// String / character literals
		if (str[i] == '"' || str[i] == '\'') {
			char literal_delim = str[i];
			begs[i++] = (literal_delim == '"' ? STRING_LITERAL : CHARACTER);
			// End on newline - to mitigate invalid literals
			while (i < end && str[i] != literal_delim && str[i] != '\n') {
				// Ordinary character
				if (str[i] != '\\') {
					++i;
					continue;
				}

				/* Escape sequence */
				begs[i++] = ESCAPED_CHARACTER;

				// Octals and Hexadecimal
				if (isdigit(str[i]) || str[i] == 'x')
					i += 2;
				// Unicode U+nnnn
				else if (str[i] == 'u')
					i += 4;
				// Unicode U+nnnnnnnn
				else if (str[i] == 'U')
					i += 8;

				// i has to point to the last character from escaped sequence
				i = std::min(i + 1, end);
				++ends[i];
			}
			++ends[i + 1];
		}
	}

	DEBUG_CSH(auto dump_begs_ends = [&] {
		for (int i = BEGIN, j = 0, line = 1; i < end; ++i, ++j) {
			while (str[i] != input[j]) {
				try_assert(input[j] == '\\' && j + 1 < (int)input.size()
					&& input[j + 1] == '\n');
				j += 2;
				++line;
			}
			auto tmplog = stdlog(toString(i), " (line ", toString(line), "): '",
				str[i], "' -> ");

			if (ends[i] == 0)
				tmplog("0, ");
			else
				tmplog("\033[1;32m", toString(ends[i]), "\033[m, ");

			switch (begs[i]) {
			case -1:
				tmplog("-1"); break;
			case PREPROCESSOR:
				tmplog("PREPROCESSOR"); break;
			case COMMENT:
				tmplog("COMMENT"); break;
			case BUILTIN_TYPE:
				tmplog("BUILTIN_TYPE"); break;
			case KEYWORD:
				tmplog("KEYWORD"); break;
			case STRING_LITERAL:
				tmplog("STRING_LITERAL"); break;
			case CHARACTER:
				tmplog("CHARACTER"); break;
			case ESCAPED_CHARACTER:
				tmplog("ESCAPED_CHARACTER"); break;
			case DIGIT:
				tmplog("DIGIT"); break;
			case CONSTANT:
				tmplog("CONSTANT"); break;
			case FUNCTION:
				tmplog("FUNCTION"); break;
			case OPERATOR:
				tmplog("OPERATOR"); break;
			default: tmplog("???");
			}

			if (str[i] == '\n')
				++line;
		}
	};)

	DEBUG_CSH(dump_begs_ends();)

	auto isName = [](int c) { return (c == '_' || c == '$' || isalnum(c)); };

	/* Mark preprocessor, functions and numbers */

	for (int i = BEGIN, styles_depth = 0; i < end; ++i) {
		auto control_style_depth = [&] {
			styles_depth += (begs[i] != -1) - ends[i];
		};

		control_style_depth();
		if (styles_depth > 0)
			continue;

		// Preprocessor
		if (str[i] == '#') {
			begs[i] = PREPROCESSOR;
			do {
				++i;
				control_style_depth();
			} while (i < end && (styles_depth || str[i] != '\n'));
			//                   ^ this is here because preprocessor directives
			// can break (on '\n') with e.g. multi-line comments

			++ends[i];

		// Function
		} else if (str[i] == '(' && i) {
			int k = i - 1;
			// Omit white-spaces between function name and '('
			static_assert(BEGIN_GUARDS > 0, "Need for unguarded search");
			while (isspace(str[k]))
				--k;

			int name_end = k + 1;

			// Function name
			for (;;) {
				// Namespaces
				static_assert(BEGIN_GUARDS > 0, "");
				if (str[k] == ':' && str[k - 1] == ':' && str[k + 1] != ':')
					--k;
				else if (!isName(str[k]))
					break;

				--k;
			}
			++k;

			struct cmp {
				bool operator()(const meta::string& a, const StringView& b)
					const
				{
					return StringView(a.data(), a.size()) < b;
				}

				bool operator()(const StringView& a, const meta::string& b)
					const
				{
					return a < StringView(b.data(), b.size());
				}
			};

			// Function name can neither be blank nor begin with a digit
			if (k < name_end && !isdigit(str[k])) {
				StringView function_name = substring(str, k, name_end);
				// It is important that function_name is taken as StringView
				// below!
				begs[k] = (std::binary_search(cpp_keywords.begin(),
						cpp_keywords.end(), function_name, cmp())
					? KEYWORD : FUNCTION);
				++ends[name_end];
			}

		// Digits - numeric literals
		} else if (isdigit(str[i])) {
			static_assert(BEGIN_GUARDS > 0, "");
			// Ignore digits in names
			if (isName(str[i - 1]))
				continue;
			// Floating points may begin with '.', but inhibit highlighting
			// the whole number 111.2.3
			static_assert(GUARD_CHARACTER != '.', "");
			if (str[i - 1] == '.' && isdigit(str[i - 2]))
				continue;

			// Number begins with dot
			bool dot_appeared = false, dot_as_beginning = false,
				exponent_appeared = false;
			if (str[i - 1] == '.') {
				dot_as_beginning = dot_appeared = true;
				begs[i - 1] = DIGIT;
			} else
				begs[i] = DIGIT;

			int k = i + 1;
			auto is_digit = isdigit;
			char exp_sign = 'e';

			// Hexadecimal
			if (str[i] == '0' && tolower(str[k]) == 'x' && !dot_appeared) {
				is_digit = isxdigit;
				exp_sign = 'p';
				++k;
				if (str[k] == '.') {
					dot_as_beginning = dot_appeared = true;
					++k;

				// Disallow numeric literal "0x"
				} else if (!is_digit(str[k])) {
					begs[i - dot_as_beginning] = -1;
					i = k - 1;
					continue;
				}
			}

			// Now the main part of the numeric literal (digits + exponent)
			for (; k < end; ++k) {
				// Digits are always valid
				if (is_digit(str[k]))
					continue;

				// Dot
				if (str[k] == '.') {
					// Disallow double dot and double exponent
					if (dot_appeared || exponent_appeared)
						goto kill_try;

					dot_appeared = true;

				// Exponent
				} else if (tolower(str[k]) == exp_sign) {
					// Do not allow cases like: 0.1e1e or .e2
					if (exponent_appeared ||
						(str[k - 1] == '.' && dot_as_beginning))
					{
						goto kill_try;
					}

					exponent_appeared = true;

				// Sign after exponent
				} else if (str[k - 1] == exp_sign &&
				(str[k] == '-' || str[k] == '+'))
				{
					continue;

				} else
					break;
			}

			// Ignore invalid numeric literals
			if ((str[k - 1] == '.' && dot_as_beginning) ||
				// In floating-point hexadecimals exponent has to appear
				(exp_sign == 'p' && dot_appeared && !exponent_appeared) ||
				// Allow literals like: "111."
				(str[k - 1] != '.' && !is_digit(str[k - 1])))
			{
			kill_try:
				// dot_as_beginning does not imply beg[i - 1] in hexadecimals
				begs[i - (dot_as_beginning && exp_sign == 'e')] = -1;
				i = k - 1;
				continue;
			}

			/* Suffixes */
			i = k;
			static_assert(END_GUARDS > 0, "");
			// Float-point: (f|l)
			if (dot_appeared || exponent_appeared) {
				if (str[i] == 'f' || str[i] == 'F' ||
					str[i] == 'l' || str[i] == 'L')
				{
					++i;
				}
				++ends[i];
				continue;
			}

			// Integer (l|ll|lu|llu|u|ul|ull) or float-point (f)
			if (str[i] == 'f' || str[i] == 'F') {
				++i;

			} else if (str[i] == 'l' || str[i] == 'L') { // l
				++i;
				if (str[i] == 'u' || str[i] == 'U') // lu
					++i;
				// Because lL and Ll suffixes are wrong
				else if (str[i] == str[i - 1]) { // ll | LL
					++i;
					if (str[i] == 'u' || str[i] == 'U') // llu
						++i;
				}
			} else if (str[i] == 'u' || str[i] == 'U') { // u
				++i;
				if (str[i] == 'l' || str[i] == 'L') { // ul
					++i;
					// Because lL and Ll suffixes are wrong
					if (str[i] == str[i - 1]) // ull | uLL
						++i;
				}
			}
			++ends[i];
		}
	}

	DEBUG_CSH(dump_begs_ends();)

	auto isOperator = [](unsigned char c) {
		// In xxx are marked characters from "!%&()*+,-./:;<=>?[\\]^{|}~";
		constexpr array<bool, 128> xxx {{
		//      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
		/* 0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 1 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 2 */ 0, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,
		/* 3 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
		/* 4 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 5 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,
		/* 6 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 7 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,
		}};
		return (c < 128 && xxx[c]);
	};

	/* Highlight non-styled code */

	// Highlights fragments of code not containing: comments, preprocessor,
	// number literals, string literals and character literals
	auto highlight = [&](int beg, int endi) {
		DEBUG_CSH(stdlog("highlight(", toString(beg), ", ", toString(endi),
			"):");)
		if (beg >= endi)
			return;

		auto aho_res = aho.searchIn(substring(str, beg, endi));
		// Handle last one to eliminate right boundary checks
		for (int i = endi - 1; i >= beg;) {
			int k = aho.pattId(aho_res[i - beg]), j;

			static_assert(BEGIN_GUARDS > 0, "");
			static_assert(END_GUARDS > 0, "");
			// Found a word
			if (k && !isName(str[i + 1]) &&
				!isName(str[j = i - words[k].size]))
			{
				DEBUG_CSH(stdlog("aho: ", toString(j + 1), ": ", words[k].str);)
				begs[j + 1] = words[k].style;
				++ends[i + 1];
				i = j;
				continue;

			} else if (isOperator(str[i])) {
				begs[i] = OPERATOR;
				++ends[i + 1];
			}
			--i;
		}
	};

	/* Find non-styled segments and tun highlight() on them */
	{
		int last_non_styled = 0;
		for (int i = BEGIN, styles_depth = 0; i < end; ++i) {
			int k = styles_depth;
			if (begs[i] != -1)
				++styles_depth;
			styles_depth -= ends[i];

			if (k == 0 && styles_depth > 0) {
				highlight(last_non_styled, i);
				// Needed for proper non-styled interval after this loop
				last_non_styled = end;

			} else if (k > 0 && styles_depth == 0)
				last_non_styled = i;
		}
		highlight(last_non_styled, end); // Take care of last non-styled piece
	}

	DEBUG_CSH(dump_begs_ends();)

	/* Parse styles and produce result */

	string res = "<table class=\"code-view\">"
		"<tbody>"
		"<tr><td id=\"L1\" line=\"1\"></td><td>";
	// Stack of styles (needed to properly break on '\n')
	vector<StyleType> style_stack;
	int first_unescaped = BEGIN;
	for (int i = BEGIN, j = 0, line = 1; i < end; ++i, ++j) {
		// End styles
		if (ends[i]) {
			htmlSpecialChars(res, substring(str, first_unescaped, i));
			first_unescaped = i;

			// Leave last style and try to elide it
			while (ends[i] > 1) {
				style_stack.pop_back();
				res += end_style;
				--ends[i];
			}

			if (ends[i]) {
				// Style elision (to compress and simplify output)
				if (style_stack.back() == begs[i])
					begs[i] = -1;
				else {
					style_stack.pop_back();
					res += end_style;
				}
			}
		}

		// Handle erased "\\\n" sequences
		if (str[i] != input[j]) {
			htmlSpecialChars(res, substring(str, first_unescaped, i));
			first_unescaped = i;
			// End styles
			for (int k = style_stack.size(); k > 0; --k)
				res += end_style;
			// Handle "\\\n"
			do {
				try_assert(input[j] == '\\' && j + 1 < (int)input.size()
					&& input[j + 1] == '\n');

				string line_str = toString(++line);
				// When we were ending styles (somewhere above), there can be
				// an opportunity to elide style OPERATOR (only in the first
				// iteration of this loop) but it's not worth that, as it's a
				// very rare situation and gives little profit at the expense of
				// a not pretty if statement
				back_insert(res, begin_style[OPERATOR], '\\', end_style,
					"</td></tr>"
					"<tr><td id=\"L", line_str, "\" line=\"", line_str,
						"\"></td><td>");
			} while (str[i] != input[j += 2]);

			// Restore styles
			for (StyleType style : style_stack)
				res += begin_style[style];
		}

		// Line ending
		if (str[i] == '\n') {
			htmlSpecialChars(res, substring(str, first_unescaped, i));
			first_unescaped = i + 1;
			// End styles
			for (int k = style_stack.size(); k > 0; --k)
				res += end_style;
			// Fill the empty line with '\n'
			if (j == 0 || input[j - 1] == '\n')
				res += '\n';
			// Break the line
			string line_str = toString(++line);
			back_insert(res, "</td></tr>"
				"<tr><td id=\"L", line_str, "\" line=\"", line_str,
					"\"></td><td>");
			// Restore styles
			for (StyleType style : style_stack)
				res += begin_style[style];
		}

		// Begin style
		if (begs[i] != -1) {
			htmlSpecialChars(res, substring(str, first_unescaped, i));
			first_unescaped = i;

			style_stack.emplace_back(begs[i]);
			res += begin_style[begs[i]];
		}
	}

	// Ending
	htmlSpecialChars(res, substring(str, first_unescaped, end));
	// End styles
	for (uint i = end; i < ends.size(); ++i)
		while (ends[i]--)
			res += end_style;
	// End table
	res += "</td></tr>"
			"</tbody>"
		"</table>";

	return res;
}
